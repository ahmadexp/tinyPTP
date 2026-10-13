#pragma once
#include "BuildConfig.h"

void printSigned64(int64_t v) {
  char digits[21]; uint8_t n = 0;
  uint64_t x;
  if (v < 0) { Serial.print('-'); x = uint64_t(-(v + 1)) + 1; } else x = v;
  do { digits[n++] = '0' + x % 10; x /= 10; } while (x);
  while (n) Serial.print(digits[--n]);
}
void ptpLogPrefix() {
  const uint32_t now = millis();
  Serial.print(F("tinyptp[")); Serial.print(now / 1000); Serial.print('.');
  const uint16_t fraction = now % 1000;
  if (fraction < 100) Serial.print('0');
  if (fraction < 10) Serial.print('0');
  Serial.print(fraction); Serial.print(F("]: "));
}
void ptpLogIdentity(const uint8_t *identity) {
  const char digits[] = "0123456789abcdef";
  for (uint8_t i = 0; i < 8; ++i) {
    if (i == 3 || i == 5) Serial.print('.');
    Serial.print(digits[identity[i] >> 4]); Serial.print(digits[identity[i] & 15]);
  }
}
void ptpLogSample(int64_t offset, int64_t path, int32_t frequency,
                  uint8_t state, uint32_t sample, uint16_t sequence) {
  ptpLogPrefix(); Serial.print(F("master offset ")); printSigned64(offset);
  Serial.print(F(" s")); Serial.print(state);
  Serial.print(F(" freq ")); if (frequency >= 0) Serial.print('+'); Serial.print(frequency);
  Serial.print(F(" path delay ")); printSigned64(path);
  Serial.print(F(" sample ")); Serial.print(sample);
  Serial.print(F(" seq ")); Serial.println(sequence);
}
