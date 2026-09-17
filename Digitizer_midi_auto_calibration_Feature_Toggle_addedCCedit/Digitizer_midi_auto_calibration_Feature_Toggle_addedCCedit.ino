// File: Digitizer_MIDI_AllFeatures_Toggles_FIX.ino
// Target: Arduino Nano (DIN MIDI @31250) по умолчанию. Все фичи под #define.

//============================= BUILD TOGGLES ==================================
//#define PLATFORM_RP2040
#ifndef PLATFORM_RP2040
  #if defined(ARDUINO_ARCH_RP2040)
    #define PLATFORM_RP2040 1
  #else
    #define PLATFORM_RP2040 0
  #endif
#endif
// RP2040 backends (игнор на AVR):
//#define MIDI_BACKEND_USB
//#define MIDI_BACKEND_DIN

//#define DISP_LCD1602
#define DISP_SSD1306_U8X8

//#define UI_LANG_RU

#define FEAT_ARP          1
#define FEAT_CLOCK_IN     1
#define FEAT_TEMPO        1
#define FEAT_SCALES_FULL  1
#define FEAT_ARP_DIV      1
#define FEAT_CHORD        1
#define FEAT_LABELS       1
#define FEAT_CC_EDIT      1
#define FEAT_PROFILES     0
#define FEAT_LED          0
#define FEAT_SYSEX        0

//============================= INCLUDES =======================================
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <string.h>

#if defined(ARDUINO_ARCH_AVR)
  #include <avr/pgmspace.h>
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #ifndef pgm_read_byte
    #define pgm_read_byte(addr) (*(const uint8_t*)(addr))
  #endif
  #ifndef pgm_read_word
    #define pgm_read_word(addr) ((uintptr_t)(addr))
  #endif
  static inline void strncpy_P(char* d,const char* s,size_t n){ strncpy(d,s,n); }
#endif

#if PLATFORM_RP2040 && defined(MIDI_BACKEND_USB)
  #include <Adafruit_TinyUSB.h>
  #include <MIDI.h>
  Adafruit_USBD_MIDI usb_midi;
  MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIIF);
#elif PLATFORM_RP2040 && defined(MIDI_BACKEND_DIN)
  #include <MIDI.h>
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDIIF);
#else
  #include <MIDI.h>
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial, MIDIIF);
#endif

#if defined(DISP_LCD1602)
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(0x27,16,2);
#elif defined(DISP_SSD1306_U8X8)
  #include <U8x8lib.h>
  U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);
#else
  #error "Select display: DISP_SSD1306_U8X8 or DISP_LCD1602"
#endif

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

//============================= PINS/CONST =====================================
static const uint8_t PIN_XP=A0, PIN_YP=A1, PIN_XM=5, PIN_YM=6;
static const uint8_t ENC_A=2, ENC_B=3, ENC_BTN=8;
static const uint8_t BTN_NEXT=9, BTN_SAVE=10, BTN_CHORD=7, BTN_ARP=4;

#if FEAT_LED
  #define LED_PIN 11
  #define LED_HAS_HW 0
#endif

static const uint8_t  MIDI_CH=1;
static const uint16_t ADC_MAX=1023;
static const uint8_t  SAMPLES=5;
static const uint16_t TOUCH_NOISE=12;
static const uint16_t PULL_DELTA=128;
static const uint32_t LONG_PRESS_MS=5000;

//============================= INPUT/ENC ======================================
struct DebBtn{ uint8_t pin, st; uint32_t t; };
static DebBtn bEnc{ENC_BTN,HIGH,0}, bNext{BTN_NEXT,HIGH,0}, bSave{BTN_SAVE,HIGH,0},
              bChord{BTN_CHORD,HIGH,0}, bArp{BTN_ARP,HIGH,0};
static uint32_t encPressTs=0; static bool encRotDuringHold=false;
volatile int16_t encDelta=0;
static inline void encISR_A(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a==b)?+1:-1; }
static inline void encISR_B(){ bool a=digitalRead(ENC_A), b=digitalRead(ENC_B); encDelta += (a!=b)?+1:-1; }
static inline int16_t takeEnc(){ noInterrupts(); int16_t d=encDelta; encDelta=0; interrupts(); return d; }
static bool btnFalling(DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ b.st=n; b.t=millis(); return n==LOW; } return false; }
static bool btnRising (DebBtn& b){ uint8_t n=digitalRead(b.pin); if(n!=b.st && millis()-b.t>30){ uint8_t was=b.st; b.st=n; b.t=millis(); return (was==LOW && n==HIGH);} return false; }
static bool btnDown   (DebBtn& b){ return b.st==LOW; }

//============================= EEPROM =========================================
struct Persist{
  uint8_t  magic, version;
  uint16_t calXmin, calXmax, calYmin, calYmax;
  uint8_t  lastPair;
  bool     notesMode;
  uint8_t  noteMin, noteMax;
  uint8_t  scaleIdx;
#if FEAT_TEMPO
  uint16_t tempoBPM;
#endif
#if FEAT_CLOCK_IN
  bool     clockIn;
#endif
#if FEAT_ARP_DIV
  uint8_t  divIndex;
#endif
#if FEAT_ARP
  uint8_t  arpStyle;
#endif
#if FEAT_CHORD
  uint8_t  chordLen;
#endif
  bool     invY;
#if FEAT_LABELS
  char     labels[10][9];
#endif
#if FEAT_CC_EDIT
  uint8_t  ccPairs[10][2];
#endif
#if FEAT_LED
  bool     ledEnable; uint8_t ledBrightness;
#endif
  uint16_t crc;
} G;

#if FEAT_PROFILES
struct Perf{
  uint8_t  magic, version;
  bool     notesMode; uint8_t lastPair;
  uint8_t  noteMin, noteMax, scaleIdx;
  #if FEAT_TEMPO
    uint16_t tempoBPM;
  #endif
  #if FEAT_CLOCK_IN
    bool     clockIn;
  #endif
  #if FEAT_ARP_DIV
    uint8_t  divIndex;
  #endif
  #if FEAT_ARP
    uint8_t  arpStyle;
  #endif
  #if FEAT_CHORD
    uint8_t  chordLen;
  #endif
  bool     invY;
  #if FEAT_LED
    bool     ledEnable; uint8_t ledBrightness;
  #endif
  #if FEAT_CC_EDIT
    uint8_t  ccPairs[10][2];
  #endif
  uint16_t crc;
};
#endif

