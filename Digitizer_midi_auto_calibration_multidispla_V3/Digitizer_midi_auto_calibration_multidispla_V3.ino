// File: src/touch_to_midi_nano_multidisplay_lang_toggle.ino
//
// Touch→MIDI (Arduino Nano, DIN-MIDI). Глобальный RU/EN тогл для OLED/1602, SysEx, EEPROM, меню.
//
// ── ВЫБЕРИ РОВНО ОДИН ДИСПЛЕЙ ────────────────────────────────────────────────
//#define DISP_SSD1306
//#define DISP_SSD1309
//#define DISP_SH1106
//#define DISP_LCD1602   // HD44780 16x2 I2C (LiquidCrystal_I2C)
#if !defined(DISP_SSD1306) && !defined(DISP_SSD1309) && !defined(DISP_SH1106) && !defined(DISP_LCD1602)
  #define DISP_SSD1306
#endif

// ── ЕДИНЫЙ ТОГЛ ЯЗЫКА UI ─────────────────────────────────────────────────────
// Раскомментируй для русского интерфейса (OLED + 1602):
//#define UI_LANG_RU

// ── ПАРАМЕТРЫ OLED / 1602 (можно переопределить сверху) ─────────────────────
#ifndef OLED_I2C_ADDR
  #define OLED_I2C_ADDR 0x3C
#endif
#ifndef OLED_ROT
  #define OLED_ROT U8G2_R0
#endif

#if defined(UI_LANG_RU)
// Шрифты с кириллицей + UTF-8
  #ifndef OLED_FONT_SMALL
    #define OLED_FONT_SMALL u8g2_font_6x12_t_cyrillic
  #endif
  #ifndef OLED_FONT_BIG
    #define OLED_FONT_BIG   u8g2_font_unifont_t_cyrillic
  #endif
  #define OLED_USE_UTF8 1
#else
  #ifndef OLED_FONT_SMALL
    #define OLED_FONT_SMALL u8g2_font_6x10_tf
  #endif
  #ifndef OLED_FONT_BIG
    #define OLED_FONT_BIG   u8g2_font_fub20_tr
  #endif
  #define OLED_USE_UTF8 0
#endif

#ifndef LCD_ADDR
  #define LCD_ADDR 0x27
#endif
#ifndef LCD_COLS
  #define LCD_COLS 16
#endif
#ifndef LCD_ROWS
  #define LCD_ROWS 2
#endif

// ── MIDI ПАРАМЕТРЫ (канал/идентификация) ─────────────────────────────────────
#ifndef MIDI_CHANNEL
  #define MIDI_CHANNEL 1          // 1..16
#endif
#ifndef MIDI_DEVICE_ID
  #define MIDI_DEVICE_ID 0x10     // 0x00..0x7F
#endif
#ifndef MIDI_MFR_ID
  #define MIDI_MFR_ID 0x7D        // Non-commercial
#endif
#ifndef MIDI_FAMILY_LSB
  #define MIDI_FAMILY_LSB 0x00
#endif
#ifndef MIDI_FAMILY_MSB
  #define MIDI_FAMILY_MSB 0x01
#endif
#ifndef MIDI_MODEL_LSB
  #define MIDI_MODEL_LSB  0x00
#endif
#ifndef MIDI_MODEL_MSB
  #define MIDI_MODEL_MSB  0x01
#endif
#ifndef MIDI_VER_0
  #define MIDI_VER_0 0x00
#endif
#ifndef MIDI_VER_1
  #define MIDI_VER_1 0x00
#endif
#ifndef MIDI_VER_2
  #define MIDI_VER_2 0x01
#endif
#ifndef MIDI_VER_3
  #define MIDI_VER_3 0x00
#endif

// ── Опциональные пункты LED в меню (скрыты по умолчанию) ─────────────────────
//#define LED_FEATURE 1
//#define LED 1
#if defined(LED) || defined(LED_FEATURE)
  #define LED_MENU_ENABLED 1
#else
  #define LED_MENU_ENABLED 0
#endif

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <MIDI.h>

#if defined(DISP_LCD1602)
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
#else
  #include <U8g2lib.h>
  #if defined(DISP_SSD1306)
    U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(OLED_ROT, U8X8_PIN_NONE);
  #elif defined(DISP_SSD1309)
    U8G2_SSD1309_128X64_NONAME2_1_HW_I2C  u8g2(OLED_ROT, U8X8_PIN_NONE);
  #elif defined(DISP_SH1106)
    U8G2_SH1106_128X64_NONAME_1_HW_I2C    u8g2(OLED_ROT, U8X8_PIN_NONE);
  #endif
#endif

// ── Макрос языка для строк ───────────────────────────────────────────────────
#if defined(UI_LANG_RU)
  #define L(ru,en) F(ru)
#else
  #define L(ru,en) F(en)
#endif

// ── Пины ─────────────────────────────────────────────────────────────────────
static const uint8_t PIN_XP = A0;  // Touch X+
static const uint8_t PIN_YP = A1;  // Touch Y+
static const uint8_t PIN_XM = 5;   // Touch X-
static const uint8_t PIN_YM = 6;   // Touch Y-
static const uint8_t ENC_A   = 2;
static const uint8_t ENC_B   = 3;
static const uint8_t ENC_BTN = 8;
static const uint8_t BTN_NEXT= 9;
static const uint8_t BTN_SAVE= 10;
static const uint8_t BTN_CAL = 7;  // hold+touch = Chord; short/long = Cal/InvertY
static const uint8_t BTN_ARP = 4;  // hold+touch = Arp

// ── MIDI (DIN: TX=D1, 31250) ────────────────────────────────────────────────
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, DINMIDI);
static const uint8_t  MIDI_CH = MIDI_CHANNEL;
static const uint8_t  CC_DELTA = 1;

// ── Touch/ADC ────────────────────────────────────────────────────────────────
static const uint16_t ADC_MAX = 1023;
static const uint8_t  SAMPLES_PER_AXIS = 6;
static const uint16_t TOUCH_NOISE = ADC_MAX / 70;
static const uint16_t PULL_DELTA  = ADC_MAX / 8;
static const uint32_t CAL_WINDOW_MS = 10000;
static const uint32_t LOOP_MS = 2;

// ── Прочее ───────────────────────────────────────────────────────────────────
static bool invertX=false, invertY=false;
enum State : uint8_t { CAL_WAIT_TOUCH, CAL_RUNNING, RUN };
static State state_ = RUN;
static bool  menuActive=false;
static const uint8_t  DEFAULT_CC_X=74, DEFAULT_CC_Y=71;
static const uint8_t  DEFAULT_NOTE_MIN=48, DEFAULT_NOTE_MAX=72;
static const bool     DEFAULT_NOTES_MODE=false;
static const uint16_t DEFAULT_TEMPO_BPM=120;
static const bool     DEFAULT_CLOCK_IN=false;
static const uint32_t LONG_PRESS_MS = 5000;

// ── Скейлы/Арп ───────────────────────────────────────────────────────────────
struct ScaleDef{ const int8_t* steps; uint8_t count; const char* name; };
static const int8_t S_MAJOR[]={0,2,4,5,7,9,11}, S_MINOR[]={0,2,3,5,7,8,10};
static const int8_t S_HMAJ[]={0,2,4,5,7,8,11}, S_HMIN[]={0,2,3,5,7,8,11};
static const int8_t S_PHRY[]={0,1,3,5,7,8,10}, S_LYDI[]={0,2,4,6,7,9,11};
static const int8_t S_MIXO[]={0,2,4,5,7,9,10}, S_DORI[]={0,2,3,5,7,9,10};
static const int8_t S_PENTA[]={0,2,4,7,9};
enum ScaleIdx:uint8_t{SC_MAJOR,SC_MINOR,SC_HMAJ,SC_HMIN,SC_PHRY,SC_LYDI,SC_MIXO,SC_DORI,SC_PENTA,SC__COUNT};
static const ScaleDef SCALES[SC__COUNT]={
  {S_MAJOR,7,"Major"},{S_MINOR,7,"Minor"},{S_HMAJ,7,"HarmMajor"},{S_HMIN,7,"HarmMinor"},
  {S_PHRY,7,"Phrygian"},{S_LYDI,7,"Lydian"},{S_MIXO,7,"Mixolydian"},{S_DORI,7,"Dorian"},{S_PENTA,5,"Pentatonic"}
};
enum ArpStyle:uint8_t{ARP_UP,ARP_DOWN,ARP_PINGPONG,ARP_UPDOWN,ARP_OUTSIDE_IN,ARP_RANDOM,ARP_RANDOM_WALK,ARP__COUNT};
static const char* ARP_STYLE_NAMES[ARP__COUNT]={"Up","Down","Ping-Pong","Up-Down","Outside-In","Random","Random-Walk"};
struct DivDef{ uint8_t denom; const char* label; };
enum DivIdx:uint8_t{DIV_1_4,DIV_1_8,DIV_1_16,DIV_1_3,DIV_1_6,DIV_1_7,DIV_1_9,DIV__COUNT};
static const DivDef DIVS[DIV__COUNT]={{4,"1/4"},{8,"1/8"},{16,"1/16"},{3,"1/3"},{6,"1/6"},{7,"1/7"},{9,"1/9"}};

