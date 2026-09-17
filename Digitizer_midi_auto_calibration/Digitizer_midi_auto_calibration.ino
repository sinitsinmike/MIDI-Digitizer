// File: touch_to_midi_buildtoggle.ino
//
// ─────────────────────────────────────────────────────────────────────────────
// 4-wire resistive touch → MIDI. КАЛИБРОВКА = 10 СЕКУНД.
// Калибровка проводится движением пальца без отрыва: LB → RB → RT → LT.
// ─────────────────────────────────────────────────────────────────────────────
// BUILD-TOGGLE:
//   #define MIDI_NOTES_MODE 1   // ← включить режим НОТ
//   // #define MIDI_NOTES_MODE 1 // ← закомментировать для режима CC
// ─────────────────────────────────────────────────────────────────────────────

// ===== BUILD TOGGLE =====
#define MIDI_NOTES_MODE 1   // 0 = CC mode (X→CC74, Y→CC71), 1 = Notes mode (X→Note, Y→Velocity)

#include <Arduino.h>
#include <MIDI.h>
#if defined(ARDUINO_ARCH_RP2040)
  #include <Adafruit_TinyUSB.h> // USB-MIDI only for RP2040
#endif

// ── WIRING (X = длинная сторона 2–4; Y = короткая 1–3). В КАЖДУЮ линию 470–1000 Ω (включая A0/A1)!
static const uint8_t PIN_XP = A0;  // ← контакт 2 (через резистор)
static const uint8_t PIN_YP = A1;  // ← контакт 1 (через резистор)
static const uint8_t PIN_XM = 5;   // ← контакт 4 (через резистор)
static const uint8_t PIN_YM = 6;   // ← контакт 3 (через резистор)
static const uint8_t BTN_PIN = 7;  // короткое=калибровка; длинное(>0.8s)=инверт Y

#if defined(ARDUINO_ARCH_RP2040)
  static const uint16_t ADC_MAX = 4095;
#else
  static const uint16_t ADC_MAX = 1023;
#endif

// ── MIDI ──
static const uint8_t  MIDI_CH   = 1;
static const uint8_t  MIDI_CC_X = 74;
static const uint8_t  MIDI_CC_Y = 71;
static const uint8_t  CC_DELTA  = 1;

#if defined(ARDUINO_ARCH_RP2040)
  static const int DIN_TX_PIN = 4; // Serial1 TX pin (RP2040) for DIN out
#endif

// Стартовая нота-индикатор
static const uint8_t  START_NOTE = 60; // C4
static const uint8_t  START_VEL  = 100;
static const uint16_t START_MS   = 120;

// ── Notes-mode config ──
#if MIDI_NOTES_MODE
static const uint8_t NOTE_MIN = 48;   // C3
static const uint8_t NOTE_MAX = 72;   // C5 inclusive
static const uint8_t VEL_MIN  = 1;
static const uint8_t VEL_DELTA = 3;   // Threshold for Channel Pressure updates
#endif

// ── Touch/Calib ──
static const uint8_t  SAMPLES_PER_AXIS = 6;
static const uint16_t TOUCH_NOISE = ADC_MAX / 70;
static const uint32_t LOOP_MS = 2;
static const uint32_t CAL_WINDOW_MS = 10000;     // 10 s
static const uint16_t PULL_DELTA = ADC_MAX / 8;  // pull-probe threshold

// Orientation (runtime invert via button)
static bool invertX = false;
static bool invertY = false;

// ── State ──
enum State : uint8_t { CAL_WAIT_TOUCH, CAL_RUNNING, RUN };
State state_ = CAL_WAIT_TOUCH;

uint16_t calXmin, calXmax, calYmin, calYmax;
uint32_t calDeadline = 0, lastLoopTs = 0;

#if !MIDI_NOTES_MODE
  // CC state
  uint8_t  lastCC_X = 255, lastCC_Y = 255;
#else
  // Notes state
  uint8_t  curNote = 255;
  uint8_t  lastVel = 0;
  bool     noteOn  = false;
#endif

// ── MIDI instances ──
#if defined(ARDUINO_ARCH_RP2040)
  Adafruit_USBD_MIDI usb_midi;
  MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, USBMIDI);
#endif
#if defined(ARDUINO_ARCH_RP2040)
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, DINMIDI);
#else
  MIDI_CREATE_INSTANCE(HardwareSerial, Serial,   DINMIDI); // Nano: DIN on Serial (не открывать монитор)
#endif

// ── Utils ──
static inline uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi){ if(v<lo)return lo; if(v>hi)return hi; return v; }
static inline bool midRange(uint16_t s){ return (s>TOUCH_NOISE) && (s<(ADC_MAX-TOUCH_NOISE)); }

static inline void setAllHiZ(){
  pinMode(PIN_XP,INPUT); pinMode(PIN_XM,INPUT);
  pinMode(PIN_YP,INPUT); pinMode(PIN_YM,INPUT);
}