static uint16_t crc16(const uint8_t* p,size_t n){ uint16_t c=0xFFFF; for(size_t i=0;i<n;i++){ c^=(uint16_t)p[i]<<8; for(uint8_t b=0;b<8;b++) c=(c&0x8000)?(c<<1)^0x1021:(c<<1);} return c; }
static void eewrite(int a,const void* s,size_t n){ const uint8_t* p=(const uint8_t*)s; for(size_t i=0;i<n;i++) EEPROM.update(a+i,p[i]); }
static void eeread (int a,void* d,size_t n){ uint8_t* p=(uint8_t*)d; for(size_t i=0;i<n;i++) p[i]=EEPROM.read(a+i); }
static const int EEADDR=0;

#if FEAT_PROFILES
  static int EEADDR_PROF_BASE(){ return EEADDR + (int)sizeof(G); }
  static int EEADDR_PROF(int idx){ return EEADDR_PROF_BASE() + idx*(int)sizeof(Perf); }
#endif

static void defaults(){
  memset(&G,0,sizeof(G));
  G.magic='M'; G.version=8;
  G.calXmin=100; G.calXmax=900; G.calYmin=100; G.calYmax=900;
  G.lastPair=0; G.notesMode=false; G.noteMin=48; G.noteMax=72; G.scaleIdx=0;
#if FEAT_TEMPO
  G.tempoBPM=120;
#endif
#if FEAT_CLOCK_IN
  G.clockIn=false;
#endif
#if FEAT_ARP_DIV
  G.divIndex=1;
#endif
#if FEAT_ARP
  G.arpStyle=0;
#endif
#if FEAT_CHORD
  G.chordLen=4;
#endif
  G.invY=true;
#if FEAT_LABELS
  const char* dflt[10]={"Mod/Expr","Vol/Pan","Cut/Res","Atk/Rel","Rev/Cho","Foot/Br","Port/On","Var/Cut","Trm/Phs","Det/Cho"};
  for(uint8_t i=0;i<10;i++){ strncpy(G.labels[i], dflt[i], 8); G.labels[i][8]=0; }
#endif
#if FEAT_CC_EDIT
  const uint8_t defPairs[10][2]={{1,11},{7,10},{74,71},{73,72},{91,93},{4,2},{5,65},{70,74},{92,95},{94,93}};
  memcpy(G.ccPairs, defPairs, sizeof(G.ccPairs));
#endif
#if FEAT_LED
  G.ledEnable=false; G.ledBrightness=30;
#endif
  G.crc=0; G.crc=crc16((uint8_t*)&G,sizeof(G)-2);
}
static inline bool calOK(){ return (G.calXmax>G.calXmin+100)&&(G.calYmax>G.calYmin+100); }
static void savePersist(){ G.crc=crc16((uint8_t*)&G,sizeof(G)-2); eewrite(EEADDR,&G,sizeof(G)); }
static void loadPersist(){ eeread(EEADDR,&G,sizeof(G)); if(G.magic!='M'||G.version!=8||crc16((uint8_t*)&G,sizeof(G)-2)!=G.crc){ defaults(); savePersist(); } }

#if FEAT_PROFILES
static void perfFromPersist(Perf& P){
  memset(&P,0,sizeof(P)); P.magic='P'; P.version=2;
  P.notesMode=G.notesMode; P.lastPair=G.lastPair; P.noteMin=G.noteMin; P.noteMax=G.noteMax; P.scaleIdx=G.scaleIdx;
  #if FEAT_TEMPO
    P.tempoBPM=G.tempoBPM;
  #endif
  #if FEAT_CLOCK_IN
    P.clockIn=G.clockIn;
  #endif
  #if FEAT_ARP_DIV
    P.divIndex=G.divIndex;
  #endif
  #if FEAT_ARP
    P.arpStyle=G.arpStyle;
  #endif
  #if FEAT_CHORD
    P.chordLen=G.chordLen;
  #endif
  P.invY=G.invY;
  #if FEAT_LED
    P.ledEnable=G.ledEnable; P.ledBrightness=G.ledBrightness;
  #endif
  #if FEAT_CC_EDIT
    memcpy(P.ccPairs,G.ccPairs,sizeof(G.ccPairs));
  #endif
  P.crc=0; P.crc=crc16((uint8_t*)&P,sizeof(P)-2);
}
static void persistFromPerf(const Perf& P){
  G.notesMode=P.notesMode; G.lastPair=P.lastPair; G.noteMin=P.noteMin; G.noteMax=P.noteMax; G.scaleIdx=P.scaleIdx;
  #if FEAT_TEMPO
    G.tempoBPM=P.tempoBPM;
  #endif
  #if FEAT_CLOCK_IN
    G.clockIn=P.clockIn;
  #endif
  #if FEAT_ARP_DIV
    G.divIndex=P.divIndex;
  #endif
  #if FEAT_ARP
    G.arpStyle=P.arpStyle;
  #endif
  #if FEAT_CHORD
    G.chordLen=P.chordLen;
  #endif
  G.invY=P.invY;
  #if FEAT_LED
    G.ledEnable=P.ledEnable; G.ledBrightness=P.ledBrightness;
  #endif
  #if FEAT_CC_EDIT
    memcpy(G.ccPairs,P.ccPairs,sizeof(G.ccPairs));
  #endif
}
static bool profValid(const Perf& P){ return P.magic=='P' && P.version==2 && crc16((const uint8_t*)&P,sizeof(P)-2)==P.crc; }
static void profSave(uint8_t idx){ Perf P; perfFromPersist(P); eewrite(EEADDR_PROF(idx), &P, sizeof(P)); }
static bool profLoad(uint8_t idx){ Perf P; eeread(EEADDR_PROF(idx), &P, sizeof(P)); if(!profValid(P)) return false; persistFromPerf(P); savePersist(); return true; }
static uint8_t profIndex=0;
#endif

//============================= CC PAIRS =======================================
#if FEAT_CC_EDIT
  static uint8_t pairX(uint8_t i){ return G.ccPairs[i%10][0]; }
  static uint8_t pairY(uint8_t i){ return G.ccPairs[i%10][1]; }
  static void setPairX(uint8_t i,uint8_t v){ G.ccPairs[i%10][0]=v; }
  static void setPairY(uint8_t i,uint8_t v){ G.ccPairs[i%10][1]=v; }
#else
  static const uint8_t CC_PAIRS[10][2] PROGMEM = {
    {1,11},{7,10},{74,71},{73,72},{91,93},{4,2},{5,65},{70,74},{92,95},{94,93}
  };
  static uint8_t pairX(uint8_t i){ return pgm_read_byte(&CC_PAIRS[i%10][0]); }
  static uint8_t pairY(uint8_t i){ return pgm_read_byte(&CC_PAIRS[i%10][1]); }
