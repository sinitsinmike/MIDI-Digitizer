// File: Digitizer_midi_LITE_fit_nano.ino
// Target: Arduino Nano / ATmega328P (DIN MIDI on TX @31250)
// LITE build: минимальный размер прошивки и ОЗУ, влезает в 30k/2k.
// ─────────────────────────────────────────────────────────────────────────────

// ======================== BUILD SWITCHES =====================================
#define LITE_BUILD 1              // LITE: да (экономит место)

// === ДИСПЛЕЙ: выбери ОДИН ===
//#define DISP_LCD1602            // 1602 (LiquidCrystal_I2C)
#define DISP_SSD1306_U8X8         // SSD1306 128x64, U8x8 текстовый

// ЯЗЫК UI (компиляция): раскомментируй для русского
// #define UI_LANG_RU

// ======================== INCLUDES / LIBS ====================================
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <MIDI.h>

#if defined(DISP_LCD1602)
  #include <LiquidCrystal_I2C.h>
  #ifndef LCD_ADDR
    #define LCD_ADDR 0x27
  #endif
  #ifndef LCD_COLS
    #define LCD_COLS 16
  #endif
  #ifndef LCD_ROWS
    #define LCD_ROWS 2
  #endif
  LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
#elif defined(DISP_SSD1306_U8X8)
  #include <U8x8lib.h>
  // I2C SSD1306 128x64, аппаратный I2C, дефолтный адрес 0x3C
  U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);
#else
  #error "Select display: DISP_LCD1602 or DISP_SSD1306_U8X8"
#endif

// ======================== LOCALE MACRO =======================================
#ifdef UI_LANG_RU
  #undef UI_LANG_RU
  #define UI_LANG_RU 1
#else
  #define UI_LANG_RU 0
#endif
#if UI_LANG_RU
  #define L(ru,en) F(ru)
#else
  #define L(ru,en) F(en)
#endif

// ======================== PINS ===============================================
static const uint8_t PIN_XP = A0;  // Touch X+
static const uint8_t PIN_YP = A1;  // Touch Y+
static const uint8_t PIN_XM = 5;   // Touch X-
static const uint8_t PIN_YM = 6;   // Touch Y-

static const uint8_t ENC_A   = 2;
static const uint8_t ENC_B   = 3;
static const uint8_t ENC_BTN = 8;

static const uint8_t BTN_NEXT= 9;   // шаг по пунктам меню
static const uint8_t BTN_SAVE= 10;  // сохранить Perf

// ======================== MIDI ==============================================
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, DINMIDI);
static const uint8_t MIDI_CH = 1;

// ======================== TOUCH / ADC =======================================
static const uint16_t ADC_MAX = 1023;
static const uint8_t  SAMPLES = 5;
static const uint16_t TOUCH_NOISE = 12;     // порог «не 0 и не 1023»
static const uint16_t PULL_DELTA  = 128;    // простая проверка присутствия касания

// ======================== UI / STATE ========================================
struct DebBtn{ uint8_t pin; uint8_t st; uint32_t t; };
static DebBtn bEnc{ENC_BTN,HIGH,0}, bNext{BTN_NEXT,HIGH,0}, bSave{BTN_SAVE,HIGH,0};
static uint32_t encPressTs=0;
static bool encRotDuringHold=false;
static const uint32_t LONG_PRESS_MS = 5000;

enum State:uint8_t{RUN, CAL_WAIT, CAL_RUN, MENU};
static State st = RUN;

volatile int16_t encDelta=0;
static inline void encISR_A(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a==b)?+1:-1; }
static inline void encISR_B(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a!=b)?+1:-1; }
static inline int16_t takeEnc(){ noInterrupts(); int16_t d=encDelta; encDelta=0; interrupts(); return d; }
static bool btnFalling(DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ b.st=n; b.t=millis(); return n==LOW; } return false; }
static bool btnRising (DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ uint8_t was=b.st; b.st=n; b.t=millis(); return (was==LOW && n==HIGH);} return false; }
static bool btnDown   (DebBtn& b){ return b.st==LOW; }

// ======================== EEPROM (минимум) ===================================
struct GlobalHdr{
  uint8_t  magic, version;
  uint16_t calXmin, calXmax, calYmin, calYmax;
  uint8_t  lastPair;   // выбранная CC-пара
  bool     notesMode;  // текущий режим
  uint8_t  noteMin, noteMax;
  uint16_t crc;
} G;

