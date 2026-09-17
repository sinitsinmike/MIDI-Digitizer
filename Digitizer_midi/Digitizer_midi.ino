// File: src/touch_midi_unified.ino
//
// 4-проводный резистивный тач -> MIDI CC (X/Y)
// RP2040: USB-MIDI (TinyUSB) + DIN-MIDI
// AVR (Nano/LGT8F): только DIN-MIDI
//
// Требуемые библиотеки:
//   - MIDI Library by FortySevenEffects (для всех)
//   - Adafruit TinyUSB Library (только если RP2040 и нужен USB-MIDI)

#include <Arduino.h>

// ---------- Фичи по умолчанию ----------
#if defined(ARDUINO_ARCH_RP2040)
  #ifndef USE_USB_MIDI
  #define USE_USB_MIDI 1   // RP2040 умеет USB-MIDI
  #endif
#else
  #ifndef USE_USB_MIDI
  #define USE_USB_MIDI 0   // AVR: TinyUSB недоступен
  #endif
#endif

#ifndef USE_DIN_MIDI
#define USE_DIN_MIDI 1     // Оставляем DIN-MIDI на всех платформах
#endif

#include <MIDI.h>

#if USE_USB_MIDI
  #include <Adafruit_TinyUSB.h>
#endif

// ---------- Пины тач-панели ----------
// X+ и Y+ должны быть на пинах с АЦП
static const uint8_t PIN_XP = A0;
static const uint8_t PIN_YP = A1;
static const uint8_t PIN_XM = 5;  // замените при необходимости
static const uint8_t PIN_YM = 6;  // замените при необходимости
// Последовательно в КАЖДЫЙ из 4 проводов поставьте 470–1000 Ом (защита MCU).

// ---------- Калибровка/параметры ----------
#if defined(ARDUINO_ARCH_RP2040)
  static const uint16_t ADC_MAX = 4095;
#else
  static const uint16_t ADC_MAX = 1023;
#endif

static const uint16_t RAW_X_MIN = ADC_MAX / 20;         // ~5% слева
static const uint16_t RAW_X_MAX = ADC_MAX - ADC_MAX/20; // ~95% справа
static const uint16_t RAW_Y_MIN = ADC_MAX / 20;
static const uint16_t RAW_Y_MAX = ADC_MAX - ADC_MAX/20;

static const bool INVERT_X = false;
static const bool INVERT_Y = true;

static const uint16_t TOUCH_NOISE = ADC_MAX / 70; // порог "нет касания"
static const uint8_t  SAMPLES_PER_AXIS = 6;
static const uint8_t  CC_DELTA = 1;
static const uint32_t SEND_RATE_MS = 5;

// ---------- MIDI настройки ----------
static const uint8_t MIDI_CHANNEL = 1;
static const uint8_t MIDI_CC_X = 74;
static const uint8_t MIDI_CC_Y = 71;

// DIN-MIDI TX пин (только для RP2040; на AVR используется Serial)
#if USE_DIN_MIDI && defined(ARDUINO_ARCH_RP2040)
static const int DIN_TX_PIN = 4; // выставьте свой
#endif

// ---------- Инстансы MIDI ----------
#if USE_USB_MIDI
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, USBMIDI);
#endif

#if USE_DIN_MIDI
  #if defined(ARDUINO_ARCH_RP2040)
    MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, DINMIDI);
  #else
    // AVR/Nano: используем аппаратный Serial (D0/D1). На время работы не пользуйтесь USB-Serial.
    MIDI_CREATE_INSTANCE(HardwareSerial, Serial, DINMIDI);
  #endif
#endif

// ---------- Внутреннее ----------
uint8_t lastCC_X = 255, lastCC_Y = 255;
uint32_t lastSendMs = 0;

static inline uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline uint8_t mapToCC(uint16_t v, uint16_t inMin, uint16_t inMax, bool inv) {
  v = clampU16(v, inMin, inMax);
  if (inv) v = inMax - (v - inMin);
  const uint32_t num = (uint32_t)(v - inMin) * 127u;
  const uint32_t den = (uint32_t)(inMax - inMin);
  return (uint8_t)(den ? num / den : 0);
}

static inline void prepReadX() {
  pinMode(PIN_XP, OUTPUT); digitalWrite(PIN_XP, HIGH);
  pinMode(PIN_XM, OUTPUT); digitalWrite(PIN_XM, LOW);
  pinMode(PIN_YP, INPUT);
  pinMode(PIN_YM, INPUT);
  delayMicroseconds(30); // даём делителю устаканиться
}