#endif

//============================= SCALES =========================================
#if FEAT_SCALES_FULL
  static const int8_t S_MAJOR[] ={0,2,4,5,7,9,11};
  static const int8_t S_MINOR[] ={0,2,3,5,7,8,10};
  static const int8_t S_HMAJ[]  ={0,2,4,5,7,8,11};
  static const int8_t S_HMIN[]  ={0,2,3,5,7,8,11};
  static const int8_t S_PHRY[]  ={0,1,3,5,7,8,10};
  static const int8_t S_LYDI[]  ={0,2,4,6,7,9,11};
  static const int8_t S_MIXO[]  ={0,2,4,5,7,9,10};
  static const int8_t S_DORI[]  ={0,2,3,5,7,9,10};
  static const int8_t S_PENTA[] ={0,2,4,7,9};
  enum Scale:uint8_t{SC_MAJOR,SC_MINOR,SC_HMAJ,SC_HMIN,SC_PHRY,SC_LYDI,SC_MIXO,SC_DORI,SC_PENTA,SC__N};
  static const uint8_t SC_LEN[SC__N]={7,7,7,7,7,7,7,7,5};
  static const int8_t* const SC_STEPS[SC__N]={S_MAJOR,S_MINOR,S_HMAJ,S_HMIN,S_PHRY,S_LYDI,S_MIXO,S_DORI,S_PENTA};
  static const char N0[] PROGMEM="Major",N1[] PROGMEM="Minor",N2[] PROGMEM="HarmMaj",N3[] PROGMEM="HarmMin",
                    N4[] PROGMEM="Phryg",N5[] PROGMEM="Lydian",N6[] PROGMEM="Mixol",N7[] PROGMEM="Dorian",N8[] PROGMEM="Pent";
  static const char* const SC_NAMES[] PROGMEM={N0,N1,N2,N3,N4,N5,N6,N7,N8};
#else
  static const int8_t S_MAJOR[]={0,2,4,5,7,9,11};
  static const int8_t S_MINOR[]={0,2,3,5,7,8,10};
  static const int8_t S_PENTA[]={0,2,4,7,9};
  enum Scale:uint8_t{SC_MAJOR,SC_MINOR,SC_PENTA,SC__N};
  static const uint8_t SC_LEN[SC__N]={7,7,5};
  static const int8_t* const SC_STEPS[SC__N]={S_MAJOR,S_MINOR,S_PENTA};
  static const char N0[] PROGMEM="Major", N1[] PROGMEM="Minor", N2[] PROGMEM="Pent";
  static const char* const SC_NAMES[] PROGMEM={N0,N1,N2};
#endif
static inline void pstr(char* out,const char* pgm,size_t n){ strncpy_P(out,(PGM_P)pgm,n-1); out[n-1]=0; }

//============================= ARP / DIV / CLOCK ==============================
#if FEAT_ARP
  enum ArpStyle:uint8_t{ARP_UP,ARP_DOWN,ARP_PINGPONG,ARP_UPDOWN,ARP_OUTSIDE_IN,ARP_RANDOM,ARP_RANDOM_WALK,ARP__COUNT};
#endif
#if FEAT_ARP_DIV
  struct DivDef{ uint8_t denomDiv; const char* label; };
  enum DivIdx:uint8_t{DIV_1_4,DIV_1_8,DIV_1_16,DIV_1_3,DIV_1_6,DIV__COUNT};
  static const DivDef DIVS[DIV__COUNT]={{1,"1/4"},{2,"1/8"},{4,"1/16"},{3,"1/3"},{6,"1/6"}};
#endif

#if FEAT_ARP
  static uint32_t nextArpMs=0; static bool arpActive=false;
  static uint8_t  arpNotes[4]={0}, arpLen=0; static int8_t arpIdx=0, arpDir=1;
  static uint8_t  arpPrevNote=255, lastRootForArp=255;
  static uint8_t  oiLow=0,oiHigh=0; static bool oiTakeLow=true; static int8_t rwPos=0;
  #if FEAT_TEMPO && FEAT_ARP_DIV
    static inline uint32_t msPerStep(){ uint8_t d=DIVS[G.divIndex].denomDiv; return (uint32_t)(60000UL/(uint32_t)G.tempoBPM/(uint32_t)d); }
  #endif
  #if FEAT_CLOCK_IN && FEAT_ARP_DIV
    static inline uint8_t ticksPerStep(){ uint8_t d=DIVS[G.divIndex].denomDiv; return (uint8_t)(24/d); }
    static uint8_t clkTickAcc=0; static uint32_t clkLastMs=0;
    static void midiPollClockIn(){
      if(!G.clockIn) return;
      while(MIDIIF.read()){
        auto t=MIDIIF.getType();
        if(t==midi::Clock){ clkLastMs=millis(); clkTickAcc++; if(clkTickAcc>=ticksPerStep()){ clkTickAcc=0; } }
        else if(t==midi::Start || t==midi::Continue){ clkTickAcc=0; clkLastMs=millis(); }
        else if(t==midi::Stop){ clkTickAcc=0; }
      }
    }
  #endif
  static void arpReset(const uint8_t chord[],uint8_t chordLen){
    memcpy(arpNotes,chord,chordLen); arpLen=chordLen; arpIdx=0; arpDir=1;
    arpPrevNote=255; nextArpMs=millis(); lastRootForArp=chord[0];
    oiLow=0; oiHigh=arpLen?arpLen-1:0; oiTakeLow=true; rwPos=0;
    #if FEAT_CLOCK_IN && FEAT_ARP_DIV
      clkTickAcc=0;
    #endif
  }
  static uint8_t arpNextIndex(){
    switch(G.arpStyle){
      case ARP_UP:{uint8_t i=arpIdx; arpIdx=(arpIdx+1)%arpLen; return i;}
      case ARP_DOWN:{uint8_t i=arpIdx; arpIdx=(arpIdx==0?arpLen-1:arpIdx-1); return i;}
      case ARP_PINGPONG:{uint8_t i=arpIdx; arpIdx+=arpDir; if(arpIdx>=arpLen){arpIdx=arpLen-1; arpDir=-1;} else if(arpIdx<0){arpIdx=0; arpDir=+1;} return i;}
      case ARP_UPDOWN:{uint8_t i=arpIdx; arpIdx+=arpDir; if(arpIdx>=arpLen){arpIdx=arpLen-2; arpDir=-1;} else if(arpIdx<0){arpIdx=1; arpDir=+1;} return i;}
      case ARP_OUTSIDE_IN:{ if(oiLow>oiHigh){oiLow=0; oiHigh=arpLen-1; oiTakeLow=true;} uint8_t i=oiTakeLow?oiLow++:oiHigh--; oiTakeLow=!oiTakeLow; return i;}
      case ARP_RANDOM: return (uint8_t)random(0,arpLen);
      case ARP_RANDOM_WALK:{ if(arpLen<=1) return 0; int step=(random(0,2)==0)?-1:+1; rwPos+=step; if(rwPos<0)rwPos=1; if(rwPos>=arpLen)rwPos=arpLen-2; return (uint8_t)rwPos;}
      default:return 0;
    }
  }
  static void arpNoteOffPrev(){ if(arpPrevNote!=255){ MIDIIF.sendNoteOff(arpPrevNote,0,MIDI_CH); arpPrevNote=255; } }
  static void arpStep(uint8_t vel){ uint8_t idx=arpNextIndex(); uint8_t n=arpNotes[idx]; arpNoteOffPrev(); MIDIIF.sendNoteOn(n,vel,MIDI_CH); arpPrevNote=n; }
