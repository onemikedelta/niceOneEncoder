/*
 * niceOneEncoder - Nice One / Nice Flor-S rolling code transmitter
 *
 * Sends a 72-bit Nice One frame (52-bit Nice Flor-S core + 20-bit extension)
 * or a plain 52-bit Nice Flor-S frame, using OOK on a single digital pin.
 *
 * No lookup table: the cipher is computed from a 32-byte key.
 *
 * Copyright (c) 2026 onemikedelta <https://github.com/onemikedelta>
 * Licensed under the niceOneEncoder License 1.0 (non-commercial copyleft).
 * See LICENSE.
 */

#include <EEPROM.h>
#include <avr/pgmspace.h>

/* ======================= configuration ======================= */

#define REMOTE_SERIAL 0x1234567UL /* 28-bit remote serial */
#define FRAME_BITS    72          /* 72 = Nice One, 52 = Nice Flor-S */

#define PIN_TX  8                 /* OOK transmitter data input */
#define PIN_LED LED_BUILTIN       /* D13 on Uno/Nano, lit while transmitting */

/* Buttons wired to GND, using the internal pull-ups. */
static const uint8_t BUTTON_PIN[4] = { 9, 10, 11, 12 };

/* Nice button codes are positional: button 1..4 = 0x1, 0x2, 0x4, 0x8 */
static const uint8_t BUTTON_CODE[4] = { 0x1, 0x2, 0x4, 0x8 };

/* ======================= protocol ======================= */

/* Pulse widths in microseconds.
 * bit 1 = TE_LONG high + TE_SHORT low
 * bit 0 = TE_SHORT high + TE_LONG low
 * start and stop bit = 3 * TE_SHORT
 * each frame is preceded by GAP_UNITS * TE_SHORT of silence
 */
#define TE_SHORT  500
#define TE_LONG   1000
#define GAP_UNITS 37
#define PARCELS   16 /* repetitions sent per button press */

/* Key used by the Nice cipher. Indexed by 5 bits, so 32 bytes are enough. */
static const uint8_t NICE_KEY[32] PROGMEM = {
  0x19, 0x05, 0x3F, 0x61, 0xCB, 0x6D, 0x45, 0x0A,
  0x03, 0x07, 0x40, 0x05, 0x47, 0x86, 0xB4, 0x4A,
  0x29, 0x9E, 0x66, 0xC7, 0x5D, 0x76, 0xAF, 0x65,
  0x3C, 0x4D, 0x8F, 0xAE, 0x67, 0x94, 0x1D, 0x55
};

/* ======================= counter ======================= */

/* The receiver only accepts a counter higher than the last one it saw, so it
 * has to survive power loss.
 */
#define EEPROM_BASE  0
#define EEPROM_MAGIC 0x4E31

static uint16_t counter;

static void counterLoad(void) {
  uint16_t magic = 0;
  EEPROM.get(EEPROM_BASE, magic);
  if (magic == EEPROM_MAGIC) {
    EEPROM.get(EEPROM_BASE + 2, counter);
  } else {
    counter = 0;
  }
}

static void counterSave(void) {
  uint16_t magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_BASE, magic);
  EEPROM.put(EEPROM_BASE + 2, counter);
}

/* ======================= cipher ======================= */

static inline uint8_t keyByte(uint8_t i) {
  return pgm_read_byte(&NICE_KEY[i & 0x1F]);
}

static void magicXor(uint8_t *p, uint8_t k) {
  for (uint8_t i = 1; i < 6; i++) p[i] ^= k;
}

/* Encrypts (serial << 16 | counter) into the 48-bit frame body. */
static uint64_t niceEncrypt(uint64_t data) {
  uint8_t p[6];
  for (uint8_t i = 0; i < 6; i++) p[i] = (uint8_t)((data >> (8 * i)) & 0xFF);

  for (uint8_t round = 0; round < 2; round++) {
    uint8_t k = keyByte(p[0] & 0x1F);
    magicXor(p, k);
    p[5] &= 0x0F;
    p[0] ^= (uint8_t)(k & 0xE0);

    k = (uint8_t)(keyByte(p[0] >> 3) + 0x25);
    magicXor(p, k);
    p[5] &= 0x0F;
    p[0] ^= (uint8_t)(k & 0x07);

    if (round == 0) {
      uint8_t t = p[0];
      p[0] = p[1];
      p[1] = t;
    }
  }

  /* final byte swap and inversion */
  p[5] = (uint8_t)((~p[5]) & 0x0F);
  uint8_t k = (uint8_t)~p[4];
  p[4] = (uint8_t)~p[0];
  p[0] = (uint8_t)~p[2];
  p[2] = k;
  k = (uint8_t)~p[3];
  p[3] = (uint8_t)~p[1];
  p[1] = k;

  uint64_t out = 0;
  for (int8_t i = 5; i >= 0; i--) out = (out << 8) | p[i];
  return out;
}

/* ======================= frame assembly ======================= */

/* Frame fields, as transmitted:
 *   nibble 0  button code
 *   nibble 1  0x0F ^ button ^ parcel
 *   nibble 2  encrypted body, high nibble
 *   byte 2..6 encrypted body
 * Nice One appends 20 more bits: parcel code, hold flag and a CRC8.
 */