static inline void prepReadY() {
  pinMode(PIN_YP, OUTPUT); digitalWrite(PIN_YP, HIGH);
  pinMode(PIN_YM, OUTPUT); digitalWrite(PIN_YM, LOW);
  pinMode(PIN_XP, INPUT);
  pinMode(PIN_XM, INPUT);
  delayMicroseconds(30);
}

static inline uint16_t readAxisAvg(bool axisX) {
  uint32_t acc = 0;
  if (axisX) {
    for (uint8_t i = 0; i < SAMPLES_PER_AXIS; i++) { prepReadX(); acc += analogRead(PIN_YP); }
  } else {
    for (uint8_t i = 0; i < SAMPLES_PER_AXIS; i++) { prepReadY(); acc += analogRead(PIN_XP); }
  }
  return (uint16_t)(acc / SAMPLES_PER_AXIS);
}

static inline bool isTouch(uint16_t s) {
  return (s > TOUCH_NOISE) && (s < (ADC_MAX - TOUCH_NOISE));
}

static inline void setAllHiZ() {
  pinMode(PIN_XP, INPUT);
  pinMode(PIN_XM, INPUT);
  pinMode(PIN_YP, INPUT);
  pinMode(PIN_YM, INPUT);
}

static inline void sendCC(uint8_t cc, uint8_t val) {
#if USE_USB_MIDI
  USBMIDI.sendControlChange(cc, val, MIDI_CHANNEL);
#endif
#if USE_DIN_MIDI
  DINMIDI.sendControlChange(cc, val, MIDI_CHANNEL);
#endif
}

void setup() {
#if defined(ARDUINO_ARCH_RP2040)
  analogReadResolution(12);
#else
  // AVR: 10-бит по умолчанию
#endif

#if USE_USB_MIDI
  USBMIDI.begin(MIDI_CHANNEL_OMNI);
  USBMIDI.turnThruOff();
#endif

#if USE_DIN_MIDI
  #if defined(ARDUINO_ARCH_RP2040)
    Serial1.setTX(DIN_TX_PIN);
    Serial1.begin(31250);
  #else
    Serial.begin(31250); // На Nano это D0/D1; не используйте одновременно USB-Serial.
  #endif
  DINMIDI.begin(MIDI_CHANNEL_OMNI);
  DINMIDI.turnThruOff();
#endif

  setAllHiZ();
}

void loop() {
  // Быстрая проверка касания
  prepReadX();
  uint16_t probe = analogRead(PIN_YP);
  if (!isTouch(probe)) {
    setAllHiZ();
    lastCC_X = lastCC_Y = 255;
    delay(1);
#if USE_USB_MIDI
    tud_task(); // RP2040: выталкиваем USB пакеты
#endif
    return;
  }

  uint16_t rawX = readAxisAvg(true);
  uint16_t rawY = readAxisAvg(false);
  if (!isTouch(rawX) || !isTouch(rawY)) { setAllHiZ(); delay(1); return; }

  uint8_t ccX = mapToCC(rawX, RAW_X_MIN, RAW_X_MAX, INVERT_X);
  uint8_t ccY = mapToCC(rawY, RAW_Y_MIN, RAW_Y_MAX, INVERT_Y);

  const uint32_t now = millis();
  if (now - lastSendMs >= SEND_RATE_MS) {
    if (lastCC_X == 255 || (uint8_t)abs((int)ccX - (int)lastCC_X) >= CC_DELTA) { sendCC(MIDI_CC_X, ccX); lastCC_X = ccX; }
    if (lastCC_Y == 255 || (uint8_t)abs((int)ccY - (int)lastCC_Y) >= CC_DELTA) { sendCC(MIDI_CC_Y, ccY); lastCC_Y = ccY; }
    lastSendMs = now;
  }

#if USE_USB_MIDI
  tud_task();
#endif
}

/*
-- Проводка DIN-MIDI OUT (корректный вариант с транзистором):
  MCU TX -> 1k -> база NPN (2N3904), эмиттер -> GND, коллектор -> DIN pin5 через 220 Ом.
  DIN pin4 -> +5V через 220 Ом, DIN pin2 -> GND, база подтянута 10k к GND.
-- Быстрый вариант (часто работает, но вне строгой спецификации):
  DIN pin4 -> +5V через 220 Ом; DIN pin5 <- TX через 220 Ом; DIN pin2 -> GND.
-- Для USB-MIDI:
  Используйте RP2040 и установите библиотеку Adafruit TinyUSB. В IDE выберите плату RP2040.
*/