// ── EEPROM ───────────────────────────────────────────────────────────────────
#define PERF_SLOTS 10
#define CC_PAIR_COUNT 10
#define LABEL_LEN 12
struct GlobalHdr{
  uint8_t magic, version;
  uint16_t calXmin, calXmax, calYmin, calYmax;
  uint8_t lastPerf;
  uint16_t crc;
};
struct Perf{
  uint8_t magic, version;
  bool notesMode;
  uint8_t ccX, ccY;
  uint8_t noteMin, noteMax;
  bool invY;
  uint8_t scaleIndex;
  uint16_t tempoBPM;
  bool clockIn;
  uint8_t arpStyle, divIndex, chordLen, ccPairIdx;
  bool ledEnable; uint8_t ledBrightness;
  uint16_t crc;
};
struct CCPairsBlob{
  uint8_t magic, version;
  uint8_t pairs[CC_PAIR_COUNT][2];
  char labels[CC_PAIR_COUNT][LABEL_LEN+1];
  uint16_t crc;
};

static const int EEPROM_ADDR=0;
static GlobalHdr G;
static Perf S,TMP;
static CCPairsBlob CCStore;
static uint8_t CCPairs[CC_PAIR_COUNT][2];
static char Labels[CC_PAIR_COUNT][LABEL_LEN+1];

static uint16_t crc16_ccitt(const uint8_t* p,size_t n){ uint16_t crc=0xFFFF; for(size_t i=0;i<n;i++){ crc^=(uint16_t)p[i]<<8; for(uint8_t b=0;b<8;b++) crc=(crc&0x8000)?(crc<<1)^0x1021:(crc<<1);} return crc; }
static void eepromWrite(int addr,const void* src,size_t n){ const uint8_t* p=(const uint8_t*)src; for(size_t i=0;i<n;i++) EEPROM.update(addr+i,p[i]); }
static bool checkCRC(const void* obj,size_t n){ const uint8_t* p=(const uint8_t*)obj; uint16_t want; memcpy(&want,p+n-2,2); uint16_t got=crc16_ccitt(p,n-2); return want==got; }
static int perfBase(){ return EEPROM_ADDR + (int)sizeof(GlobalHdr); }
static int perfAddr(uint8_t slot){ return perfBase()+ slot*(int)sizeof(Perf); }
static int pairsAddr(){ return perfBase() + PERF_SLOTS*(int)sizeof(Perf); }
static void defaultsGlobal(){ G.magic='G'; G.version=2; G.calXmin=G.calYmin=0; G.calXmax=G.calYmax=0; G.lastPerf=0; G.crc=0; G.crc=crc16_ccitt((uint8_t*)&G,sizeof(G)-2); }
static void defaultsPerfFixed(Perf& P){
  P.magic='P'; P.version=3; P.notesMode=DEFAULT_NOTES_MODE; P.ccX=DEFAULT_CC_X; P.ccY=DEFAULT_CC_Y;
  P.noteMin=DEFAULT_NOTE_MIN; P.noteMax=DEFAULT_NOTE_MAX; P.invY=false; P.scaleIndex=SC_MAJOR;
  P.tempoBPM=DEFAULT_TEMPO_BPM; P.clockIn=DEFAULT_CLOCK_IN;
  P.arpStyle=ARP_UP; P.divIndex=DIV_1_8; P.chordLen=4; P.ccPairIdx=0;
  P.ledEnable=false; P.ledBrightness=30; P.crc=0; P.crc=crc16_ccitt((uint8_t*)&P,sizeof(P)-2);
}
static void defaultsPairs(){ const uint8_t def[CC_PAIR_COUNT][2]={{1,11},{7,10},{74,71},{73,72},{75,70},{91,93},{4,2},{5,65},{92,95},{94,93}}; const char* lbl[CC_PAIR_COUNT]={"Mod/Expr","Vol/Pan","Cutoff/Res","Atk/Rel","Sound6/Var","Rev/Cho","Foot/Breath","PortaT/On","Trem/Phas","Detune/Cho"}; for(uint8_t i=0;i<CC_PAIR_COUNT;i++){ CCPairs[i][0]=def[i][0]; CCPairs[i][1]=def[i][1]; strncpy(Labels[i],lbl[i],LABEL_LEN); Labels[i][LABEL_LEN]='\0'; } }
static void saveGlobal(){ G.crc=crc16_ccitt((uint8_t*)&G,sizeof(G)-2); eepromWrite(EEPROM_ADDR,&G,sizeof(G)); }
static bool loadGlobal(){ EEPROM.get(EEPROM_ADDR,G); if(G.magic!='G'||G.version!=2||!checkCRC(&G,sizeof(G))){ defaultsGlobal(); saveGlobal(); return false; } return true; }
static void savePerf(uint8_t slot,const Perf& P){ Perf t=P; t.crc=0; t.crc=crc16_ccitt((uint8_t*)&t,sizeof(t)-2); eepromWrite(perfAddr(slot),&t,sizeof(t)); }
static bool loadPerf(uint8_t slot,Perf& out){ EEPROM.get(perfAddr(slot),out); if(out.magic!='P'||out.version!=3||!checkCRC(&out,sizeof(out))) return false; return true; }
static void pairsSave(){ CCStore.magic='C'; CCStore.version=2; for(uint8_t i=0;i<CC_PAIR_COUNT;i++){ CCStore.pairs[i][0]=CCPairs[i][0]; CCStore.pairs[i][1]=CCPairs[i][1]; strncpy(CCStore.labels[i],Labels[i],LABEL_LEN); CCStore.labels[i][LABEL_LEN]='\0'; } CCStore.crc=0; CCStore.crc=crc16_ccitt((uint8_t*)&CCStore,sizeof(CCStore)-2); eepromWrite(pairsAddr(),&CCStore,sizeof(CCStore)); }
static bool pairsLoad(){ EEPROM.get(pairsAddr(),CCStore); if(CCStore.magic!='C'||CCStore.version!=2||!checkCRC(&CCStore,sizeof(CCStore))) return false; for(uint8_t i=0;i<CC_PAIR_COUNT;i++){ CCPairs[i][0]=CCStore.pairs[i][0]; CCPairs[i][1]=CCStore.pairs[i][1]; strncpy(Labels[i],CCStore.labels[i],LABEL_LEN); Labels[i][LABEL_LEN]='\0'; } return true; }

// ── CC short names (EN, оставляем как есть) ──────────────────────────────────
static const __FlashStringHelper* ccNameShort(uint8_t cc){
  switch(cc){
    case 0:return F("BankSel"); case 1:return F("ModWheel"); case 2:return F("Breath");
    case 4:return F("Foot"); case 5:return F("PortaT"); case 6:return F("DataEnt");
    case 7:return F("Volume"); case 10:return F("Pan"); case 11:return F("Expr");
    case 71:return F("Reson"); case 73:return F("Attack"); case 74:return F("Cutoff");
    case 70:return F("Var"); case 72:return F("Rel"); case 91:return F("Reverb"); case 93:return F("Chorus");
    default: return F("Ctrl");
  }
}
static void copyFlashStr(const __FlashStringHelper* f,char* buf,size_t n){ if(!f||!n){ if(n) buf[0]=0; return; } strncpy_P(buf,(PGM_P)f,n-1); buf[n-1]=0; }