#endif

//============================= TOUCH ==========================================
static inline void setAllHiZ(){ pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); }
static inline void prepReadX(){ pinMode(PIN_XP,OUTPUT); digitalWrite(PIN_XP,HIGH); pinMode(PIN_XM,OUTPUT); digitalWrite(PIN_XM,LOW); pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT); delayMicroseconds(25); }
static inline void prepReadY(){ pinMode(PIN_YP,OUTPUT); digitalWrite(PIN_YP,HIGH); pinMode(PIN_YM,OUTPUT); digitalWrite(PIN_YM,LOW); pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT); delayMicroseconds(25); }
static inline uint16_t readAxisAvg(bool axisX){ uint32_t acc=0; if(axisX){ for(uint8_t i=0;i<SAMPLES;i++){ prepReadX(); (void)analogRead(PIN_YP); acc+=analogRead(PIN_YP);} } else { for(uint8_t i=0;i<SAMPLES;i++){ prepReadY(); (void)analogRead(PIN_XP); acc+=analogRead(PIN_XP);} } return (uint16_t)(acc/SAMPLES); }
static inline uint16_t pullProbeX(){ prepReadX(); uint16_t b=analogRead(PIN_YP); pinMode(PIN_YP,INPUT); digitalWrite(PIN_YP,HIGH); delayMicroseconds(25); uint16_t p=analogRead(PIN_YP); digitalWrite(PIN_YP,LOW); return (p>b)?(p-b):(b-p); }
static inline uint16_t pullProbeY(){ prepReadY(); uint16_t b=analogRead(PIN_XP); pinMode(PIN_XP,INPUT); digitalWrite(PIN_XP,HIGH); delayMicroseconds(25); uint16_t p=analogRead(PIN_XP); digitalWrite(PIN_XP,LOW); return (p>b)?(p-b):(b-p); }
static inline bool touchingXY(uint16_t rx,uint16_t ry){
  if(rx<=TOUCH_NOISE || rx>=ADC_MAX-TOUCH_NOISE) return false;
  if(ry<=TOUCH_NOISE || ry>=ADC_MAX-TOUCH_NOISE) return false;
  if(pullProbeX()>PULL_DELTA) return false;
  if(pullProbeY()>PULL_DELTA) return false;
  return true;
}
static inline uint8_t map127(uint16_t raw,uint16_t mn,uint16_t mx,bool inv){
  if(mx<=mn+1) return inv?0:127;
  if(raw<mn) raw=mn; if(raw>mx) raw=mx;
  uint16_t span=mx-mn; uint16_t pos=raw-mn;
  uint8_t v=(uint8_t)((uint32_t)pos*127UL/span);
  return inv?(uint8_t)(127-v):v;
}

//============================= MIDI WRAPPERS ==================================
static inline void midiCC(uint8_t cc,uint8_t val){ MIDIIF.sendControlChange(cc,val,MIDI_CH); }
static inline void midiNoteOn(uint8_t n,uint8_t v){ MIDIIF.sendNoteOn(n,v,MIDI_CH); }
static inline void midiNoteOff(uint8_t n){ MIDIIF.sendNoteOff(n,0,MIDI_CH); }
static inline void midiChanPressure(uint8_t v){ MIDIIF.sendAfterTouch(v,MIDI_CH); }
static inline void startupBeep(){ midiCC(123,0); midiNoteOn(60,100); delay(100); midiNoteOff(60); }

#if FEAT_SYSEX
static void sendIdentityReply(){
  const uint8_t reply[] = { 0x7E, 0x10, 0x06, 0x02, 0x7D, 0x00,0x01, 0x00,0x01, 0x00,0x00,0x01,0x00 };
  MIDIIF.sendSysEx(sizeof(reply), reply, false);
}
static void handleSysEx(byte* data, unsigned size){
  if (size>=5 && data[0]==0x7E && data[2]==0x06 && data[3]==0x01){ sendIdentityReply(); }
}
#endif