static inline void prepReadX(){ // XP=Vcc, XM=GND, read YP
  pinMode(PIN_XP,OUTPUT); digitalWrite(PIN_XP,HIGH);
  pinMode(PIN_XM,OUTPUT); digitalWrite(PIN_XM,LOW);
  pinMode(PIN_YP,INPUT);  pinMode(PIN_YM,INPUT);
  delayMicroseconds(30);
}
static inline void prepReadY(){ // YP=Vcc, YM=GND, read XP
  pinMode(PIN_YP,OUTPUT); digitalWrite(PIN_YP,HIGH);
  pinMode(PIN_YM,OUTPUT); digitalWrite(PIN_YM,LOW);
  pinMode(PIN_XP,INPUT);  pinMode(PIN_XM,INPUT);
  delayMicroseconds(30);
}

static inline uint16_t readAxisAvg(bool axisX){
  uint32_t acc=0;
  if(axisX){
    for(uint8_t i=0;i<SAMPLES_PER_AXIS;i++){ prepReadX(); (void)analogRead(PIN_YP); delayMicroseconds(8); acc += analogRead(PIN_YP); }
  }else{
    for(uint8_t i=0;i<SAMPLES_PER_AXIS;i++){ prepReadY(); (void)analogRead(PIN_XP); delayMicroseconds(8); acc += analogRead(PIN_XP); }
  }
  return (uint16_t)(acc / SAMPLES_PER_AXIS);
}

static inline uint16_t pullProbeX(){ // sense=YP
  prepReadX(); (void)analogRead(PIN_YP); delayMicroseconds(8);
  uint16_t base = analogRead(PIN_YP);
  pinMode(PIN_YP, INPUT); digitalWrite(PIN_YP, HIGH);
  delayMicroseconds(30); (void)analogRead(PIN_YP); delayMicroseconds(8);
  uint16_t pulled = analogRead(PIN_YP);
  digitalWrite(PIN_YP, LOW);
  return (pulled>base)? (pulled-base) : (base-pulled);
}
static inline uint16_t pullProbeY(){ // sense=XP
  prepReadY(); (void)analogRead(PIN_XP); delayMicroseconds(8);
  uint16_t base = analogRead(PIN_XP);
  pinMode(PIN_XP, INPUT); digitalWrite(PIN_XP, HIGH);
  delayMicroseconds(30); (void)analogRead(PIN_XP); delayMicroseconds(8);
  uint16_t pulled = analogRead(PIN_XP);
  digitalWrite(PIN_XP, LOW);
  return (pulled>base)? (pulled-base) : (base-pulled);
}

static inline float norm01(uint16_t raw, uint16_t mn, uint16_t mx, bool inv){
  if (mx<=mn) return 0.0f;
  raw = clampU16(raw, mn, mx);
  float v = (float)(raw - mn) / (float)(mx - mn);
  return inv ? (1.0f - v) : v;
}
static inline uint8_t toCC(float n01){
  if (n01 <= 0.f) return 0;
  if (n01 >= 1.f) return 127;
  return (uint8_t)(n01 * 127.f + 0.5f);
}

// ── MIDI helpers ──
static inline void midiCC(uint8_t cc, uint8_t val){
#if defined(ARDUINO_ARCH_RP2040)
  USBMIDI.sendControlChange(cc, val, MIDI_CH);
#endif
  DINMIDI.sendControlChange(cc, val, MIDI_CH);
}
#if MIDI_NOTES_MODE
static inline void midiNoteOn(uint8_t note, uint8_t vel){
#if defined(ARDUINO_ARCH_RP2040)
  USBMIDI.sendNoteOn(note, vel, MIDI_CH);
#endif
  DINMIDI.sendNoteOn(note, vel, MIDI_CH);
}
static inline void midiNoteOff(uint8_t note){
#if defined(ARDUINO_ARCH_RP2040)
  USBMIDI.sendNoteOff(note, 0, MIDI_CH);
#endif
  DINMIDI.sendNoteOff(note, 0, MIDI_CH);
}
static inline void midiChanPressure(uint8_t val){
#if defined(ARDUINO_ARCH_RP2040)
  USBMIDI.sendAfterTouch(val, MIDI_CH);
#endif
  DINMIDI.sendAfterTouch(val, MIDI_CH);
}
#endif

static inline void startupBeep(){ // why: подтверждение тракта
#if MIDI_NOTES_MODE
  midiNoteOn(60, START_VEL); delay(START_MS); midiNoteOff(60);
#else
  midiCC(123,0); // All Notes Off (safety)
  midiCC(1, 64);
#endif
}