// ── Touch low-level ─────────────────────────────────────────────────────────
static inline void setAllHiZ(){ pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); }
static inline void prepReadX(){ pinMode(PIN_XP,OUTPUT); digitalWrite(PIN_XP,HIGH); pinMode(PIN_XM,OUTPUT); digitalWrite(PIN_XM,LOW); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); delayMicroseconds(30); }
static inline void prepReadY(){ pinMode(PIN_YP,OUTPUT); digitalWrite(PIN_YP,HIGH); pinMode(PIN_YM,OUTPUT); digitalWrite(PIN_YM,LOW); pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); delayMicroseconds(30); }
static inline uint16_t readAxisAvg(bool axisX){ uint32_t acc=0; if(axisX){ for(uint8_t i=0;i<SAMPLES_PER_AXIS;i++){ prepReadX(); (void)analogRead(PIN_YP); delayMicroseconds(8); acc+=analogRead(PIN_YP);} } else { for(uint8_t i=0;i<SAMPLES_PER_AXIS;i++){ prepReadY(); (void)analogRead(PIN_XP); delayMicroseconds(8); acc+=analogRead(PIN_XP);} } return (uint16_t)(acc/SAMPLES_PER_AXIS); }
static inline bool midRange(uint16_t s){ return (s>TOUCH_NOISE)&&(s<(ADC_MAX-TOUCH_NOISE)); }
static inline uint16_t pullProbeX(){ prepReadX(); (void)analogRead(PIN_YP); delayMicroseconds(8); uint16_t base=analogRead(PIN_YP); pinMode(PIN_YP,INPUT); digitalWrite(PIN_YP,HIGH); delayMicroseconds(30); (void)analogRead(PIN_YP); delayMicroseconds(8); uint16_t pulled=analogRead(PIN_YP); digitalWrite(PIN_YP,LOW); return (pulled>base)?(pulled-base):(base-pulled); }
static inline uint16_t pullProbeY(){ prepReadY(); (void)analogRead(PIN_XP); delayMicroseconds(8); uint16_t base=analogRead(PIN_XP); pinMode(PIN_XP,INPUT); digitalWrite(PIN_XP,HIGH); delayMicroseconds(30); (void)analogRead(PIN_XP); delayMicroseconds(8); uint16_t pulled=analogRead(PIN_XP); digitalWrite(PIN_XP,LOW); return (pulled>base)?(pulled-base):(base-pulled); }
static inline uint16_t clampU16(uint16_t v,uint16_t lo,uint16_t hi){ if(v<lo)return lo; if(v>hi)return hi; return v; }
static inline float norm01(uint16_t raw,uint16_t mn,uint16_t mx,bool inv){ if(mx<=mn)return 0.f; raw=clampU16(raw,mn,mx); float v=(float)(raw-mn)/(float)(mx-mn); return inv?(1.f-v):v; }
static inline uint8_t toCC(float n01){ if(n01<=0.f)return 0; if(n01>=1.f)return 127; return (uint8_t)(n01*127.f+0.5f); }

// ── MIDI helpers ─────────────────────────────────────────────────────────────
static inline void midiCC(uint8_t cc,uint8_t val){ DINMIDI.sendControlChange(cc,val,MIDI_CH); }
static inline void midiNoteOn(uint8_t note,uint8_t vel){ DINMIDI.sendNoteOn(note,vel,MIDI_CH); }
static inline void midiNoteOff(uint8_t note){ DINMIDI.sendNoteOff(note,0,MIDI_CH); }
static inline void midiChanPressure(uint8_t val){ DINMIDI.sendAfterTouch(val,MIDI_CH); }
static inline void startupBeep(){ midiCC(123,0); midiNoteOn(60,100); delay(120); midiNoteOff(60); }

// ── UI общие переменные ─────────────────────────────────────────────────────
enum UiField:uint8_t{
  F_MODE, F_CCX, F_CCY,
  F_CCPAIRSEL, F_CCPAIR_X, F_CCPAIR_Y, F_CCPAIR_LABEL, F_CCPAIR_SAVE,
#if LED_MENU_ENABLED
  F_LED_ENABLE, F_LED_BRIGHT,
#endif
  F_NMIN, F_NMAX, F_SCALE, F_TEMPO, F_CLKIN,
  F_ARPSTYLE, F_DIV, F_CHLEN, F_INVY, F_CALNOW,
  F_PERFSLOT, F_PERFLOAD, F_PERFSAVE,
  F__COUNT
};
static UiField cursor=F_MODE;
static uint32_t lastStatusTs=0;
static uint8_t  liveCC_X=0, liveCC_Y=0;
static bool     liveNoteOn=false; static uint8_t liveNote=0, liveVel=0;

// ── Калибровка runtime ───────────────────────────────────────────────────────
static uint32_t calDeadline=0, lastLoopTs=0;
static uint16_t calXminRt,calXmaxRt,calYminRt,calYmaxRt;
static bool validSpan(uint16_t mn,uint16_t mx){ return (mx>mn) && ((mx-mn)>= (ADC_MAX/10)); }

// ── Энкодер/кнопки ───────────────────────────────────────────────────────────
volatile int16_t encDelta=0;
static inline void encISR_A(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a==b)?+1:-1; }
static inline void encISR_B(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a!=b)?+1:-1; }
static inline int16_t takeEncDelta(){ noInterrupts(); int16_t d=encDelta; encDelta=0; interrupts(); return d; }
struct DebBtn{ uint8_t pin, st; uint32_t t; };
static DebBtn bEnc{ENC_BTN,HIGH,0}, bNext{BTN_NEXT,HIGH,0}, bSave{BTN_SAVE,HIGH,0}, bCal{BTN_CAL,HIGH,0}, bArp{BTN_ARP,HIGH,0};
static bool btnFalling(DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ b.st=n; b.t=millis(); return n==LOW; } return false; }
static bool btnRising (DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ uint8_t was=b.st; b.st=n; b.t=millis(); return (was==LOW && n==HIGH);} return false; }
static bool btnIsDown (DebBtn& b){ return b.st==LOW; }
static uint32_t encPressTs=0, calPressTs=0;
static bool encRotDuringHold=false;

// ── OLED рендер ─────────────────────────────────────────────────────────────
#if !defined(DISP_LCD1602)
static void drawRow(uint8_t y,const char* label,const char* value,bool sel){
  if(sel){ u8g2.drawBox(0,y-10,128,12); u8g2.setDrawColor(0); }
  u8g2.setCursor(2,y); u8g2.print(label); u8g2.setCursor(68,y); u8g2.print(value);
  if(sel){ u8g2.setDrawColor(1); }
}
static void ccShort(uint8_t cc,char* out,size_t n){ copyFlashStr(ccNameShort(cc),out,n); }