static uint16_t crc16(const uint8_t* p, size_t n){
  uint16_t c=0xFFFF; for(size_t i=0;i<n;i++){ c^=(uint16_t)p[i]<<8; for(uint8_t b=0;b<8;b++) c=(c&0x8000)?(c<<1)^0x1021:(c<<1);} return c;
}
static void eewrite(int a,const void* s,size_t n){ const uint8_t* p=(const uint8_t*)s; for(size_t i=0;i<n;i++) EEPROM.update(a+i,p[i]); }
static bool eeread (int a,void* d,size_t n){ uint8_t* p=(uint8_t*)d; for(size_t i=0;i<n;i++) p[i]=EEPROM.read(a+i); return true; }
static const int EEADDR=0;

static void defaults(){
  G.magic='G'; G.version=1;
  G.calXmin=0; G.calXmax=0; G.calYmin=0; G.calYmax=0;
  G.lastPair=0; G.notesMode=false;
  G.noteMin=48; G.noteMax=72;
  G.crc=0; G.crc=crc16((uint8_t*)&G,sizeof(G)-2);
}
static void saveGlobal(){ G.crc=crc16((uint8_t*)&G,sizeof(G)-2); eewrite(EEADDR,&G,sizeof(G)); }
static void loadGlobal(){
  eeread(EEADDR,&G,sizeof(G));
  if(G.magic!='G'||G.version!=1||crc16((uint8_t*)&G,sizeof(G)-2)!=G.crc){
    defaults(); saveGlobal();
  }
}
static inline bool calOK(){ return (G.calXmax>G.calXmin+100) && (G.calYmax>G.calYmin+100); }

// ======================== CC PAIRS (10 штук) =================================
static const uint8_t CC_PAIRS[10][2] PROGMEM = {
  {1,11},   // Mod / Expr
  {7,10},   // Vol / Pan
  {74,71},  // Cutoff / Res
  {73,72},  // Attack / Release
  {91,93},  // Reverb / Chorus
  {4,2},    // Foot / Breath
  {5,65},   // PortaTime / PortaOn
  {70,74},  // Var / Cutoff
  {92,95},  // Trem / Phaser
  {94,93}   // Detune / Chorus
};
static uint8_t pairX(uint8_t i){ return pgm_read_byte(&CC_PAIRS[i][0]); }
static uint8_t pairY(uint8_t i){ return pgm_read_byte(&CC_PAIRS[i][1]); }

static const char CCN_0[] PROGMEM="Mod/Expr";
static const char CCN_1[] PROGMEM="Vol/Pan";
static const char CCN_2[] PROGMEM="Cut/Res";
static const char CCN_3[] PROGMEM="Atk/Rel";
static const char CCN_4[] PROGMEM="Rev/Cho";
static const char CCN_5[] PROGMEM="Foot/Br";
static const char CCN_6[] PROGMEM="Port/On";
static const char CCN_7[] PROGMEM="Var/Cut";
static const char CCN_8[] PROGMEM="Trm/Phs";
static const char CCN_9[] PROGMEM="Det/Cho";
static const char* const CCNAMES[] PROGMEM = {CCN_0,CCN_1,CCN_2,CCN_3,CCN_4,CCN_5,CCN_6,CCN_7,CCN_8,CCN_9};

static void nameOfPair(uint8_t idx, char* out, uint8_t n){
  strncpy_P(out, (PGM_P)pgm_read_word(&(CCNAMES[idx%10])), n-1); out[n-1]='\0';
}

// ======================== SCALES (3 шт. для LITE) ============================
static const int8_t S_MAJOR[]={0,2,4,5,7,9,11};
static const int8_t S_MINOR[]={0,2,3,5,7,8,10};
static const int8_t S_PENTA[]={0,2,4,7,9};
enum Scale:uint8_t{SC_MAJOR,SC_MINOR,SC_PENTA,SC__N};
static const uint8_t SC_LEN[SC__N]={7,7,5};
static const int8_t* const SC_STEPS[SC__N]={S_MAJOR,S_MINOR,S_PENTA};
static uint8_t scaleIdx=SC_MAJOR;