// ── Calibration (10 s) ──
static inline void beginCalWait(){
  state_ = CAL_WAIT_TOUCH;
  calXmin = calYmin = 0xFFFF; calXmax = calYmax = 0;
#if !MIDI_NOTES_MODE
  // reset CC edges
  // (почему: чтобы сразу слать после нового касания)
#endif
}
static inline void beginCalRun(){ state_ = CAL_RUNNING; calDeadline = millis() + CAL_WINDOW_MS; }
static inline void finishCal(){
  state_ = RUN;
  const uint16_t spanMin = ADC_MAX / 10;
  if ((calXmax<=calXmin)||(calYmax<=calYmin)||
      ((calXmax-calXmin)<spanMin)||((calYmax-calYmin)<spanMin)) {
    calXmin = ADC_MAX/20; calXmax = ADC_MAX - ADC_MAX/20;
    calYmin = ADC_MAX/20; calYmax = ADC_MAX - ADC_MAX/20;
  }
}

static inline void handleButton(){ // короткое=калибровка; длинное=инверт Y
  static uint8_t prev=HIGH; static uint32_t t0=0;
  uint8_t now = digitalRead(BTN_PIN);
  if (prev==HIGH && now==LOW){ t0 = millis(); }
  if (prev==LOW && now==HIGH){
    uint32_t dt = millis() - t0;
    if (dt > 800){ invertY = !invertY; } // меняем «верх/низ» при необходимости
    else { beginCalWait(); }
  }
  prev = now;
}

void setup(){
#if defined(ARDUINO_ARCH_RP2040)
  analogReadResolution(12);
#endif
  pinMode(BTN_PIN, INPUT_PULLUP);

#if defined(ARDUINO_ARCH_RP2040)
  USBMIDI.begin(MIDI_CHANNEL_OMNI); USBMIDI.turnThruOff();
  Serial1.setTX(DIN_TX_PIN); Serial1.begin(31250);
#else
  Serial.begin(31250); // Nano: DIN-MIDI на аппаратном UART (Serial Monitor НЕ открывать)
#endif
  DINMIDI.begin(MIDI_CHANNEL_OMNI); DINMIDI.turnThruOff();

  startupBeep();

  setAllHiZ();
  calXmin = ADC_MAX/20; calXmax = ADC_MAX - ADC_MAX/20;
  calYmin = ADC_MAX/20; calYmax = ADC_MAX - ADC_MAX/20;
  beginCalWait();
}

void loop(){
  handleButton();

  // Read both axes + probes → robust touch detect
  uint16_t rawX = readAxisAvg(true);
  uint16_t rawY = readAxisAvg(false);
  uint16_t dX   = pullProbeX();
  uint16_t dY   = pullProbeY();
  bool touching = midRange(rawX) && midRange(rawY) && (dX < PULL_DELTA) && (dY < PULL_DELTA);

  if (state_ == CAL_WAIT_TOUCH){
    if (touching) beginCalRun();
#if defined(ARDUINO_ARCH_RP2040)
    tud_task();
#endif
    setAllHiZ(); delay(1); return;
  }
  if (state_ == CAL_RUNNING){
    if (touching){
      if (rawX<calXmin) calXmin=rawX; if (rawX>calXmax) calXmax=rawX;
      if (rawY<calYmin) calYmin=rawY; if (rawY>calYmax) calYmax=rawY;
    }
    if (millis() >= calDeadline) finishCal();
#if defined(ARDUINO_ARCH_RP2040)
    tud_task();
#endif
    setAllHiZ(); delay(1); return;
  }

  // RUN
  if (millis() - lastLoopTs < LOOP_MS){ setAllHiZ(); return; }
  lastLoopTs = millis();

  if (touching){
    float nx = norm01(rawX, calXmin, calXmax, invertX);
    float ny = norm01(rawY, calYmin, calYmax, invertY);

#if !MIDI_NOTES_MODE
    // CC mode
    static uint8_t lastX = 255, lastY = 255;
    uint8_t ccX = toCC(nx);
    uint8_t ccY = toCC(ny);
    if (lastX==255 || (uint8_t)abs((int)ccX-(int)lastX) >= CC_DELTA){ midiCC(MIDI_CC_X, ccX); lastX = ccX; }
    if (lastY==255 || (uint8_t)abs((int)ccY-(int)lastY) >= CC_DELTA){ midiCC(MIDI_CC_Y, ccY); lastY = ccY; }
#else
    // Notes mode (mono)
    uint8_t note = NOTE_MIN + (uint8_t)round(nx * (NOTE_MAX - NOTE_MIN));
    uint8_t vel  = VEL_MIN + (uint8_t)round(ny * (127 - VEL_MIN));
    if (!noteOn){ midiNoteOn(note, vel); curNote=note; lastVel=vel; noteOn=true; }
    else if (note != curNote){ midiNoteOff(curNote); midiNoteOn(note, vel); curNote=note; lastVel=vel; }
    else { if ((uint8_t)abs((int)vel-(int)lastVel) >= VEL_DELTA){ midiChanPressure(vel); lastVel=vel; } }
#endif

  } else {
#if MIDI_NOTES_MODE
    if (noteOn){ midiNoteOff(curNote); noteOn=false; curNote=255; }
#else
    // сбросим «порог» для следующего касания
    // (держим локально в loop — выше)
#endif
  }

#if defined(ARDUINO_ARCH_RP2040)
  tud_task(); // flush USB
#endif
  setAllHiZ();
}