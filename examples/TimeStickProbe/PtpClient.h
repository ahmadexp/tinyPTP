#pragma once
#include "PtpMath.h"

// Fixed peer, L2, domain 42, two-step, E2E. No BMCA or profile claim.
const uint8_t ptpMaster[10] = {0x0c,0x9d,0x92,0xff,0xfe,0xc5,0x83,0x32,0,1};
uint8_t ptpIdentity[10];
bool ptpRunning = false, ptpDiscipline = false;
bool haveSync = false, haveFollow = false, haveTx = false, haveResponse = false;
uint16_t syncSequence = 0, delaySequence = 0;
PtpTime t1, t2, t3, t4;
int64_t syncCorrection = 0, responseCorrection = 0;
uint32_t exchangeStarted = 0, quarantineUntil = 0;
uint32_t ptpSamples = 0, ptpTimeouts = 0, ptpRxMissing = 0, ptpRxDiscard = 0;
int32_t integralPpb = 0, frequencyPpb = 0;
uint8_t tsReport[131];
uint16_t tsUsed = 0;
bool tsOverflow = false;
uint32_t tsPackets = 0, tsReports = 0;

void printSigned64(int64_t v) {
  char digits[21]; uint8_t n = 0;
  uint64_t x;
  if (v < 0) { Serial.print('-'); x = uint64_t(-(v + 1)) + 1; } else x = v;
  do { digits[n++] = '0' + x % 10; x /= 10; } while (x);
  while (n) Serial.print(digits[--n]);
}
void clearPtpExchange() { haveSync = haveFollow = haveTx = haveResponse = false; }
bool ptpHardwareTime(const uint8_t *p, PtpTime &t) {
  if (p[8] || p[9]) return false;
  t.ns = readLe32(p); t.sec = readLe32(p + 4);
  return t.ns < 1000000000UL && (t.sec || t.ns);
}
bool ptpSetClock(int64_t delta) {
  uint8_t data[12] = {};
  if (!asixRead(verifiedAsixAddress, 0x13, 2, 0x1014, 10, data)) return false;
  PtpTime now;
  if (!ptpHardwareTime(data, now) || !ptpAdd(now, delta)) return false;
  for (uint8_t i = 0; i < 4; ++i) {
    data[i] = now.ns >> (8 * i); data[4 + i] = now.sec >> (8 * i);
  }
  // Absolute set has USB latency. Use only for initial coarse acquisition;
  // the frequency servo removes the residual using packet timestamps.
  return Usb.ctrlReq(verifiedAsixAddress, 0, 0x40, 0x11, 8, 0x10,
                     0x12, 12, 12, data, NULL) == 0;
}
bool ptpSetFrequency(int32_t ppb) {
  if (!verifiedAsixAddress || !hardwareReady) return false;
  const uint32_t addend = 0xccccccccUL + int64_t(0xccccccccUL) * ppb / 1000000000LL;
  return asixWrite(verifiedAsixAddress, 0x11, 0x1030, 0x12, 4, addend);
}
void finishPtpExchange() {
  if (!haveSync || !haveFollow || !haveTx || !haveResponse) return;
  int64_t offset, path;
  ptpExchange(t1, t2, t3, t4, syncCorrection, responseCorrection, offset, path);
  clearPtpExchange();
  // Reject impossible or implausible direct-link measurements, including
  // events spanning an unexpected clock step or stale timestamp pairing.
  if (path < -1000 || path > 1000000) {
    Serial.print(F("PTP rejected path_ns=")); printSigned64(path); Serial.println(); return;
  }
  ++ptpSamples;
  Serial.print(F("PTP sample=")); Serial.print(ptpSamples);
  Serial.print(F(" seq=")); Serial.print(delaySequence);
  Serial.print(F(" offset_ns=")); printSigned64(offset);
  Serial.print(F(" path_ns=")); printSigned64(path);
  Serial.print(F(" freq_ppb=")); Serial.print(frequencyPpb);
  Serial.println(ptpDiscipline ? F(" servo=on") : F(" servo=off"));
  if (!ptpDiscipline) return;
  if (offset > 500000000LL || offset < -500000000LL) {
    if (!ptpSetClock(-offset)) { Serial.println(F("PTP clock step failed")); ptpRunning = false; return; }
    integralPpb = frequencyPpb = 0;
    if (!ptpSetFrequency(0)) ptpRunning = false;
    quarantineUntil = millis() + 200;
    Serial.println(F("PTP coarse clock step; acquiring residual offset"));
    return;
  }
  // 1 Hz PI servo, ppb equals ns/s. Conditional integration prevents windup.
  int64_t proposedIntegral = int64_t(integralPpb) - offset / 20;
  if (proposedIntegral > 200000) proposedIntegral = 200000;
  if (proposedIntegral < -200000) proposedIntegral = -200000;
  int64_t control = proposedIntegral - offset / 2;
  if (control <= 500000 && control >= -500000) integralPpb = proposedIntegral;
  if (control > 500000) control = 500000;
  if (control < -500000) control = -500000;
  frequencyPpb = control;
  if (!ptpSetFrequency(frequencyPpb)) ptpRunning = false;
}
bool sendPtpDelayRequest() {
  uint8_t tx[72] = {};
  const uint32_t header = 60UL | (60UL << 21);
  for (uint8_t i = 0; i < 4; ++i) tx[i] = header >> (8 * i);
  const uint8_t dest[6] = {1,0x1b,0x19,0,0,0};
  memcpy(tx + 8, dest, 6); memcpy(tx + 14, probeMac, 6);
  tx[20] = 0x88; tx[21] = 0xf7;
  uint8_t *h = tx + 22;
  h[0] = 1; h[1] = 2; h[3] = 44; h[4] = 42;
  memcpy(h + 20, ptpIdentity, 10);
  ++delaySequence;
  h[30] = delaySequence >> 8; h[31] = delaySequence;
  h[32] = 1; h[33] = 0x7f;
  const uint8_t error = Usb.outTransfer(verifiedAsixAddress, 3, sizeof(tx), tx);
  if (error) { Serial.print(F("PTP Delay_Req USB error=")); Serial.println(error); }
  return error == 0;
}
void handlePtpFrame(const uint8_t *frame, uint16_t length, const uint8_t *stamp) {
  if (length < 58 || frame[12] != 0x88 || frame[13] != 0xf7) return;
  if (int32_t(millis() - quarantineUntil) < 0) return;
  const uint8_t *h = frame + 14;
  const uint16_t size = ptpBe16(h + 2), seq = ptpBe16(h + 30);
  if (size < 44 || size > length - 14 || (h[0] & 0xf0) ||
      (h[1] & 0xf) != 2 || h[4] != 42 || memcmp(h + 20, ptpMaster, 10)) return;
  const uint8_t type = h[0] & 0xf;
  const int64_t correction = ptpCorrection(h + 8);
  // Bound corrections to one second, keeping all subsequent arithmetic safe.
  if (correction > 65536000000000LL || correction < -65536000000000LL) return;
  if (type == 0) {
    if (haveSync || !(h[6] & 2)) return; // fixed two-step master
    if (!stamp || !ptpHardwareTime(stamp, t2)) { ++ptpRxMissing; return; }
    syncSequence = seq; syncCorrection = correction;
    haveSync = true; exchangeStarted = millis();
  } else if (type == 8 && haveSync && !haveFollow && seq == syncSequence) {
    if (!ptpWireTime(h + 34, t1)) return;
    syncCorrection += correction;
    haveFollow = true;
    if (!sendPtpDelayRequest()) clearPtpExchange();
  } else if (type == 9 && size >= 54 && haveFollow && seq == delaySequence &&
             !memcmp(h + 44, ptpIdentity, 10)) {
    if (!ptpWireTime(h + 34, t4)) return;
    responseCorrection = correction; haveResponse = true;
    finishPtpExchange();
  }
}
void parsePtpAggregate() {
  if (rxUsed < 16) { ++ptpRxDiscard; return; }
  const uint32_t trailer = readLe32(rxAggregate + rxUsed - 8);
  const uint16_t count = trailer & 0x1fff;
  const uint32_t offset = trailer >> 13;
  if (!count || count > 8 || offset >= rxUsed - 8) { ++ptpRxDiscard; return; }
  // Validate the entire aggregate before allowing any frame to affect state.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    uint16_t payload = 0, meta = offset;
    for (uint16_t i = 0; i < count; ++i) {
      if (meta > rxUsed - 16) { ++ptpRxDiscard; return; }
      const uint8_t *m = rxAggregate + meta;
      const uint16_t len = (uint16_t(m[3] & 0x7f) << 8) | m[2];
      const bool stamped = m[7] & 1;
      const uint16_t metaSize = stamped ? 24 : 8;
      if (len < 14 || len > offset - payload || metaSize > rxUsed - 8 - meta) {
        ++ptpRxDiscard; return;
      }
      if (pass && (m[1] & 8) && !(m[3] & 0x80))
        handlePtpFrame(rxAggregate + payload, len, stamped ? m + 8 : NULL);
      payload += (len + 7) & ~uint16_t(7); meta += metaSize;
      if (payload > offset) { ++ptpRxDiscard; return; }
    }
    if (payload != offset || meta != rxUsed - 8) { ++ptpRxDiscard; return; }
  }
}
void pollPtpRx() {
  uint8_t packet[64]; uint16_t count = sizeof(packet);
  uint8_t error = Usb.inTransfer(verifiedAsixAddress, 2, &count, packet);
  if (error == hrNAK) return;
  if (error) { ptpRunning = false; Serial.print(F("PTP RX USB error=")); Serial.println(error); return; }
  if (count > sizeof(rxAggregate) - rxUsed) rxOverflow = true;
  if (!rxOverflow) { memcpy(rxAggregate + rxUsed, packet, count); rxUsed += count; }
  if (count == 64) return;
  if (!rxOverflow) parsePtpAggregate(); else ++ptpRxDiscard;
  rxUsed = 0; rxOverflow = false;
}
void pollPtpTxTimestamp() {
  uint8_t packet[64]; uint16_t count = sizeof(packet);
  uint8_t error = Usb.inTransfer(verifiedAsixAddress, 4, &count, packet);
  if (error == hrNAK) return;
  if (error) { ptpRunning = false; Serial.print(F("PTP timestamp USB error=")); Serial.println(error); return; }
  ++tsPackets;
  if (count > sizeof(tsReport) - tsUsed) tsOverflow = true;
  if (!tsOverflow) { memcpy(tsReport + tsUsed, packet, count); tsUsed += count; }
  if (count == 64) return;
  ++tsReports;
  if (tsReports <= 3) {
    Serial.print(F("TX timestamp report bytes=")); Serial.println(tsUsed);
    for (uint8_t i = 0; i < tsUsed; ++i) { hexByte(tsReport[i]); Serial.print(' '); }
    Serial.println();
  }
  if (!tsOverflow && tsUsed == sizeof(tsReport)) {
    const uint8_t base = (tsReport[130] & 1) ? 0 : 65;
    for (uint8_t i = 0; i < 5; ++i) {
      const uint8_t *r = tsReport + base + i * 13;
      const uint16_t seq = uint16_t(r[1]) | (uint16_t(r[2]) << 8);
      if ((r[0] & 8) && (r[0] >> 4) == 1 && haveFollow && seq == delaySequence &&
          ptpHardwareTime(r + 3, t3)) { haveTx = true; finishPtpExchange(); }
    }
  }
  tsUsed = 0; tsOverflow = false;
}
void startPtpClient(bool discipline) {
  if (!ethernetReady) { Serial.println(F("Run e first")); return; }
  ptpRunning = false; clearPtpExchange();
  uint8_t bfm;
  // MAC configuration invalidates the earlier PTP setup on the tested unit.
  // Restore the complete clock/report setup here, after Ethernet setup.
  if (!pbusWrite(verifiedAsixAddress, 0x12, 0x0000, 0x41080001UL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x1040, 500000000UL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x1000, 0x5f) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x1030, 0xccccccccUL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x1034, 10) ||
      !asixRead(verifiedAsixAddress, 1, 0xc1, 1, 1, &bfm) ||
      !macWrite(0xc1, 1, bfm | 0xc0) ||
      !pbusWrite(verifiedAsixAddress, 0x13, 0x1008, 0x070622UL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x2000, 0x00017ff1UL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x3000, 0x08017ff1UL) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x1038, 0) ||
      !pbusWrite(verifiedAsixAddress, 0x12, 0x103c, 0) ||
      !asixWrite(verifiedAsixAddress, 0x15, 6, 0, 1, 0x50) ||
      !pbusWrite(verifiedAsixAddress, 0x13, 0x3004, 0x003f0100UL)) return;
  if (!reportClockProgress(verifiedAsixAddress)) return;
  memcpy(ptpIdentity, probeMac, 3); ptpIdentity[3] = 0xff; ptpIdentity[4] = 0xfe;
  memcpy(ptpIdentity + 5, probeMac + 3, 3); ptpIdentity[8] = 0; ptpIdentity[9] = 1;
  tsUsed = rxUsed = 0; tsOverflow = rxOverflow = false;
  tsPackets = tsReports = 0;
  ptpSamples = ptpTimeouts = ptpRxMissing = ptpRxDiscard = 0;
  integralPpb = frequencyPpb = 0; quarantineUntil = millis() + 200;
  ptpDiscipline = discipline;
  if (!ptpSetFrequency(0)) return;
  ptpRunning = true;
  Serial.println(discipline ? F("PTP fixed client RUN domain=42 servo=on") : F("PTP timestamp test RUN domain=42 servo=off"));
}
void pollPtpClient() {
  if (!ptpRunning) return;
  static uint32_t lastInterrupt = 0;
  if (uint32_t(millis() - lastInterrupt) >= 160) {
    lastInterrupt = millis();
    uint8_t data[16]; uint16_t length = sizeof(data);
    uint8_t error = Usb.inTransfer(verifiedAsixAddress, 1, &length, data);
    if (!error && length >= 6) {
      Serial.print(F("PTP interrupt="));
      for (uint8_t i = 0; i < length; ++i) { hexByte(data[i]); Serial.print(' '); }
      Serial.println();
    }
  }
  pollPtpRx();
  if (!ptpRunning) return;
  pollPtpTxTimestamp();
  if (haveSync && uint32_t(millis() - exchangeStarted) > 2500) {
    ++ptpTimeouts;
    Serial.print(F("PTP exchange timeout follow=")); Serial.print(haveFollow);
    Serial.print(F(" tx=")); Serial.print(haveTx);
    Serial.print(F(" response=")); Serial.println(haveResponse);
    uint8_t reg[4];
    if (asixRead(verifiedAsixAddress, 0x11, 0, 0x12, 4, reg)) {
      Serial.print(F("PTP memory setting=")); Serial.println(readLe32(reg), HEX);
    }
    if (asixRead(verifiedAsixAddress, 0x11, 0x3000, 0x12, 4, reg)) {
      Serial.print(F("PTP TX control=")); Serial.println(readLe32(reg), HEX);
    }
    clearPtpExchange();
  }
  static uint32_t lastReport = 0;
  if (uint32_t(millis() - lastReport) > 10000) {
    lastReport = millis();
    Serial.print(F("PTP stats samples=")); Serial.print(ptpSamples);
    Serial.print(F(" timeouts=")); Serial.print(ptpTimeouts);
    Serial.print(F(" no_rx_timestamp=")); Serial.print(ptpRxMissing);
    Serial.print(F(" discarded=")); Serial.println(ptpRxDiscard);
    Serial.print(F("PTP TX reports=")); Serial.print(tsReports);
    Serial.print(F(" packets=")); Serial.print(tsPackets);
    Serial.print(F(" pending_bytes=")); Serial.println(tsUsed);
  }
}