// ======================== HELPERS (TOUCH) ====================================
static inline void setAllHiZ(){ pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); }
static inline void prepReadX(){ pinMode(PIN_XP,OUTPUT); digitalWrite(PIN_XP,HIGH); pinMode(PIN_XM,OUTPUT); digitalWrite(PIN_XM,LOW); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); delayMicroseconds(25); }
static inline void prepReadY(){ pinMode(PIN_YP,OUTPUT); digitalWrite(PIN_YP,HIGH); pinMode(PIN_YM,OUTPUT); digitalWrite(PIN_YM,LOW); pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); delayMicroseconds(25); }

static inline uint16_t readAxisAvg(bool axisX){
  uint32_t acc=0;
  if(axisX){ for(uint8_t i=0;i<SAMPLES;i++){ prepReadX(); (void)analogRead(PIN_YP); acc+=analogRead(PIN_YP);} }
  else     { for(uint8_t i=0;i<SAMPLES;i++){ prepReadY(); (void)analogRead(PIN_XP); acc+=analogRead(PIN_XP);} }
  return (uint16_t)(acc/SAMPLES);
}
static inline uint16_t pullProbeX(){ prepReadX(); uint16_t b=analogRead(PIN_YP); pinMode(PIN_YP,INPUT); digitalWrite(PIN_YP,HIGH); delayMicroseconds(25); uint16_t p=analogRead(PIN_YP); digitalWrite(PIN_YP,LOW); return (p>b)?(p-b):(b-p); }
static inline uint16_t pullProbeY(){ prepReadY(); uint16_t b=analogRead(PIN_XP); pinMode(PIN_XP,INPUT); digitalWrite(PIN_XP,HIGH); delayMicroseconds(25); uint16_t p=analogRead(PIN_XP); digitalWrite(PIN_XP,LOW); return (p>b)?(p-b):(b-p); }

static inline bool touching(uint16_t rx,uint16_t ry){
  if(rx<=TOUCH_NOISE || rx>=ADC_MAX-TOUCH_NOISE) return false;
  if(ry<=TOUCH_NOISE || ry>=ADC_MAX-TOUCH_NOISE) return false;
  if(pullProbeX()>PULL_DELTA) return false;
  if(pullProbeY()>PULL_DELTA) return false;
  return true;
}

static inline uint8_t map127(uint16_t raw, uint16_t mn, uint16_t mx, bool inv){
  if(mx<=mn+1) return inv?0:127;
  if(raw<mn) raw=mn;
  if(raw>mx) raw=mx;
  uint16_t span = mx - mn;
  uint16_t pos  = raw - mn;
  uint8_t v = (uint8_t)((uint32_t)pos * 127UL / span);
  return inv ? (uint8_t)(127 - v) : v;
}

// ======================== MIDI HELPERS =======================================
static inline void midiCC(uint8_t cc,uint8_t val){ DINMIDI.sendControlChange(cc,val,MIDI_CH); }
static inline void midiNoteOn(uint8_t note,uint8_t vel){ DINMIDI.sendNoteOn(note,vel,MIDI_CH); }
static inline void midiNoteOff(uint8_t note){ DINMIDI.sendNoteOff(note,0,MIDI_CH); }
static inline void startupBeep(){ midiCC(123,0); midiNoteOn(60,100); delay(100); midiNoteOff(60); }

