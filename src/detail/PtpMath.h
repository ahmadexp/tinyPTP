#pragma once
#include <stdint.h>

// Bounded diagnostic epoch: 32-bit seconds, valid until 2106. Reject rather
// than truncate the upper 16 bits of an IEEE 1588 seconds field.
struct PtpTime { uint32_t sec; uint32_t ns; };
inline uint16_t ptpBe16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
inline uint32_t ptpBe32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline bool ptpWireTime(const uint8_t *p, PtpTime &t) {
  if (p[0] || p[1]) return false;
  t.sec = ptpBe32(p + 2); t.ns = ptpBe32(p + 6);
  return t.ns < 1000000000UL;
}
inline int64_t ptpDiff(const PtpTime &a, const PtpTime &b) {
  return (int64_t(a.sec) - b.sec) * 1000000000LL + int64_t(a.ns) - b.ns;
}
inline bool ptpAdd(PtpTime &t, int64_t ns) {
  const int64_t value = int64_t(t.sec) * 1000000000LL + t.ns + ns;
  if (value < 0 || value > 4294967295999999999LL) return false;
  t.sec = value / 1000000000LL; t.ns = value % 1000000000LL;
  return true;
}
// Correction fields are signed 48.16 ns. Keep fractions through the exchange.
inline int64_t ptpCorrection(const uint8_t *p) {
  uint64_t v = 0;
  for (uint8_t i = 0; i < 8; ++i) v = (v << 8) | p[i];
  if (!(v >> 63)) return int64_t(v);
  return -1 - int64_t(~v);
}
inline void ptpExchange(const PtpTime &t1, const PtpTime &t2,
                        const PtpTime &t3, const PtpTime &t4,
                        int64_t forwardCorrection, int64_t reverseCorrection,
                        int64_t &offset, int64_t &delay) {
  const int64_t a = ptpDiff(t2, t1), b = ptpDiff(t4, t3);
  offset = (a - b) / 2 - (forwardCorrection - reverseCorrection) / 131072;
  delay = (a + b) / 2 - (forwardCorrection + reverseCorrection) / 131072;
}