static void valueStr(char* buf,UiField f){
  switch(f){
    case F_MODE:  snprintf(buf,16,"%s", S.notesMode? (UI_LANG_RU? "НОТЫ":"NOTES") : "CC"); break;
    case F_CCX:   snprintf(buf,16,"%u", S.ccX); break;
    case F_CCY:   snprintf(buf,16,"%u", S.ccY); break;
    case F_CCPAIRSEL: snprintf(buf,16,"#%u",(unsigned)S.ccPairIdx); break;
    case F_CCPAIR_X:{ char nm[10]; ccShort(CCPairs[S.ccPairIdx][0],nm,sizeof(nm)); snprintf(buf,16,"%u %s",CCPairs[S.ccPairIdx][0],nm);} break;
    case F_CCPAIR_Y:{ char nm[10]; ccShort(CCPairs[S.ccPairIdx][1],nm,sizeof(nm)); snprintf(buf,16,"%u %s",CCPairs[S.ccPairIdx][1],nm);} break;
    case F_CCPAIR_LABEL: snprintf(buf,16,"%s",Labels[S.ccPairIdx]); break;
    case F_CCPAIR_SAVE:  snprintf(buf,16,"%s", UI_LANG_RU? "Нажать":"Press"); break;
  #if LED_MENU_ENABLED
    case F_LED_ENABLE: snprintf(buf,16,"%s", S.ledEnable? (UI_LANG_RU?"Вкл":"On") : (UI_LANG_RU?"Выкл":"Off")); break;
    case F_LED_BRIGHT: snprintf(buf,16,"%u",(unsigned)S.ledBrightness); break;
  #endif
    case F_NMIN:  snprintf(buf,16,"%u",S.noteMin); break;
    case F_NMAX:  snprintf(buf,16,"%u",S.noteMax); break;
    case F_SCALE: snprintf(buf,16,"%s",SCALES[S.scaleIndex].name); break;
    case F_TEMPO: snprintf(buf,16, UI_LANG_RU? "%u уд/м" : "%u BPM", (unsigned)S.tempoBPM); break;
    case F_CLKIN: snprintf(buf,16,"%s", S.clockIn? (UI_LANG_RU?"Вкл":"On"):(UI_LANG_RU?"Выкл":"Off")); break;
    case F_ARPSTYLE: snprintf(buf,16,"%s",ARP_STYLE_NAMES[S.arpStyle]); break;
    case F_DIV:   snprintf(buf,16,"%s",DIVS[S.divIndex].label); break;
    case F_CHLEN: snprintf(buf,16,"%u",S.chordLen); break;
    case F_INVY:  snprintf(buf,16,"%s",invertY?(UI_LANG_RU?"Да":"Yes"):(UI_LANG_RU?"Нет":"No")); break;
    case F_CALNOW:{ bool ok=validSpan(G.calXmin,G.calXmax)&&validSpan(G.calYmin,G.calYmax); snprintf(buf,16,"%s",ok?(UI_LANG_RU?"Сохранено":"Stored"):(UI_LANG_RU?"Нужна":"Required")); } break;
    case F_PERFSLOT: snprintf(buf,16,"#%u",(unsigned)G.lastPerf); break;
    case F_PERFLOAD: snprintf(buf,16, UI_LANG_RU? "Загруз #%u" : "Load #%u", (unsigned)G.lastPerf); break;
    case F_PERFSAVE: snprintf(buf,16, UI_LANG_RU? "Сохр #%u"   : "Save #%u", (unsigned)G.lastPerf); break;
    default: snprintf(buf,16,""); break;
  }
}
static const char* labelOf(UiField f){
  switch(f){
    case F_MODE:return UI_LANG_RU? "Режим":"Mode";
    case F_CCX:return "CC X"; case F_CCY:return "CC Y";
    case F_CCPAIRSEL:return UI_LANG_RU? "Пара CC":"CC Pair";
    case F_CCPAIR_X:return UI_LANG_RU? "Пара X":"Pair X";
    case F_CCPAIR_Y:return UI_LANG_RU? "Пара Y":"Pair Y";
    case F_CCPAIR_LABEL:return UI_LANG_RU? "Метка":"Pair Label";
    case F_CCPAIR_SAVE:return UI_LANG_RU? "Сохр. пары":"Save CC Pairs";
  #if LED_MENU_ENABLED
    case F_LED_ENABLE:return UI_LANG_RU? "LED Вкл":"LED Enable";
    case F_LED_BRIGHT:return UI_LANG_RU? "LED Ярк":"LED Bright";
  #endif
    case F_NMIN:return UI_LANG_RU? "Мин нота":"NoteMin";
    case F_NMAX:return UI_LANG_RU? "Макс нота":"NoteMax";
    case F_SCALE:return UI_LANG_RU? "Лад":"Scale";
    case F_TEMPO:return UI_LANG_RU? "Темп":"Tempo";
    case F_CLKIN:return UI_LANG_RU? "Clock Вх":"Clock In";
    case F_ARPSTYLE:return UI_LANG_RU? "Арпеджио":"Arp Style";
    case F_DIV:return UI_LANG_RU? "Деление":"Division";
    case F_CHLEN:return UI_LANG_RU? "Дл. акк":"Chord Len";
    case F_INVY:return UI_LANG_RU? "Инверт Y":"InvertY";
    case F_CALNOW:return UI_LANG_RU? "Калибр.":"Calibrate";
    case F_PERFSLOT:return UI_LANG_RU? "Профиль":"Perf Slot";
    case F_PERFLOAD:return UI_LANG_RU? "Загрузить":"Load Perf";
    case F_PERFSAVE:return UI_LANG_RU? "Сохранить":"Save Perf";
    default:return "";
  }
}
static void noteName(uint8_t note,char* out,size_t n){ static const char* nm[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; uint8_t pc=note%12; int8_t oct=(int8_t)note/12 - 1; snprintf(out,n,"%s%d",nm[pc],oct); }

static void renderUIOLED(){
  char val[20], buf[34];
  u8g2.firstPage(); do{
    u8g2.setFont(OLED_FONT_SMALL);
    if (OLED_USE_UTF8) u8g2.enableUTF8Print();
    u8g2.setCursor(0,10); u8g2.print(UI_LANG_RU? F("МЕНЮ  (энкодер 5с = Сохран/Выход)") : F("MENU  (hold ENC 5s = Save&Exit)"));
    uint8_t base=(cursor/4)*4;
    for(uint8_t i=0;i<4;i++){
      UiField f=(UiField)(base+i); if(f>=F__COUNT)break;
      valueStr(val,f); drawRow(24+i*12,labelOf(f),val,f==cursor);
    }
    bool ok=validSpan(G.calXmin,G.calXmax)&&validSpan(G.calYmin,G.calYmax);
    snprintf(buf,sizeof(buf), UI_LANG_RU? "Калибр:%s  D7:Кал  D10:Сохр" : "Cal:%s  D7:Cal  D10:Save", ok?(UI_LANG_RU?"OK":"OK"):(UI_LANG_RU?"Нужно":"NEEDED"));
    u8g2.setCursor(0,64); u8g2.print(buf);
  }while(u8g2.nextPage());
}
static void renderStatusOLED(){
  uint32_t now=millis(); if(now-lastStatusTs<100) return; lastStatusTs=now;
  u8g2.firstPage(); do{
    u8g2.setFont(OLED_FONT_SMALL); if (OLED_USE_UTF8) u8g2.enableUTF8Print();
    u8g2.setCursor(0,10); u8g2.print(S.notesMode? (UI_LANG_RU? F("Режим нот"):F("MIDI Notes Mode")) : (UI_LANG_RU? F("Режим CC"):F("MIDI CC Mode")));
    if (S.notesMode){
      char lo[8],hi[8],range[28]; noteName(S.noteMin,lo,sizeof(lo)); noteName(S.noteMax,hi,sizeof(hi));
      snprintf(range,sizeof(range), UI_LANG_RU? "Диапазон: %s-%s" : "Range: %s-%s", lo,hi); u8g2.setCursor(0,22); u8g2.print(range);
    } else {
      if (Labels[S.ccPairIdx][0]){ char line[32]; snprintf(line,sizeof(line), UI_LANG_RU? "Пара #%u %s" : "Pair #%u %s",(unsigned)S.ccPairIdx,Labels[S.ccPairIdx]); u8g2.setCursor(0,22); u8g2.print(line); }
      else { char xnm[10],ynm[10],line[32]; copyFlashStr(ccNameShort(CCPairs[S.ccPairIdx][0]),xnm,sizeof(xnm)); copyFlashStr(ccNameShort(CCPairs[S.ccPairIdx][1]),ynm,sizeof(ynm)); snprintf(line,sizeof(line), UI_LANG_RU? "Пара #%u  X:%s Y:%s" : "Pair #%u  X:%s Y:%s",(unsigned)S.ccPairIdx,xnm,ynm); u8g2.setCursor(0,22); u8g2.print(line); }
    }
    char big[28];
    if(!S.notesMode) snprintf(big,sizeof(big),"CC%u / CC%u",liveCC_X,liveCC_Y);
    else { if(liveNoteOn){ char nm[8]; noteName(liveNote,nm,sizeof(nm)); snprintf(big,sizeof(big), UI_LANG_RU? "%s / %u" : "%s / %u",nm,liveVel);} else snprintf(big,sizeof(big), UI_LANG_RU? "-- / --" : "-- / --"); }
    u8g2.setFont(OLED_FONT_BIG); uint8_t w=u8g2.getStrWidth(big); int x=(128-w)/2; if(x<0)x=0; u8g2.setCursor(x,50); u8g2.print(big);
    u8g2.setFont(OLED_FONT_SMALL); u8g2.setCursor(0,64); u8g2.print(UI_LANG_RU? F("Держи ENC 5с: Меню/Сохр | D7:Аккорд | D4:Арп") : F("Hold ENC 5s: Menu/Save | D7:Chord | D4:Arp"));
  }while(u8g2.nextPage());
}
static void renderCalOLED(uint32_t ms_left){
  u8g2.firstPage(); do{
    u8g2.setFont(OLED_FONT_SMALL); if (OLED_USE_UTF8) u8g2.enableUTF8Print();
    u8g2.setCursor(0,12); u8g2.print(UI_LANG_RU? F("КАЛИБРАЦИЯ 10с") : F("CALIBRATION 10s"));
    u8g2.setCursor(0,24); u8g2.print(UI_LANG_RU? F("Движение: LB->RB") : F("Move: LB -> RB"));
    u8g2.setCursor(0,36); u8g2.print(UI_LANG_RU? F("            RT->LT") : F("       RT -> LT"));
    u8g2.setCursor(0,48); u8g2.print(UI_LANG_RU? F("Не отрывать палец") : F("Don't lift finger"));
    u8g2.setCursor(0,60); u8g2.print(UI_LANG_RU? F("Осталось: ") : F("Left: ")); u8g2.print(ms_left/1000); u8g2.print(UI_LANG_RU? F("с"):F("s"));
  }while(u8g2.nextPage());
}
// OLED Label Editor (ASCII/UTF-8 печать уже включена выше)
static bool labelEditActive=false; static uint8_t labelCursor=0;
static const char CHARSET[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_/+#&:.,";
static int charsetIndex(char c){ const char* p=strchr(CHARSET,c?c:' '); return p? (int)(p-CHARSET) : 0; }
static char charsetStep(char c,int step){ int n=(int)strlen(CHARSET); int i=charsetIndex(c); i=(i+step)%n; if(i<0)i+=n; return CHARSET[i]; }
static void renderLabelEditorOLED(){
  u8g2.firstPage(); do{
    u8g2.setFont(OLED_FONT_SMALL); if (OLED_USE_UTF8) u8g2.enableUTF8Print();
    u8g2.setCursor(0,10); u8g2.print(UI_LANG_RU? F("Редакт. метки (пара #") : F("Edit Label (pair #")); u8g2.print(S.ccPairIdx); u8g2.print(")");
    u8g2.setFont(OLED_FONT_BIG); char buf[LABEL_LEN+1]; strncpy(buf,Labels[S.ccPairIdx],LABEL_LEN); buf[LABEL_LEN]='\0';
    uint8_t w=u8g2.getStrWidth(buf); int x=(128-w)/2; if(x<0)x=0; u8g2.setCursor(x,46); u8g2.print(buf);
    u8g2.setDrawColor(1); int cw=u8g2.getMaxCharWidth()-1; int cx=x+(int)labelCursor*cw; if(cx<0)cx=0; if(cx>127)cx=127; u8g2.drawHLine(cx,50,cw>0?cw:6);
    u8g2.setFont(OLED_FONT_SMALL); u8g2.setCursor(0,62); u8g2.print(UI_LANG_RU? F("ЭНК:симв  D9:->  D10:<-  долгий:Сохр") : F("ENC:char  D9:->  D10:<-  long ENC:Save"));
  }while(u8g2.nextPage());
}

  #define RENDER_STATUS()       renderStatusOLED()
  #define RENDER_UI()           renderUIOLED()
  #define RENDER_CAL(ms_left)   renderCalOLED(ms_left)
  #define RENDER_LABEL_EDITOR() renderLabelEditorOLED()

#else // ── 1602 рендер ─────────────────────────────────────────────────────────

static void noteName(uint8_t note,char* out,size_t n){ static const char* nm[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; uint8_t pc=note%12; int8_t oct=(int8_t)note/12 - 1; snprintf(out,n,"%s%d",nm[pc],oct); }

static bool labelEditActive=false; static uint8_t labelCursor=0;
static const char CHARSET[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_/+#&:.,";
static int charsetIndex(char c){ const char* p=strchr(CHARSET,c?c:' '); return p? (int)(p-CHARSET) : 0; }
static char charsetStep(char c,int step){ int n=(int)strlen(CHARSET); int i=charsetIndex(c); i=(i+step)%n; if(i<0)i+=n; return CHARSET[i]; }

static inline void lcdClr(){ lcd.clear(); }
static inline void lcdNoCursorBlink(){ lcd.noCursor(); lcd.noBlink(); }

static void renderUI1602(){
  lcdClr(); lcdNoCursorBlink();
  const __FlashStringHelper* name =
    (cursor==F_MODE)? L("Режим","Mode"):
    (cursor==F_CCX)?  L("CC X","CC X"):
    (cursor==F_CCY)?  L("CC Y","CC Y"):
    (cursor==F_CCPAIRSEL)? L("Пара CC","CC Pair"):
    (cursor==F_CCPAIR_X)?  L("Пара X","Pair X"):
    (cursor==F_CCPAIR_Y)?  L("Пара Y","Pair Y"):
    (cursor==F_CCPAIR_LABEL)?L("Метка","Label"):
    (cursor==F_CCPAIR_SAVE)? L("Сохр. пары","Save Pairs"):
#if LED_MENU_ENABLED
    (cursor==F_LED_ENABLE)? L("LED Вкл","LED Enable"):
    (cursor==F_LED_BRIGHT)? L("LED Ярк","LED Bright"):
#endif
    (cursor==F_NMIN)? L("Мин нота","NoteMin"):
    (cursor==F_NMAX)? L("Макс нота","NoteMax"):
    (cursor==F_SCALE)? L("Лад","Scale"):
    (cursor==F_TЕМPO)? L("Темп","Tempo"):
    (cursor==F_CLKIN)? L("Clock Вх","Clock In"):
    (cursor==F_ARPSTYLE)? L("Арпеджио","ArpStyle"):
    (cursor==F_DIV)? L("Деление","Division"):
    (cursor==F_CHLEN)? L("Дл. акк","Chord Len"):
    (cursor==F_INVY)? L("Инверт Y","InvertY"):
    (cursor==F_CALNOW)? L("Калибр.","Calibrate"):
    (cursor==F_PERFSLOT)? L("Профиль","Perf Slot"):
    (cursor==F_PERFLOAD)? L("Загрузить","Load Perf"):
    (cursor==F_PERFSAVE)? L("Сохранить","Save Perf"): L("","");

  lcd.setCursor(0,0); lcd.print(name);

  char v[17];
  switch(cursor){
    case F_MODE:  snprintf(v,sizeof(v), S.notesMode? (UI_LANG_RU? "НОТЫ":"NOTES") : "CC"); break;
    case F_CCX:   snprintf(v,sizeof(v),"CCX=%u",S.ccX); break;
    case F_CCY:   snprintf(v,sizeof(v),"CCY=%u",S.ccY); break;
    case F_CCPAIRSEL: snprintf(v,sizeof(v), UI_LANG_RU? "Пара#%u":"Pair#%u",(unsigned)S.ccPairIdx); break;
    case F_CCPAIR_X:  snprintf(v,sizeof(v),"%u",CCPairs[S.ccPairIdx][0]); break;
    case F_CCPAIR_Y:  snprintf(v,sizeof(v),"%u",CCPairs[S.ccPairIdx][1]); break;
    case F_CCPAIR_LABEL:{ char tmp[LABEL_LEN+1]; strncpy(tmp,Labels[S.ccPairIdx],LABEL_LEN); tmp[LABEL_LEN]=0; snprintf(v,sizeof(v),"\"%.12s\"",tmp);} break;
    case F_CCPAIR_SAVE: snprintf(v,sizeof(v), UI_LANG_RU? "Нажать":"Press"); break;
#if LED_MENU_ENABLED
    case F_LED_ENABLE: snprintf(v,sizeof(v), S.ledEnable? (UI_LANG_RU?"Вкл":"On"):(UI_LANG_RU?"Выкл":"Off")); break;
    case F_LED_BRIGHT: snprintf(v,sizeof(v),"Bri %u",S.ledBrightness); break;
#endif
    case F_NMIN:  snprintf(v,sizeof(v),"Nmin %u",S.noteMin); break;
    case F_NMAX:  snprintf(v,sizeof(v),"Nmax %u",S.noteMax); break;
    case F_SCALE: snprintf(v,sizeof(v),"%u",(unsigned)S.scaleIndex); break;
    case F_TEMPO: snprintf(v,sizeof(v),"%ubpm",(unsigned)S.tempoBPM); break;
    case F_CLKIN: snprintf(v,sizeof(v), S.clockIn? (UI_LANG_RU?"Вкл":"On"):(UI_LANG_RU?"Выкл":"Off")); break;
    case F_ARPSTYLE: snprintf(v,sizeof(v),"%u",(unsigned)S.arpStyle); break;
    case F_DIV:   snprintf(v,sizeof(v),"%s",DIVS[S.divIndex].label); break;
    case F_CHLEN: snprintf(v,sizeof(v),"Chord %u",S.chordLen); break;
    case F_INVY:  snprintf(v,sizeof(v), invertY? (UI_LANG_RU?"Да":"Yes"):(UI_LANG_RU?"Нет":"No")); break;
    case F_CALNOW:{ bool ok=validSpan(G.calXmin,G.calXmax)&&validSpan(G.calYmin,G.calYmax); snprintf(v,sizeof(v), ok? (UI_LANG_RU?"Сохранено":"Stored") : (UI_LANG_RU?"Нужна":"Required")); } break;
    case F_PERFSLOT: snprintf(v,sizeof(v),"#%u",(unsigned)G.lastPerf); break;
    case F_PERFLOAD: snprintf(v,sizeof(v), UI_LANG_RU? "Проф#%u":"Load#%u",(unsigned)G.lastPerf); break;
    case F_PERFSAVE: snprintf(v,sizeof(v), UI_LANG_RU? "Проф#%u":"Save#%u",(unsigned)G.lastPerf); break;
    default: snprintf(v,sizeof(v),""); break;
  }
  lcd.setCursor(0,1); lcd.print(v);
}

static void renderStatus1602(){
  uint32_t now=millis(); if(now-lastStatusTs<120) return; lastStatusTs=now;
  lcdClr(); lcdNoCursorBlink();
  if (S.notesMode){
    char lo[8],hi[8]; noteName(S.noteMin,lo,sizeof(lo)); noteName(S.noteMax,hi,sizeof(hi));
    lcd.setCursor(0,0);
    lcd.print(L("НОТЫ ","NOTES "));
    lcd.print(L("Диап:","R:"));
    char r[16]; snprintf(r,sizeof(r),"%s-%s",lo,hi); lcd.print(r);
    lcd.setCursor(0,1);
    if(liveNoteOn){ char nm[8]; noteName(liveNote,nm,sizeof(nm)); char l2[17]; snprintf(l2,sizeof(l2),"%s/%3u",nm,liveVel); lcd.print(l2); }
    else lcd.print(L("--/--","--/--"));
  } else {
    lcd.setCursor(0,0);
    if (Labels[S.ccPairIdx][0]){ lcd.print(L("CC ","CC ")); lcd.print(Labels[S.ccPairIdx]); }
    else{
      char xnm[10],ynm[10]; copyFlashStr(ccNameShort(CCPairs[S.ccPairIdx][0]),xnm,sizeof(xnm)); copyFlashStr(ccNameShort(CCPairs[S.ccPairIdx][1]),ynm,sizeof(ynm));
      char t[17]; snprintf(t,sizeof(t),"X:%s Y:%s",xnm,ynm); lcd.print(t);
    }
    lcd.setCursor(0,1); char b[17]; snprintf(b,sizeof(b),"CC%3u/CC%3u",liveCC_X,liveCC_Y); lcd.print(b);
  }
}
static void renderCal1602(uint32_t ms_left){
  lcdClr(); lcdNoCursorBlink();
  lcd.setCursor(0,0); lcd.print(L("Калибр. 10с","Cal 10s run..."));
  lcd.setCursor(0,1); char b[17];
  snprintf(b,sizeof(b), L("Ост:%lus","Left:%lus"), (unsigned long)(ms_left/1000)); lcd.print(b);
}
static void renderLabelEditor1602(){
  lcdClr();
  lcd.setCursor(0,0); lcd.print(L("Метка:","Lbl:"));
  for(uint8_t i=0;i<LABEL_LEN;i++){ char c = Labels[S.ccPairIdx][i]; lcd.print(c?c:' '); }
  lcd.setCursor(0,1); lcd.print(L("ЭНК симв D9> D10<","ENC ch  D9-> D10<-"));
  uint8_t col =  (UI_LANG_RU ? 7 : 4) + labelCursor; // "Метка:"=6+пробел, "Lbl:"=4
  if (col >= LCD_COLS) col = LCD_COLS-1;
  lcd.setCursor(col, 0); lcd.cursor(); lcd.blink();
}

  #define RENDER_STATUS()       renderStatus1602()
  #define RENDER_UI()           renderUI1602()
  #define RENDER_CAL(ms_left)   renderCal1602(ms_left)
  #define RENDER_LABEL_EDITOR() renderLabelEditor1602()
#endif // дисплеи

// ── Калибровка переходы ──────────────────────────────────────────────────────
static void beginCalWait(){ state_=CAL_WAIT_TOUCH; calXminRt=calYminRt=0xFFFF; calXmaxRt=calYmaxRt=0; RENDER_CAL(CAL_WINDOW_MS); }
static void beginCalRun(){ state_=CAL_RUNNING; calDeadline=millis()+CAL_WINDOW_MS; RENDER_CAL(CAL_WINDOW_MS); }
static void finishCal(){
  state_=RUN;
  if(!validSpan(calXminRt,calXmaxRt)||!validSpan(calYminRt,calYmaxRt)){
    calXminRt=ADC_MAX/20; calXmaxRt=ADC_MAX-ADC_MAX/20;
    calYminRt=ADC_MAX/20; calYmaxRt=ADC_MAX-ADC_MAX/20;
  }
  G.calXmin=calXminRt; G.calXmax=calXmaxRt; G.calYmin=calYminRt; G.calYmax=calYmaxRt; saveGlobal();
  RENDER_STATUS();
}

// ── Квантизация/аккорды/арп ─────────────────────────────────────────────────
static int findDegreeIndex(uint8_t pc,uint8_t tonicPc,const ScaleDef& sc){ for(uint8_t i=0;i<sc.count;i++){ if(((tonicPc+sc.steps[i])%12)==pc) return i; } return -1; }
static uint8_t quantizeToScale(uint8_t note){
  const ScaleDef& sc=SCALES[S.scaleIndex]; uint8_t tonicPc=S.noteMin%12; int bestNote=note, bestDiff=127;
  for(int oc=-2; oc<=+2; ++oc){ int baseOct=(note/12)+oc; for(uint8_t i=0;i<sc.count;i++){ int cand=baseOct*12+tonicPc+sc.steps[i]; int diff=abs(cand-(int)note); if(diff<bestDiff){ bestDiff=diff; bestNote=cand; } } }
  if(bestNote < S.noteMin) bestNote=S.noteMin; if(bestNote > S.noteMax) bestNote=S.noteMax; return (uint8_t)bestNote;
}
static void buildChord(uint8_t root,uint8_t chord[],uint8_t chordLen){
  const ScaleDef& sc=SCALES[S.scaleIndex]; uint8_t tonicPc=S.noteMin%12;
  uint8_t rpc=root%12; int r=findDegreeIndex(rpc,tonicPc,sc); if(r<0){ root=quantizeToScale(root); rpc=root%12; r=findDegreeIndex(rpc,tonicPc,sc); }
  chord[0]=root; int prev=sc.steps[r], cur=root, idx=r;
  for(uint8_t k=1;k<chordLen;k++){ idx=(idx+2)%sc.count; int step=sc.steps[idx]; int d=step-prev; if(d<=0)d+=12; cur+=d; chord[k]=(uint8_t)clampU16(cur,0,127); prev=step; }
}
static uint32_t nextArpMs=0; static bool arpActive=false; static uint8_t arpNotes[4]={0}, arpLen=4; static int8_t arpIdx=0, arpDir=1; static uint8_t arpPrevNote=255; static uint8_t lastRootForArp=255; static uint8_t oiLow=0,oiHigh=0; static bool oiTakeLow=true; static int8_t rwPos=0;
static uint8_t  clkAcc=0; static uint32_t clkLastTickMs=0;
static inline uint32_t msPerStep(){ return (uint32_t)(60000UL / (S.tempoBPM * (uint32_t)DIVS[S.divIndex].denom)); }
static inline bool clockActive(){ return S.clockIn && (millis()-clkLastTickMs)<500; }
static void arpResetPattern(const uint8_t chord[],uint8_t chordLen){ memcpy(arpNotes,chord,chordLen); arpLen=chordLen; arpIdx=0; arpDir=1; arpPrevNote=255; nextArpMs=millis(); lastRootForArp=chord[0]; oiLow=0; oiHigh=arpLen?arpLen-1:0; oiTakeLow=true; rwPos=0; clkAcc=0; }
static uint8_t arpNextIndex(){ switch(S.arpStyle){ case ARP_UP:{uint8_t i=arpIdx; arpIdx=(arpIdx+1)%arpLen; return i;} case ARP_DOWN:{uint8_t i=arpIdx; arpIdx=(arpIdx==0?arpLen-1:arpIdx-1); return i;} case ARP_PINGPONG:{uint8_t i=arpIdx; arpIdx+=arpDir; if(arpIdx>=arpLen){arpIdx=arpLen-1; arpDir=-1;} else if(arpIdx<0){arpIdx=0; arpDir=+1;} return i;} case ARP_UPDOWN:{uint8_t i=arpIdx; arpIdx+=arpDir; if(arpIdx>=arpLen){arpIdx=arpLen-2; arpDir=-1;} else if(arpIdx<0){arpIdx=1; arpDir=+1;} return i;} case ARP_OUTSIDE_IN:{ if(oiLow>oiHigh){oiLow=0; oiHigh=arpLen-1; oiTakeLow=true;} uint8_t i=oiTakeLow?oiLow++:oiHigh--; oiTakeLow=!oiTakeLow; return i;} case ARP_RANDOM: return (uint8_t)random(0,arpLen); case ARP_RANDOM_WALK:{ if(arpLen<=1) return 0; int step=(random(0,2)==0)?-1:+1; rwPos+=step; if(rwPos<0)rwPos=1; if(rwPos>=arpLen)rwPos=arpLen-2; return (uint8_t)rwPos;} default:return 0; } }
static void arpNoteOffPrev(){ if(arpPrevNote!=255){ midiNoteOff(arpPrevNote); arpPrevNote=255; } }
static void arpStep(uint8_t vel){ uint8_t idx=arpNextIndex(); uint8_t n=arpNotes[idx]; arpNoteOffPrev(); midiNoteOn(n,vel); arpPrevNote=n; liveNoteOn=true; liveNote=n; liveVel=vel; }
static void pollMidiClockIn(){ while(DINMIDI.read()){ auto t=DINMIDI.getType(); if(t==midi::Clock){ clkLastTickMs=millis(); const uint8_t denom=DIVS[S.divIndex].denom; clkAcc+=denom; if(arpActive && btnIsDown(bArp)){ if(clkAcc>=24){ clkAcc-=24; arpStep(liveVel);} } } else if (t==midi::Start || t==midi::Continue){ clkAcc=0; } } }
static void transposeRange(int d){ if(!d) return; int minDown=-(int)S.noteMin; int maxUp=127-(int)S.noteMax; if(d>maxUp)d=maxUp; if(d<minDown)d=minDown; S.noteMin=(uint8_t)clampU16(S.noteMin+d,0,127); S.noteMax=(uint8_t)clampU16(S.noteMax+d,0,127); }

// ── SysEx Identity Reply ─────────────────────────────────────────────────────
static void sendIdentityReply(){
  const uint8_t reply[] = { 0x7E, MIDI_DEVICE_ID, 0x06, 0x02, MIDI_MFR_ID, MIDI_FAMILY_LSB, MIDI_FAMILY_MSB, MIDI_MODEL_LSB, MIDI_MODEL_MSB, MIDI_VER_0, MIDI_VER_1, MIDI_VER_2, MIDI_VER_3 };
  DINMIDI.sendSysEx(sizeof(reply), reply, false);
}
void handleSysEx(byte* data, unsigned size){
  if (size >= 5 && data[0]==0x7E && data[2]==0x06 && data[3]==0x01){ uint8_t dst = data[1]; if (dst==0x7F || dst==MIDI_DEVICE_ID) sendIdentityReply(); }
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup(){
  pinMode(ENC_A,INPUT_PULLUP); pinMode(ENC_B,INPUT_PULLUP); pinMode(ENC_BTN,INPUT_PULLUP);
  pinMode(BTN_NEXT,INPUT_PULLUP); pinMode(BTN_SAVE,INPUT_PULLUP);
  pinMode(BTN_CAL,INPUT_PULLUP); pinMode(BTN_ARP,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR_A, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR_B, CHANGE);

#if defined(DISP_LCD1602)
  lcd.init(); lcd.backlight(); lcd.noCursor(); lcd.noBlink();
#else
  u8g2.setI2CAddress(OLED_I2C_ADDR<<1); u8g2.begin();
  if (OLED_USE_UTF8) u8g2.enableUTF8Print(); // why: кириллица
#endif

  Serial.begin(31250);
  DINMIDI.begin(MIDI_CHANNEL_OMNI); DINMIDI.turnThruOff();
  DINMIDI.setHandleSystemExclusive(handleSysEx);
  startupBeep();

  (void)loadGlobal();
  if(!loadPerf(G.lastPerf,S)){ defaultsPerfFixed(S); savePerf(G.lastPerf,S); }
  if(!pairsLoad()){ defaultsPairs(); pairsSave(); }
  S.ccX=CCPairs[S.ccPairIdx][0]; S.ccY=CCPairs[S.ccPairIdx][1]; invertY=S.invY;
  if(!validSpan(G.calXmin,G.calXmax)||!validSpan(G.calYmin,G.calYmax)) state_=CAL_WAIT_TOUCH; else state_=RUN;
  randomSeed(analogRead(A0));
  setAllHiZ();
  if(state_==RUN) RENDER_STATUS(); else RENDER_CAL(CAL_WINDOW_MS);
}

// ── Loop ────────────────────────────────────────────────────────────────────
void loop(){
  if (btnFalling(bEnc)) { encPressTs=millis(); encRotDuringHold=false; }
  if (btnRising(bEnc)){
    uint32_t dt=millis()-encPressTs;
    if (labelEditActive){
      if (dt>1500){ pairsSave(); labelEditActive=false; RENDER_UI(); }
    } else if (dt>LONG_PRESS_MS && !encRotDuringHold){
      if(!menuActive){ menuActive=true; RENDER_UI(); }
      else { S.invY=invertY; savePerf(G.lastPerf,S); saveGlobal(); menuActive=false; RENDER_STATUS(); }
    } else if (menuActive){
      if (cursor==F_MODE){ S.notesMode=!S.notesMode; }
      else if (cursor==F_INVY){ invertY=!invertY; }
      else if (cursor==F_CALNOW){ beginCalWait(); }
      else if (cursor==F_PERFLOAD){ if(loadPerf(G.lastPerf,TMP)){ S=TMP; invertY=S.invY; } }
      else if (cursor==F_PERFSAVE){ S.invY=invertY; savePerf(G.lastPerf,S); }
      else if (cursor==F_CCPAIR_SAVE){ pairsSave(); }
      else if (cursor==F_CCPAIR_LABEL){ labelEditActive=true; labelCursor=0; RENDER_LABEL_EDITOR(); }
      S.ccX=CCPairs[S.ccPairIdx][0]; S.ccY=CCPairs[S.ccPairIdx][1];
      if(!labelEditActive) RENDER_UI();
    }
  }

  int16_t d=takeEncDelta();
  if (d){
    if (digitalRead(ENC_BTN)==LOW) encRotDuringHold=true;

    if (labelEditActive){
      char c=Labels[S.ccPairIdx][labelCursor];
      Labels[S.ccPairIdx][labelCursor]=charsetStep(c,(d>0)?+1:-1);
      RENDER_LABEL_EDITOR();
    } else if (menuActive){
      const bool encHeld=(digitalRead(ENC_BTN)==LOW); const int mult=encHeld?12:1;
      switch(cursor){
        case F_MODE: S.notesMode=(d>0); break;
        case F_CCX:  {int v=S.ccX+d; if(v<0)v=0; if(v>127)v=127; S.ccX=v;} break;
        case F_CCY:  {int v=S.ccY+d; if(v<0)v=0; if(v>127)v=127; S.ccY=v;} break;
        case F_CCPAIRSEL:{ int v=(int)S.ccPairIdx+d; while(v<0)v+=CC_PAIR_COUNT; while(v>=CC_PAIR_COUNT)v-=CC_PAIR_COUNT; S.ccPairIdx=(uint8_t)v; S.ccX=CCPairs[S.ccPairIdx][0]; S.ccY=CCPairs[S.ccPairIdx][1]; } break;
        case F_CCPAIR_X:{ int v=(int)CCPairs[S.ccPairIdx][0]+d; if(v<0)v=0; if(v>127)v=127; CCPairs[S.ccPairIdx][0]=(uint8_t)v; S.ccX=v; } break;
        case F_CCPAIR_Y:{ int v=(int)CCPairs[S.ccPairIdx][1]+d; if(v<0)v=0; if(v>127)v=127; CCPairs[S.ccPairIdx][1]=(uint8_t)v; S.ccY=v; } break;
        case F_CCPAIR_LABEL: break;
      #if LED_MENU_ENABLED
        case F_LED_ENABLE: S.ledEnable=(d>0); break;
        case F_LED_BRIGHT:{ int v=(int)S.ledBrightness+d; if(v<0)v=0; if(v>60)v=60; S.ledBrightness=(uint8_t)v; } break;
      #endif
        case F_NMIN:{ long v=(long)S.noteMin+(long)d*mult; if(v<0)v=0; if(v>127)v=127; if(v>S.noteMax)v=S.noteMax; S.noteMin=(uint8_t)v; } break;
        case F_NMAX:{ long v=(long)S.noteMax+(long)d*mult; if(v<0)v=0; if(v>127)v=127; if(v<S.noteMin)v=S.noteMin; S.noteMax=(uint8_t)v; } break;
        case F_SCALE:{ int v=(int)S.scaleIndex+(d>0?+1:-1); if(v<0)v=SC__COUNT-1; if(v>=SC__COUNT)v=0; S.scaleIndex=(uint8_t)v; } break;
        case F_TEMPO:{ int v=(int)S.tempoBPM+(d>0?+1:-1); if(v<40)v=40; if(v>240)v=240; S.tempoBPM=(uint16_t)v; } break;
        case F_CLKIN: S.clockIn=(d>0); break;
        case F_ARPSTYLE:{ int v=(int)S.arpStyle+(d>0?+1:-1); if(v<0)v=ARP__COUNT-1; if(v>=ARP__COUNT)v=0; S.arpStyle=(uint8_t)v; } break;
        case F_DIV:{ int v=(int)S.divIndex+(d>0?+1:-1); if(v<0)v=DIV__COUNT-1; if(v>=DIV__COUNT)v=0; S.divIndex=(uint8_t)v; } break;
        case F_CHLEN:{ int v=(int)S.chordLen+(d>0?+1:-1); if(v<3)v=3; if(v>4)v=4; S.chordLen=(uint8_t)v; } break;
        case F_INVY: invertY=(d>0); break;
        case F_CALNOW: break;
        case F_PERFSLOT:{ int v=(int)G.lastPerf+(d>0?+1:-1); if(v<0)v=PERF_SLOTS-1; if(v>=PERF_SLOTS)v=0; G.lastPerf=(uint8_t)v; saveGlobal(); } break;
        default: break;
      }
      RENDER_UI();
    } else {
      if (S.notesMode){ const bool encHeld=(digitalRead(ENC_BTN)==LOW); const int mult=encHeld?12:1; transposeRange((int)d*mult); RENDER_STATUS(); }
      else { int v=(int)S.ccPairIdx+d; while(v<0)v+=CC_PAIR_COUNT; while(v>=CC_PAIR_COUNT)v-=CC_PAIR_COUNT; S.ccPairIdx=(uint8_t)v; S.ccX=CCPairs[S.ccPairIdx][0]; S.ccY=CCPairs[S.ccPairIdx][1]; RENDER_STATUS(); }
    }
  }

  if (labelEditActive){
    if (btnFalling(bNext)) { if (labelCursor < LABEL_LEN-1) labelCursor++; RENDER_LABEL_EDITOR(); }
    if (btnFalling(bSave)) { Labels[S.ccPairIdx][labelCursor] = ' '; if (labelCursor>0) labelCursor--; RENDER_LABEL_EDITOR(); }
    return;
  }
  if (menuActive){
    if (btnFalling(bNext)) { cursor=(UiField)((cursor+1)%F__COUNT); RENDER_UI(); }
    if (btnFalling(bSave)) { S.invY=invertY; savePerf(G.lastPerf,S); RENDER_UI(); }
  }

  if (btnFalling(bCal)) calPressTs=millis();
  if (btnRising(bCal)){
    bool touchingNow = liveNoteOn; uint32_t dt=millis()-calPressTs;
    if (!touchingNow){ if (dt>800){ invertY=!invertY; S.invY=invertY; savePerf(G.lastPerf,S); if(menuActive) RENDER_UI(); else RENDER_STATUS(); } else beginCalWait(); }
  }

  uint16_t rawX=readAxisAvg(true), rawY=readAxisAvg(false);
  uint16_t dX=pullProbeX(), dY=pullProbeY();
  bool touching = midRange(rawX)&&midRange(rawY)&&(dX<PULL_DELTA)&&(dY<PULL_DELTA);

  if (state_==CAL_WAIT_TOUCH){ if(touching) beginCalRun(); setAllHiZ(); delay(1); return; }
  if (state_==CAL_RUNNING){
    if (touching){
      if(rawX<calXminRt) calXminRt=rawX; if(rawX>calXmaxRt) calXmaxRt=rawX;
      if(rawY<calYminRt) calYminRt=rawY; if(rawY>calYmaxRt) calYmaxRt=rawY;
    }
    uint32_t left=(calDeadline>millis())?(calDeadline-millis()):0; RENDER_CAL(left);
    if (millis()>=calDeadline){ G.calXmin=calXminRt; G.calXmax=calXmaxRt; G.calYmin=calYminRt; G.calYmax=calYmaxRt; saveGlobal(); finishCal(); }
    setAllHiZ(); delay(1); return;
  }

  if (millis()-lastLoopTs<LOOP_MS){ setAllHiZ(); return; }
  lastLoopTs=millis();
  float nx=norm01(rawX,G.calXmin,G.calXmax,false);
  float ny=norm01(rawY,G.calYmin,G.calYmax,invertY);

  if (!S.notesMode){
    uint8_t ccX=toCC(nx), ccY=toCC(ny);
    static uint8_t edgeX=255, edgeY=255;
    if(edgeX==255 || (uint8_t)abs((int)ccX-(int)edgeX)>=CC_DELTA){ midiCC(S.ccX,ccX); edgeX=ccX; }
    if(edgeY==255 || (uint8_t)abs((int)ccY-(int)edgeY)>=CC_DELTA){ midiCC(S.ccY,ccY); edgeY=ccY; }
    liveCC_X=edgeX; liveCC_Y=edgeY; liveNoteOn=false;
  } else {
    uint8_t rootLin = S.noteMin + (uint8_t)round(nx * (S.noteMax - S.noteMin));
    uint8_t root    = quantizeToScale(rootLin);
    uint8_t vel     = (uint8_t)max(1,(int)round(ny*127));
    bool chordHold  = btnIsDown(bCal);
    bool arpHold    = btnIsDown(bArp);
    if (S.clockIn) pollMidiClockIn();

    if (touching){
      if (arpHold){
        uint8_t chord[4]={0}; buildChord(root,chord,S.chordLen);
        if(lastRootForArp!=root){ arpResetPattern(chord,S.chordLen); }
        arpActive=true; liveVel=vel;
        if (!clockActive()){ if (millis()>=nextArpMs){ nextArpMs=millis()+msPerStep(); arpStep(vel);} }
      } else if (chordHold){
        static bool chordOn=false; static uint8_t prevChord[4]={255,255,255,255};
        uint8_t chord[4]={0}; buildChord(root,chord,S.chordLen);
        if (!chordOn){ for(uint8_t i=0;i<S.chordLen;i++) midiNoteOn(chord[i],vel); memcpy(prevChord,chord,4); chordOn=true; }
        else { if (memcmp(prevChord,chord,S.chordLen)!=0){ for(uint8_t i=0;i<S.chordLen;i++) midiNoteOff(prevChord[i]); for(uint8_t i=0;i<S.chordLen;i++) midiNoteOn(chord[i],vel); memcpy(prevChord,chord,4);} else { midiChanPressure(vel);} }
        liveNoteOn=true; liveNote=root; liveVel=vel; arpActive=false; arpNoteOffPrev();
      } else {
        static uint8_t cur=255;
        if (cur==255){ midiNoteOn(root,vel); cur=root; }
        else if (root!=cur){ midiNoteOff(cur); midiNoteOn(root,vel); cur=root; }
        else { midiChanPressure(vel); }
        liveNoteOn=true; liveNote=root; liveVel=vel; arpActive=false; arpNoteOffPrev();
      }
    } else {
      static uint8_t cur=255; if(cur!=255){ midiNoteOff(cur); cur=255; }
      if(arpActive){ arpActive=false; arpNoteOffPrev(); }
      liveNoteOn=false;
    }
  }

  if(!menuActive && state_==RUN && !labelEditActive) RENDER_STATUS();
  setAllHiZ();
}