// ======================== NOTE UTILS =========================================
static void noteName(uint8_t n, char* out, uint8_t k){
  static const char* nm[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  uint8_t pc=n%12; int8_t oc=(int8_t)n/12 - 1; snprintf(out,k,"%s%d",nm[pc],oc);
}
static uint8_t quantizeToScale(uint8_t note){
  uint8_t tonic = G.noteMin%12; const int8_t* st = SC_STEPS[scaleIdx]; uint8_t cnt=SC_LEN[scaleIdx];
  int bestN=note, bestD=127;
  for(int oc=-2; oc<=2; ++oc){
    int base = (note/12 + oc)*12 + tonic;
    for(uint8_t i=0;i<cnt;i++){
      int cand = base + st[i];
      int d = abs(cand - (int)note);
      if(d<bestD){ bestD=d; bestN=cand; }
    }
  }
  if(bestN< G.noteMin) bestN=G.noteMin;
  if(bestN> G.noteMax) bestN=G.noteMax;
  return (uint8_t)bestN;
}

// ======================== UI (U8x8 / 1602) ===================================
static void uiClear(){
#if defined(DISP_LCD1602)
  lcd.clear();
#else
  u8x8.clearDisplay();
#endif
}
static void uiBegin(){
#if defined(DISP_LCD1602)
  lcd.init(); lcd.backlight(); lcd.noCursor(); lcd.noBlink();
#else
  u8x8.begin();
  u8x8.setPowerSave(0);
  u8x8.setFont(u8x8_font_5x7_f);
#endif
}
static void uiPrint(uint8_t col,uint8_t row, const __FlashStringHelper* s){
#if defined(DISP_LCD1602)
  lcd.setCursor(col,row); lcd.print(s);
#else
  char buf[17]; strncpy_P(buf,(PGM_P)s,16); buf[16]='\0';
  u8x8.drawString(col,row,buf);
#endif
}
static void uiPrintDyn(uint8_t col,uint8_t row, const char* s){
#if defined(DISP_LCD1602)
  lcd.setCursor(col,row); lcd.print(s);
#else
  u8x8.drawString(col,row,s);
#endif
}

// Status screen
static void renderStatus(uint8_t ccxLive, uint8_t ccyLive, bool noteOn, uint8_t note, uint8_t vel){
  uiClear();
  // Row0: mode
  uiPrint(0,0, G.notesMode ? L("MIDI Ноты", "MIDI Notes") : L("MIDI CC", "MIDI CC"));
  if(G.notesMode){
    char r[16], lo[8], hi[8];
    noteName(G.noteMin,lo,sizeof(lo)); noteName(G.noteMax,hi,sizeof(hi));
    snprintf(r,sizeof(r), UI_LANG_RU ? "Диап:%s-%s" : "Range:%s-%s", lo,hi);
    uiPrintDyn(0,1,r);
    char b[16];
    if(noteOn){ char nm[8]; noteName(note,nm,sizeof(nm)); snprintf(b,sizeof(b), "%s/%3u", nm, vel); }
    else strcpy(b, "--/--");
    uiPrintDyn(0,3,b);
  }else{
    char name[12]; nameOfPair(G.lastPair,name,sizeof(name));
    char l1[16]; snprintf(l1,sizeof(l1), UI_LANG_RU ? "Пара#%u %s" : "Pair#%u %s", (unsigned)G.lastPair, name);
    uiPrintDyn(0,1,l1);
    char b[16]; snprintf(b,sizeof(b), "CC%3u/CC%3u", ccxLive, ccyLive);
    uiPrintDyn(0,3,b);
  }
  uiPrint(0,5, L("Держи ENC 5с", "Hold ENC 5s"));
}

// Calibration screens
static void renderCalWait(){
  uiClear();
  uiPrint(0,0, L("Калибр. 10с", "Calib 10s"));
  uiPrint(0,1, L("Коснитесь экрана", "Touch the screen"));
  uiPrint(0,2, L("и ведите LB->RB", "move LB->RB"));
  uiPrint(0,3, L("      RT->LT",    "     RT->LT"));
}
static void renderCalRun(uint32_t leftMs){
  uiClear();
  uiPrint(0,0, L("Калибр. идёт", "Calibrating"));
  char b[16]; snprintf(b,sizeof(b), UI_LANG_RU ? "Ост:%lus" : "Left:%lus", (unsigned long)(leftMs/1000));
  uiPrintDyn(0,2,b);
  uiPrint(0,4, L("Не отрывать", "Don't lift"));
}

// Menu (минимальный набор полей)
enum UiField:uint8_t{F_MODE,F_PAIR,F_NOTE_MIN,F_NOTE_MAX,F_SCALE,F_CALNOW,F__COUNT};
static UiField cursor=F_MODE;

static void renderMenu(){
  uiClear();
  uiPrint(0,0, L("МЕНЮ (D9/D10)", "MENU (D9/D10)"));
  // текущий пункт и значение (2 строки)
  const __FlashStringHelper* lab =
    (cursor==F_MODE)     ? L("Режим", "Mode") :
    (cursor==F_PAIR)     ? L("Пара CC", "CC Pair") :
    (cursor==F_NOTE_MIN) ? L("Мин нота", "Note Min") :
    (cursor==F_NOTE_MAX) ? L("Макс нота", "Note Max") :
    (cursor==F_SCALE)    ? L("Лад", "Scale") :
    (cursor==F_CALNOW)   ? L("Калибровка", "Calibrate") : L("", "");
  uiPrint(0,2, lab);

  char val[16];
  if(cursor==F_MODE) snprintf(val,sizeof(val), G.notesMode ? (UI_LANG_RU?"НОТЫ":"NOTES") : "CC");
  else if(cursor==F_PAIR){ char n[12]; nameOfPair(G.lastPair,n,sizeof(n)); snprintf(val,sizeof(val), "#%u %s", (unsigned)G.lastPair, n); }
  else if(cursor==F_NOTE_MIN){ char nm[8]; noteName(G.noteMin,nm,sizeof(nm)); snprintf(val,sizeof(val), "%u(%s)", G.noteMin, nm); }
  else if(cursor==F_NOTE_MAX){ char nm[8]; noteName(G.noteMax,nm,sizeof(nm)); snprintf(val,sizeof(val), "%u(%s)", G.noteMax, nm); }
  else if(cursor==F_SCALE){
    const char* sc = (scaleIdx==SC_MAJOR)? "Major" : (scaleIdx==SC_MINOR? "Minor":"Pent");
    snprintf(val,sizeof(val), "%s", sc);
  } else if(cursor==F_CALNOW){
    snprintf(val,sizeof(val), calOK()? (UI_LANG_RU?"Есть":"OK") : (UI_LANG_RU?"Нужна":"Need"));
  } else val[0]=0;
  uiPrintDyn(0,3,val);
  uiPrint(0,6, L("ENC:изм  D9:->", "ENC:chg D9:->"));
  uiPrint(0,7, L("Долгий ENC:выход", "Hold ENC:exit"));
}

// ======================== CALIBRATION RUNTIME ================================
static uint16_t calXmin,calXmax,calYmin,calYmax;
static uint32_t calEndTs=0;

static void startCalWait(){ st=CAL_WAIT; renderCalWait(); }
static void startCalRun(){ st=CAL_RUN; calXmin=0xFFFF; calYmin=0xFFFF; calXmax=0; calYmax=0; calEndTs=millis()+10000UL; renderCalRun(10000UL); }
static void finishCal(){
  if(calXmax<=calXmin+10 || calYmax<=calYmin+10){
    // fallback
    G.calXmin=100; G.calXmax=900; G.calYmin=100; G.calYmax=900;
  }else{
    G.calXmin=calXmin; G.calXmax=calXmax; G.calYmin=calYmin; G.calYmax=calYmax;
  }
  saveGlobal();
  st=RUN;
}

// ======================== SETUP =============================================
void setup(){
  pinMode(ENC_A,INPUT_PULLUP); pinMode(ENC_B,INPUT_PULLUP); pinMode(ENC_BTN,INPUT_PULLUP);
  pinMode(BTN_NEXT,INPUT_PULLUP); pinMode(BTN_SAVE,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR_A, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR_B, CHANGE);

  uiBegin();

  Serial.begin(31250);
  DINMIDI.begin(MIDI_CHANNEL_OMNI); DINMIDI.turnThruOff();
  startupBeep();

  loadGlobal();
  if(!calOK()) st=CAL_WAIT; else st=RUN;

  setAllHiZ();
  renderStatus(0,0,false,0,0);
}

// ======================== LOOP ==============================================
void loop(){
  // ENC press/hold
  if (btnFalling(bEnc)) { encPressTs=millis(); encRotDuringHold=false; }
  if (btnRising(bEnc)){
    uint32_t dt=millis()-encPressTs;
    if (dt>LONG_PRESS_MS && !encRotDuringHold){
      if (st==RUN){ st=MENU; renderMenu(); }
      else if (st==MENU){ saveGlobal(); st=RUN; renderStatus(0,0,false,0,0); }
    }
  }
  // Menu navigation buttons
  if (st==MENU){
    if (btnFalling(bNext)){ cursor=(UiField)((cursor+1)%F__COUNT); renderMenu(); }
    if (btnFalling(bSave)){ saveGlobal(); renderMenu(); }
  }

  // Encoder delta
  int16_t d = takeEnc();
  if (d){
    if (digitalRead(ENC_BTN)==LOW) encRotDuringHold=true;
    if (st==MENU){
      switch(cursor){
        case F_MODE:      G.notesMode = (d>0); break;
        case F_PAIR:      { int v=(int)G.lastPair + (d>0?+1:-1); if(v<0)v=9; if(v>9)v=0; G.lastPair=(uint8_t)v; } break;
        case F_NOTE_MIN:  { int step = (digitalRead(ENC_BTN)==LOW)?12:1; int v = (int)G.noteMin + (d>0?step:-step); if(v<0)v=0; if(v>G.noteMax)v=G.noteMax; G.noteMin=(uint8_t)v; } break;
        case F_NOTE_MAX:  { int step = (digitalRead(ENC_BTN)==LOW)?12:1; int v = (int)G.noteMax + (d>0?step:-step); if(v>127)v=127; if(v<G.noteMin)v=G.noteMin; G.noteMax=(uint8_t)v; } break;
        case F_SCALE:     { int v=(int)scaleIdx + (d>0?+1:-1); if(v<0)v=SC__N-1; if(v>=SC__N)v=0; scaleIdx=(uint8_t)v; } break;
        case F_CALNOW:    /* нажми коротко ENC вне касания */ break;
        default: break;
      }
      renderMenu();
    } else if (st==RUN){
      if (G.notesMode){
        int step = (digitalRead(ENC_BTN)==LOW)?12:1;
        int shift = (d>0?step:-step);
        // быстрый транспоз диапазона
        int nmin = (int)G.noteMin + shift;
        int nmax = (int)G.noteMax + shift;
        if(nmin<0){ nmax -= nmin; nmin=0; }
        if(nmax>127){ nmin -= (nmax-127); nmax=127; if(nmin<0)nmin=0; }
        G.noteMin=(uint8_t)nmin; G.noteMax=(uint8_t)nmax;
      } else {
        int v=(int)G.lastPair + (d>0?+1:-1); if(v<0)v=9; if(v>9)v=0; G.lastPair=(uint8_t)v;
      }
    }
  }

  // Calibrate requests (ENC short in CAL, или пункт меню F_CALNOW)
  if (st==MENU && cursor==F_CALNOW && btnRising(bEnc)) startCalWait();

  // Touch read
  uint16_t rx = readAxisAvg(true);
  uint16_t ry = readAxisAvg(false);

  if (st==CAL_WAIT){
    if (touching(rx,ry)) startCalRun();
    return;
  }
  if (st==CAL_RUN){
    if (touching(rx,ry)){
      if (rx<calXmin) calXmin=rx; if (rx>calXmax) calXmax=rx;
      if (ry<calYmin) calYmin=ry; if (ry>calYmax) calYmax=ry;
    }
    uint32_t left = (calEndTs>millis()) ? (calEndTs-millis()) : 0;
    renderCalRun(left);
    if (millis()>=calEndTs){ finishCal(); renderStatus(0,0,false,0,0); }
    return;
  }

  // RUN: map to CC / Notes
  static uint8_t lastCCx=255, lastCCy=255;
  static uint8_t curNote=255;

  bool touch = touching(rx,ry);
  uint8_t x127 = map127(rx, G.calXmin, G.calXmax, false);
  uint8_t y127 = map127(ry, G.calYmin, G.calYmax, true); // по умолчанию инверт Y

  if (!G.notesMode){
    if (touch){
      uint8_t ccx = x127, ccy = y127;
      if(lastCCx==255 || (uint8_t)abs((int)ccx-(int)lastCCx)>=1){ midiCC(pairX(G.lastPair), ccx); lastCCx=ccx; }
      if(lastCCy==255 || (uint8_t)abs((int)ccy-(int)lastCCy)>=1){ midiCC(pairY(G.lastPair), ccy); lastCCy=ccy; }
      renderStatus(lastCCx,lastCCy,false,0,0);
    }else{
      renderStatus(lastCCx,lastCCy,false,0,0);
    }
  }else{
    if (touch){
      uint8_t rootLin = G.noteMin + (uint8_t)((uint16_t)x127 * (G.noteMax - G.noteMin) / 127U);
      uint8_t root    = quantizeToScale(rootLin);
      uint8_t vel     = y127<1?1:y127;
      if (curNote==255){ midiNoteOn(root,vel); curNote=root; }
      else if (root!=curNote){ midiNoteOff(curNote); midiNoteOn(root,vel); curNote=root; }
      else { DINMIDI.sendAfterTouch(vel, MIDI_CH); } // канал. афтертач как «давление»
      renderStatus(0,0,true,curNote,vel);
    }else{
      if (curNote!=255){ midiNoteOff(curNote); curNote=255; }
      renderStatus(0,0,false,0,0);
    }
  }
}

// ======================== END =================================================