//============================= UI =============================================
static void uiBegin(){
#if defined(DISP_LCD1602)
  lcd.init(); lcd.backlight(); lcd.noCursor(); lcd.noBlink();
#else
  u8x8.begin(); u8x8.setPowerSave(0); u8x8.setFont(u8x8_font_5x7_f);
#endif
}
static void uiClear(){
#if defined(DISP_LCD1602)
  lcd.clear();
#else
  u8x8.clearDisplay();
#endif
}
static void uiPrint(uint8_t c,uint8_t r,const __FlashStringHelper* s){
#if defined(DISP_LCD1602)
  lcd.setCursor(c,r); lcd.print(s);
#else
  char b[17]; strncpy_P(b,(PGM_P)s,16); b[16]=0; u8x8.drawString(c,r,b);
#endif
}
static void uiPrintDyn(uint8_t c,uint8_t r,const char* s){
#if defined(DISP_LCD1602)
  lcd.setCursor(c,r); lcd.print(s);
#else
  u8x8.drawString(c,r,s);
#endif
}
static void noteName(uint8_t n,char* out,uint8_t k){ static const char* nm[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; uint8_t pc=n%12; int8_t oc=(int8_t)n/12 - 1; snprintf(out,k,"%s%d",nm[pc],oc); }

static void renderStatus(uint8_t ccxLive,uint8_t ccyLive,bool noteOn,uint8_t note,uint8_t vel){
  uiClear();
  uiPrint(0,0, G.notesMode ? L("MIDI Ноты","MIDI Notes") : L("MIDI CC","MIDI CC"));
  if (G.notesMode){
    char r[16], lo[8], hi[8]; noteName(G.noteMin,lo,sizeof(lo)); noteName(G.noteMax,hi,sizeof(hi));
    snprintf(r,sizeof(r), UI_LANG_RU? "Диап:%s-%s" : "Range:%s-%s", lo,hi); uiPrintDyn(0,1,r);
    char b[16]; if(noteOn){ char nm[8]; noteName(note,nm,sizeof(nm)); snprintf(b,sizeof(b), "%s/%3u", nm, vel);} else strcpy(b,"--/--");
    uiPrintDyn(0,3,b);
#if FEAT_ARP || FEAT_CHORD
    uiPrint(0,5, L("D7=Акк  D4=Арп","D7=Chord D4=Arp"));
#else
    uiPrint(0,5, L("Держи ENC 5с","Hold ENC 5s"));
#endif
  } else {
    char name[10];
#if FEAT_LABELS
    strncpy(name,G.labels[G.lastPair],sizeof(name)-1); name[sizeof(name)-1]=0;
#else
    const char* dflt[10]={"Mod/Ex","Vol/Pn","Cut/Res","Atk/Rel","Rev/Cho","FootBr","PortOn","Var/Cu","TremPh","DetCho"};
    strncpy(name,dflt[G.lastPair],sizeof(name)-1); name[sizeof(name)-1]=0;
#endif
    char l1[16]; snprintf(l1,sizeof(l1), UI_LANG_RU? "Пара#%u %s":"Pair#%u %s",(unsigned)G.lastPair,name); uiPrintDyn(0,1,l1);
    char b[16]; snprintf(b,sizeof(b), "CC%3u/CC%3u", ccxLive, ccyLive); uiPrintDyn(0,3,b);
    uiPrint(0,5, L("Держи ENC 5с","Hold ENC 5s"));
  }
}

//============================= MENU ===========================================
enum UiField:uint8_t{
  F_MODE, F_PAIR,
#if FEAT_LABELS
  F_LABEL,
#endif
#if FEAT_CC_EDIT
  F_CCX, F_CCY,
#endif
  F_NOTE_MIN, F_NOTE_MAX, F_SCALE,
#if FEAT_ARP
  F_ARPSTYLE,
#endif
#if FEAT_ARP_DIV
  F_DIVISION,
#endif
#if FEAT_TEMPO
  F_TEMPO,
#endif
#if FEAT_CLOCK_IN
  F_CLKIN,
#endif
#if FEAT_CHORD
  F_CHLEN,
#endif
#if FEAT_LED
  F_LED_ENABLE, F_LED_BRIGHT,
#endif
#if FEAT_PROFILES
  F_PROF_IDX, F_PROF_LOAD, F_PROF_SAVE,
#endif
  F_INVY, F_CALNOW, F__COUNT
};
static UiField cursor=F_MODE;

#if FEAT_LABELS
static uint8_t labelPos=0;
static const char CHARSET[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_/+#&:.,";
static int  charsetIndex(char c){ const char* p=strchr(CHARSET,c?c:' '); return p? (int)(p-CHARSET) : 0; }
static char charsetStep (char c,int step){ int n=(int)strlen(CHARSET); int i=charsetIndex(c); i=(i+step)%n; if(i<0)i+=n; return CHARSET[i]; }
#endif

static void renderMenu(){
  uiClear();
  uiPrint(0,0, L("МЕНЮ (D9/D10)","MENU (D9/D10)"));
  const __FlashStringHelper* lab =
    (cursor==F_MODE)      ? L("Режим","Mode") :
    (cursor==F_PAIR)      ? L("Пара CC","CC Pair") :
#if FEAT_LABELS
    (cursor==F_LABEL)     ? L("Метка","Label") :
#endif
#if FEAT_CC_EDIT
    (cursor==F_CCX)       ? L("CC X","CC X") :
    (cursor==F_CCY)       ? L("CC Y","CC Y") :
#endif
    (cursor==F_NOTE_MIN)  ? L("Мин нота","Note Min") :
    (cursor==F_NOTE_MAX)  ? L("Макс нота","Note Max") :
    (cursor==F_SCALE)     ? L("Лад","Scale") :
#if FEAT_ARP
    (cursor==F_ARPSTYLE)  ? L("Арпеджио","Arp Style") :
#endif
#if FEAT_ARP_DIV
    (cursor==F_DIVISION)  ? L("Деление","Division") :
#endif
#if FEAT_TEMPO
    (cursor==F_TEMPO)     ? L("Темп","Tempo") :
#endif
#if FEAT_CLOCK_IN
    (cursor==F_CLKIN)     ? L("Clock Вх","Clock In") :
#endif
#if FEAT_CHORD
    (cursor==F_CHLEN)     ? L("Дл. акк","Chord Len") :
#endif
#if FEAT_LED
    (cursor==F_LED_ENABLE)? L("LED Вкл","LED Enable") :
    (cursor==F_LED_BRIGHT)? L("LED Ярк","LED Bright") :
#endif
#if FEAT_PROFILES
    (cursor==F_PROF_IDX)  ? L("Профиль #","Profile #") :
    (cursor==F_PROF_LOAD) ? L("Загрузить","Load") :
    (cursor==F_PROF_SAVE) ? L("Сохранить","Save") :
#endif
    (cursor==F_INVY)      ? L("Инверт Y","Invert Y") :
    (cursor==F_CALNOW)    ? L("Калибровка","Calibrate") :
    L("","");
  uiPrint(0,2, lab);

  char val[16];
  if(cursor==F_MODE) snprintf(val,sizeof(val), G.notesMode ? (UI_LANG_RU?"НОТЫ":"NOTES") : "CC");
  else if(cursor==F_PAIR){ char n[10];
#if FEAT_LABELS
    strncpy(n,G.labels[G.lastPair],sizeof(n)-1); n[sizeof(n)-1]=0;
#else
    const char* dflt[10]={"Mod/Ex","Vol/Pn","Cut/Res","Atk/Rel","Rev/Cho","FootBr","PortOn","Var/Cu","TremPh","DetCho"}; strncpy(n,dflt[G.lastPair],sizeof(n)-1); n[sizeof(n)-1]=0;
#endif
    snprintf(val,sizeof(val), "#%u %s",(unsigned)G.lastPair,n);
  }
#if FEAT_LABELS
  else if(cursor==F_LABEL){ strncpy(val,G.labels[G.lastPair],sizeof(val)-1); val[sizeof(val)-1]=0; }
#endif
#if FEAT_CC_EDIT
  else if(cursor==F_CCX){ snprintf(val,sizeof(val),"%u", (unsigned)pairX(G.lastPair)); }
  else if(cursor==F_CCY){ snprintf(val,sizeof(val),"%u", (unsigned)pairY(G.lastPair)); }
#endif
  else if(cursor==F_NOTE_MIN){ char nm[8]; noteName(G.noteMin,nm,sizeof(nm)); snprintf(val,sizeof(val), "%u(%s)", G.noteMin, nm); }
  else if(cursor==F_NOTE_MAX){ char nm[8]; noteName(G.noteMax,nm,sizeof(nm)); snprintf(val,sizeof(val), "%u(%s)", G.noteMax, nm); }
  else if(cursor==F_SCALE){ char nm[10]; pstr(nm,(PGM_P)pgm_read_word(&SC_NAMES[G.scaleIdx%SC__N]),sizeof(nm)); snprintf(val,sizeof(val), "%s", nm); }
#if FEAT_ARP
  else if(cursor==F_ARPSTYLE){ const char* nm[]={"Up","Down","PingP","UpDwn","OutIn","Rand","Walk"}; snprintf(val,sizeof(val),"%s", nm[G.arpStyle%7]); }
#endif
#if FEAT_ARP_DIV
  else if(cursor==F_DIVISION){ const char* lb[]={"1/4","1/8","1/16","1/3","1/6"}; snprintf(val,sizeof(val),"%s", lb[G.divIndex%5]); }
#endif
#if FEAT_TEMPO
  else if(cursor==F_TEMPO){ snprintf(val,sizeof(val), "%u BPM", (unsigned)G.tempoBPM); }
#endif
#if FEAT_CLOCK_IN
  else if(cursor==F_CLKIN){ snprintf(val,sizeof(val), G.clockIn? (UI_LANG_RU?"Вкл":"On"):(UI_LANG_RU?"Выкл":"Off")); }
#endif
#if FEAT_CHORD
  else if(cursor==F_CHLEN){ snprintf(val,sizeof(val), "%u", (unsigned)G.chordLen); }
#endif
#if FEAT_LED
  else if(cursor==F_LED_ENABLE){ snprintf(val,sizeof(val), G.ledEnable? (UI_LANG_RU?"Да":"Yes"):(UI_LANG_RU?"Нет":"No")); }
  else if(cursor==F_LED_BRIGHT){ snprintf(val,sizeof(val), "%u", (unsigned)G.ledBrightness); }
#endif
#if FEAT_PROFILES
  else if(cursor==F_PROF_IDX){ snprintf(val,sizeof(val), "%u", (unsigned)profIndex); }
  else if(cursor==F_PROF_LOAD){ snprintf(val,sizeof(val), UI_LANG_RU ? "Нажми ENC" : "Press ENC"); }
  else if(cursor==F_PROF_SAVE){ snprintf(val,sizeof(val), UI_LANG_RU ? "Нажми ENC" : "Press ENC"); }
#endif
  else if(cursor==F_INVY){ snprintf(val,sizeof(val), G.invY?(UI_LANG_RU?"Да":"Yes"):(UI_LANG_RU?"Нет":"No")); }
  else if(cursor==F_CALNOW){ snprintf(val,sizeof(val), calOK()? (UI_LANG_RU?"Есть":"OK") : (UI_LANG_RU?"Нужна":"Need")); }
  else val[0]=0;

  uiPrintDyn(0,3,val);
  uiPrint(0,6, L("ENC:изм  D9:->","ENC:chg D9:->"));
  uiPrint(0,7, L("Долгий ENC:выход","Hold ENC:exit"));
}

//============================= CALIB/NOTES ====================================
enum State:uint8_t{RUN, CAL_WAIT, CAL_RUN, MENU, LABEL_EDIT};
static State st=RUN;
static uint16_t calXmin,calXmax,calYmin,calYmax; static uint32_t calEndTs=0;
static void startCalWait(){ st=CAL_WAIT; uiClear(); uiPrint(0,0,L("Калибр. 10с","Calib 10s")); uiPrint(0,1,L("Коснитесь и","Touch and")); uiPrint(0,2,L("ведите LB->RB","move LB->RB")); uiPrint(0,3,L("      RT->LT","     RT->LT")); }
static void startCalRun(){ st=CAL_RUN; calXmin=0xFFFF; calYmin=0xFFFF; calXmax=0; calYmax=0; calEndTs=millis()+10000UL; uiPrint(0,5,L("Не отрывать","Don't lift")); }
static void finishCal(){ if (calXmax<=calXmin+10 || calYmax<=calYmin+10){ G.calXmin=100; G.calXmax=900; G.calYmin=100; G.calYmax=900; } else { G.calXmin=calXmin; G.calXmax=calXmax; G.calYmin=calYmin; G.calYmax=calYmax; } savePersist(); st=RUN; }

static uint8_t quantizeToScale(uint8_t note){
  uint8_t tonic=G.noteMin%12; const int8_t* st=SC_STEPS[G.scaleIdx%SC__N]; uint8_t cnt=SC_LEN[G.scaleIdx%SC__N];
  int bestN=note, bestD=127;
  for(int oc=-2; oc<=2; ++oc){ int base=(note/12 + oc)*12 + tonic; for(uint8_t i=0;i<cnt;i++){ int cand=base+st[i]; int d=abs(cand-(int)note); if(d<bestD){ bestD=d; bestN=cand; } } }
  if(bestN<G.noteMin) bestN=G.noteMin; if(bestN>G.noteMax) bestN=G.noteMax; return (uint8_t)bestN;
}
#if FEAT_CHORD
static int findDegreeIdx(uint8_t pc,uint8_t tonic,const int8_t* st,uint8_t cnt){ for(uint8_t i=0;i<cnt;i++){ if(((tonic+st[i])%12)==pc) return i; } return -1; }
static void buildChord(uint8_t root,uint8_t chord[],uint8_t chordLen){
  const int8_t* st=SC_STEPS[G.scaleIdx%SC__N]; uint8_t cnt=SC_LEN[G.scaleIdx%SC__N]; uint8_t tonic=G.noteMin%12;
  uint8_t rpc=root%12; int r=findDegreeIdx(rpc,tonic,st,cnt); if(r<0){ root=quantizeToScale(root); rpc=root%12; r=findDegreeIdx(rpc,tonic,st,cnt); }
  chord[0]=root; int prev=st[r], cur=root, idx=r;
  for(uint8_t k=1;k<chordLen;k++){ idx=(idx+2)%cnt; int step=st[idx]; int d=step-prev; if(d<=0)d+=12; cur+=d; chord[k]=(uint8_t)constrain(cur,0,127); prev=step; }
}
#endif

//============================= SETUP ==========================================
void setup(){
  pinMode(ENC_A,INPUT_PULLUP); pinMode(ENC_B,INPUT_PULLUP); pinMode(ENC_BTN,INPUT_PULLUP);
  pinMode(BTN_NEXT,INPUT_PULLUP); pinMode(BTN_SAVE,INPUT_PULLUP);
  pinMode(BTN_CHORD,INPUT_PULLUP); pinMode(BTN_ARP,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR_A, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encISR_B, CHANGE);

  uiBegin();

#if PLATFORM_RP2040 && defined(MIDI_BACKEND_USB)
  usb_midi.begin();
#endif
#if PLATFORM_RP2040 && defined(MIDI_BACKEND_DIN)
  Serial1.begin(31250);
#else
  Serial.begin(31250);
#endif
  MIDIIF.begin(MIDI_CHANNEL_OMNI); MIDIIF.turnThruOff();
#if FEAT_SYSEX
  MIDIIF.setHandleSystemExclusive(handleSysEx);
#endif

#if FEAT_LED && LED_HAS_HW
  pinMode(LED_PIN,OUTPUT); analogWrite(LED_PIN, 0);
#endif

  startupBeep();
  loadPersist();
  if(!calOK()) st=CAL_WAIT; else st=RUN;
  setAllHiZ();
  renderStatus(0,0,false,0,0);
}

//============================= LOOP ===========================================
void loop(){
  if (btnFalling(bEnc)) { encPressTs=millis(); encRotDuringHold=false; }
  if (btnRising(bEnc)){
    uint32_t dt=millis()-encPressTs;
#if FEAT_LABELS
    if (st==LABEL_EDIT){ if (dt>1500){ savePersist(); st=MENU; renderMenu(); } }
    else
#endif
    if (dt>LONG_PRESS_MS && !encRotDuringHold){
      if (st==RUN){ st=MENU; renderMenu(); }
      else if (st==MENU){ savePersist(); st=RUN; renderStatus(0,0,false,0,0); }
    } else if (st==MENU){
      if (cursor==F_CALNOW){ startCalWait(); }
#if FEAT_LABELS
      if (cursor==F_LABEL){ st=LABEL_EDIT; labelPos=0; uiClear(); uiPrint(0,0,L("Ред. метки","Edit label")); }
#endif
#if FEAT_PROFILES
      if (cursor==F_PROF_LOAD){ if(profLoad(profIndex)) renderMenu(); }
      if (cursor==F_PROF_SAVE){ profSave(profIndex); renderMenu(); }
#endif
    }
  }

  if (st==MENU){
    if (btnFalling(bNext)){ cursor=(UiField)((cursor+1)%F__COUNT); renderMenu(); }
    if (btnFalling(bSave)){ savePersist(); renderMenu(); }
  }

  int16_t d = takeEnc();
  if (d){
    if (digitalRead(ENC_BTN)==LOW) encRotDuringHold=true;

#if FEAT_LABELS
    if (st==LABEL_EDIT){
      int step=(d>0)?+1:-1;
      char c=G.labels[G.lastPair][labelPos];
      c=charsetStep(c?c:' ',step);
      G.labels[G.lastPair][labelPos]=c;
      char buf[17]; snprintf(buf,sizeof(buf),"#%u: %-8s",(unsigned)G.lastPair,G.labels[G.lastPair]);
      uiPrintDyn(0,2,buf);
    } else
#endif
    if (st==MENU){
      switch(cursor){
        case F_MODE:      G.notesMode = (d>0); break;
        case F_PAIR:      { int v=(int)G.lastPair + (d>0?+1:-1); if(v<0)v=9; if(v>9)v=0; G.lastPair=(uint8_t)v; } break;
#if FEAT_CC_EDIT
        case F_CCX:       { int v=(int)pairX(G.lastPair) + (d>0?+1:-1); v=constrain(v,0,127); setPairX(G.lastPair,(uint8_t)v);} break;
        case F_CCY:       { int v=(int)pairY(G.lastPair) + (d>0?+1:-1); v=constrain(v,0,127); setPairY(G.lastPair,(uint8_t)v);} break;
#endif
        case F_NOTE_MIN:  { int step = (digitalRead(ENC_BTN)==LOW)?12:1; int v=(int)G.noteMin + (d>0?step:-step); v=constrain(v,0,(int)G.noteMax); G.noteMin=(uint8_t)v; } break;
        case F_NOTE_MAX:  { int step = (digitalRead(ENC_BTN)==LOW)?12:1; int v=(int)G.noteMax + (d>0?step:-step); v=constrain(v,(int)G.noteMin,127); G.noteMax=(uint8_t)v; } break;
        case F_SCALE:     { int v=(int)G.scaleIdx + (d>0?+1:-1); if(v<0)v=SC__N-1; if(v>=SC__N)v=0; G.scaleIdx=(uint8_t)v; } break;
#if FEAT_ARP
        case F_ARPSTYLE:  { int v=(int)G.arpStyle + (d>0?+1:-1); if(v<0)v=ARP__COUNT-1; if(v>=ARP__COUNT)v=0; G.arpStyle=(uint8_t)v; } break;
#endif
#if FEAT_ARP_DIV
        case F_DIVISION:  { int v=(int)G.divIndex + (d>0?+1:-1); if(v<0)v=DIV__COUNT-1; if(v>=DIV__COUNT)v=0; G.divIndex=(uint8_t)v; } break;
#endif
#if FEAT_TEMPO
        case F_TEMPO:     { int v=(int)G.tempoBPM + (d>0?+1:-1); v=constrain(v,40,240); G.tempoBPM=(uint16_t)v; } break;
#endif
#if FEAT_CLOCK_IN
        case F_CLKIN:     G.clockIn=(d>0); break;
#endif
#if FEAT_CHORD
        case F_CHLEN:     { int v=(int)G.chordLen + (d>0?+1:-1); v=constrain(v,3,4); G.chordLen=(uint8_t)v; } break;
#endif
#if FEAT_LED
        case F_LED_ENABLE:G.ledEnable=(d>0);
          #if LED_HAS_HW
            analogWrite(LED_PIN, G.ledEnable? map(G.ledBrightness,0,60,0,255):0);
          #endif
          break;
        case F_LED_BRIGHT:{ int v=(int)G.ledBrightness + (d>0?+1:-1); v=constrain(v,0,60); G.ledBrightness=(uint8_t)v;
          #if LED_HAS_HW
            if(G.ledEnable) analogWrite(LED_PIN, map(G.ledBrightness,0,60,0,255));
          #endif
        } break;
#endif
#if FEAT_PROFILES
        case F_PROF_IDX:  { int v=(int)profIndex + (d>0?+1:-1); if(v<0)v=9; if(v>9)v=0; profIndex=(uint8_t)v; } break;
        case F_PROF_LOAD: break;
        case F_PROF_SAVE: break;
#endif
        case F_INVY:      G.invY=(d>0); break;
        case F_CALNOW:    break;
        default: break;
      }
      renderMenu();
    } else if (st==RUN){
      if (G.notesMode){
        int step=(digitalRead(ENC_BTN)==LOW)?12:1;
        int shift=(d>0?step:-step);
        int nmin=(int)G.noteMin+shift, nmax=(int)G.noteMax+shift;
        if(nmin<0){ nmax-=nmin; nmin=0; }
        if(nmax>127){ nmin-=(nmax-127); nmax=127; if(nmin<0)nmin=0; }
        G.noteMin=(uint8_t)nmin; G.noteMax=(uint8_t)nmax;
      } else {
        int v=(int)G.lastPair + (d>0?+1:-1); if(v<0)v=9; if(v>9)v=0; G.lastPair=(uint8_t)v;
      }
    }
  }

  uint16_t rx=readAxisAvg(true), ry=readAxisAvg(false);

  if (st==CAL_WAIT){ if(touchingXY(rx,ry)) startCalRun(); return; }
  if (st==CAL_RUN){
    if (touchingXY(rx,ry)){
      if (rx<calXmin) calXmin=rx; if (rx>calXmax) calXmax=rx;
      if (ry<calYmin) calYmin=ry; if (ry>calYmax) calYmax=ry;
    }
    if (millis()>=calEndTs){ finishCal(); renderStatus(0,0,false,0,0); }
    return;
  }

  static uint8_t lastCCx=255,lastCCy=255;
  static uint8_t curNote=255;

#if FEAT_ARP && FEAT_CLOCK_IN
  if (G.clockIn) midiPollClockIn();
#endif

  bool touch=touchingXY(rx,ry);
  uint8_t x127=map127(rx,G.calXmin,G.calXmax,false);
  uint8_t y127=map127(ry,G.calYmin,G.calYmax,G.invY);

  if (!G.notesMode){
    if (touch){
      uint8_t ccx=x127, ccy=y127;
      if(lastCCx==255 || (uint8_t)abs((int)ccx-(int)lastCCx)>=1){ midiCC(pairX(G.lastPair),ccx); lastCCx=ccx; }
      if(lastCCy==255 || (uint8_t)abs((int)ccy-(int)lastCCy)>=1){ midiCC(pairY(G.lastPair),ccy); lastCCy=ccy; }
      renderStatus(lastCCx,lastCCy,false,0,0);
    } else {
      renderStatus(lastCCx,lastCCy,false,0,0);
    }
  } else {
    uint8_t rootLin = G.noteMin + (uint8_t)((uint16_t)x127 * (G.noteMax - G.noteMin) / 127U);
    uint8_t root    = quantizeToScale(rootLin);
    uint8_t vel     = (uint8_t)max(1,(int)y127);

    bool arpHold =
    #if FEAT_ARP
      btnDown(bArp)
    #else
      false
    #endif
    ;
    bool chordHold =
    #if FEAT_CHORD
      btnDown(bChord)
    #else
      false
    #endif
    ;

#if FEAT_ARP
    if (arpHold && touch){
      uint8_t chord[4]={0};
  #if FEAT_CHORD
      buildChord(root,chord,G.chordLen); uint8_t useLen=G.chordLen;
  #else
      chord[0]=root; chord[1]=root+4; chord[2]=root+7; uint8_t useLen=3;
  #endif
      if(lastRootForArp!=root){ arpReset(chord,useLen); }
      arpActive=true;
  #if FEAT_CLOCK_IN && FEAT_ARP_DIV
      if (G.clockIn){
        static uint8_t prevAcc=0;
        if (clkTickAcc!=prevAcc){ if (clkTickAcc==0){ arpStep(vel); } prevAcc=clkTickAcc; }
      } else
  #endif
  #if FEAT_TEMPO && FEAT_ARP_DIV
      { if (millis()>=nextArpMs){ nextArpMs=millis()+msPerStep(); arpStep(vel); } }
  #endif
      uint8_t shown=(arpPrevNote==255)?root:arpPrevNote;
      renderStatus(0,0,true,shown,vel);
    } else
#endif
#if FEAT_CHORD
    if (chordHold && touch){
      static bool chordOn=false; static uint8_t prevChord[4]={255,255,255,255};
      uint8_t chord[4]={0}; buildChord(root,chord,G.chordLen);
      if (!chordOn){ for(uint8_t i=0;i<G.chordLen;i++) midiNoteOn(chord[i],vel); memcpy(prevChord,chord,4); chordOn=true; }
      else {
        if (memcmp(prevChord,chord,G.chordLen)!=0){
          for(uint8_t i=0;i<G.chordLen;i++) midiNoteOff(prevChord[i]);
          for(uint8_t i=0;i<G.chordLen;i++) midiNoteOn(chord[i],vel);
          memcpy(prevChord,chord,4);
        } else { midiChanPressure(vel); }
      }
      renderStatus(0,0,true,root,vel);
      if(!touch){ for(uint8_t i=0;i<G.chordLen;i++) midiNoteOff(prevChord[i]); chordOn=false; }
    } else
#endif
    if (touch){
#if FEAT_ARP
      if(arpActive){ arpActive=false; arpNoteOffPrev(); }
#endif
      if (curNote==255){ midiNoteOn(root,vel); curNote=root; }
      else if (root!=curNote){ midiNoteOff(curNote); midiNoteOn(root,vel); curNote=root; }
      else { midiChanPressure(vel); }
      renderStatus(0,0,true,curNote,vel);
    } else {
#if FEAT_ARP
      if(arpActive){ arpActive=false; arpNoteOffPrev(); }
#endif
      if (curNote!=255){ midiNoteOff(curNote); curNote=255; }
      renderStatus(0,0,false,0,0);
    }
  }
}