static uint8_t frame[10];

static void frameBuild(uint8_t button, uint8_t parcel) {
  uint64_t body = niceEncrypt(((uint64_t)(REMOTE_SERIAL & 0x0FFFFFFFUL) << 16) | counter);
  uint8_t head = (uint8_t)(((button & 0x0F) << 4) | ((0x0F ^ button ^ parcel) & 0x0F));
  uint64_t key = ((uint64_t)head << 44) | body;

  for (uint8_t i = 0; i < 7; i++) frame[i] = (uint8_t)((key >> (48 - 8 * i)) & 0xFF);
  frame[7] = frame[8] = frame[9] = 0;
}

static uint64_t frameKey52(void) {
  uint64_t v = (uint64_t)(frame[0] & 0x0F);
  for (uint8_t i = 1; i < 7; i++) v = (v << 8) | frame[i];
  return v;
}

static uint8_t frameBit(uint8_t i) {
  return (uint8_t)((frame[i >> 3] >> (7 - (i & 7))) & 1);
}

/* CRC8 over frame bits 4..67: reflected, polynomial 0x97, init 0xFF,
 * result bit-reversed.
 */
static uint8_t frameCrc(void) {
  uint8_t reg = 0xFF;
  for (uint8_t i = 4; i < 68; i++) {
    uint8_t feedback = (uint8_t)((reg & 1) ^ frameBit(i));
    reg >>= 1;
    if (feedback) reg ^= 0x97;
  }
  uint8_t crc = 0;
  for (uint8_t i = 0; i < 8; i++) crc = (uint8_t)((crc << 1) | ((reg >> i) & 1));
  return crc;
}

/* Builds the 20-bit Nice One extension and returns it right-aligned. */
static uint32_t frameExtension(uint8_t parcel, bool hold) {
  uint8_t k = (uint8_t)(((parcel < 4) ? 0x8F : 0x80) ^ parcel);
  frame[7] = k;
  frame[8] = (uint8_t)((hold ? 0x10 : 0x00) ^ (uint8_t)(k << 4));
  frame[9] = 0;

  uint8_t crc = frameCrc();
  frame[8] = (uint8_t)(frame[8] | (crc >> 4));
  frame[9] = (uint8_t)(crc << 4);

  uint32_t ext = ((uint32_t)frame[7] << 16) | ((uint32_t)frame[8] << 8) | frame[9];
  return (ext >> 4) & 0xFFFFFUL;
}

/* ======================= transmit ======================= */

static void txBit(uint8_t bit) {
  if (bit) {
    digitalWrite(PIN_TX, HIGH);
    delayMicroseconds(TE_LONG);
    digitalWrite(PIN_TX, LOW);
    delayMicroseconds(TE_SHORT);
  } else {
    digitalWrite(PIN_TX, HIGH);
    delayMicroseconds(TE_SHORT);
    digitalWrite(PIN_TX, LOW);
    delayMicroseconds(TE_LONG);
  }
}

static void txParcel(uint64_t key, uint32_t ext) {
  digitalWrite(PIN_TX, LOW);
  for (uint8_t i = 0; i < GAP_UNITS; i++) delayMicroseconds(TE_SHORT);

  digitalWrite(PIN_TX, HIGH); /* start bit */
  delayMicroseconds(TE_SHORT * 3);
  digitalWrite(PIN_TX, LOW);
  delayMicroseconds(TE_SHORT * 3);

  for (int8_t i = 51; i >= 0; i--) txBit((uint8_t)((key >> i) & 1));
  if (FRAME_BITS == 72) {
    for (int8_t i = 19; i >= 0; i--) txBit((uint8_t)((ext >> i) & 1));
  }

  digitalWrite(PIN_TX, HIGH); /* stop bit */
  delayMicroseconds(TE_SHORT * 3);
  digitalWrite(PIN_TX, LOW);
}

/* One button press: advance the counter, store it, then repeat the frame.
 * Only the first parcel has the hold flag clear.
 */
static void transmit(uint8_t button) {
  counter++;
  counterSave();

  digitalWrite(PIN_LED, HIGH);
  for (uint8_t parcel = 0; parcel < PARCELS; parcel++) {
    frameBuild(button, parcel);
    uint64_t key = frameKey52();
    uint32_t ext = (FRAME_BITS == 72) ? frameExtension(parcel, parcel != 0) : 0;
    txParcel(key, ext);
  }
  digitalWrite(PIN_LED, LOW);
}

/* ======================= sketch ======================= */

void setup() {
  for (uint8_t i = 0; i < 4; i++) pinMode(BUTTON_PIN[i], INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_TX, OUTPUT);
  digitalWrite(PIN_TX, LOW);

  counterLoad();
}

void loop() {
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(BUTTON_PIN[i]) == LOW) {
      delay(20); /* debounce */
      if (digitalRead(BUTTON_PIN[i]) != LOW) continue;

      transmit(BUTTON_CODE[i]);

      while (digitalRead(BUTTON_PIN[i]) == LOW) delay(10); /* one frame per press */
      delay(20);
    }
  }
}
