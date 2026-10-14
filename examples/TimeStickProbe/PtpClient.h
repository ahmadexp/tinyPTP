#pragma once
#include "PtpMath.h"
#include "PtpLog.h"
#include "PpsOutput.h"
#include "PtpServo.h"

// Fixed peer, L2, domain 42, two-step, E2E. No BMCA or profile claim.
const uint8_t ptpMaster[10] = {0x0c,0x9d,0x92,0xff,0xfe,0xc5,0x83,0x32,0,1};
uint8_t ptpIdentity[10], selectedMaster[10], selectedFollower[10];
bool ptpMasterRole = false, ptpLearnMaster = false, ptpPeerBound = false;
bool masterSyncPending = false, masterFollowerBound = false;
uint32_t masterLastSync = 0, masterSyncStarted = 0, masterResponses = 0;
bool ptpRunning = false, ptpDiscipline = false;
bool haveSync = false, haveFollow = false, haveTx = false, haveResponse = false;
uint16_t syncSequence = 0, delaySequence = 0;
PtpTime t1, t2, t3, t4;
int64_t syncCorrection = 0, responseCorrection = 0;
uint32_t exchangeStarted = 0, quarantineUntil = 0;
uint32_t ptpSamples = 0, ptpTimeouts = 0, ptpRxMissing = 0, ptpRxDiscard = 0;
PtpServo clockServo;
int32_t frequencyPpb = 0;
uint8_t tsReport[131];
uint16_t tsUsed = 0;
bool tsOverflow = false;
uint32_t tsPackets = 0, tsReports = 0;
uint32_t masterLastAnnounce = 0;
uint16_t announceSequence = 0;
uint8_t servoStableSamples = 0;
uint32_t lastCompletedExchange = 0;
bool followerReceiving = false;

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
    clockServo.clearHistory();
    servoStableSamples = 0;
    Serial.print(F("PTP rejected path_ns=")); printSigned64(path); Serial.println(); return;
  }
  ++ptpSamples;
  lastCompletedExchange = millis(); followerReceiving = true;
#if !TINYPTP_PTP4L_LOG
  Serial.print(F("PTP sample=")); Serial.print(ptpSamples);
  Serial.print(F(" seq=")); Serial.print(delaySequence);
  Serial.print(F(" offset_ns=")); printSigned64(offset);
  Serial.print(F(" path_ns=")); printSigned64(path);
  Serial.print(F(" freq_ppb=")); Serial.print(frequencyPpb);
  Serial.println(ptpDiscipline ? F(" servo=on") : F(" servo=off"));
#endif
  if (!ptpDiscipline) {
#if TINYPTP_PTP4L_LOG
    ptpLogSample(offset, path, frequencyPpb, 0, ptpSamples, delaySequence);
#endif
    return;
  }
  if (offset > 500000000LL || offset < -500000000LL) {
    if (!ptpSetClock(-offset)) { Serial.println(F("PTP clock step failed")); ptpRunning = false; return; }
    clockServo.reset(); frequencyPpb = 0; servoStableSamples = 0;
    if (!ptpSetFrequency(0)) ptpRunning = false;
    quarantineUntil = millis() + 200;
#if TINYPTP_PTP4L_LOG
    ptpLogSample(offset, path, frequencyPpb, 1, ptpSamples, delaySequence);
    ptpLogPrefix(); Serial.println(F("clock stepped; acquiring residual offset"));
#else
    Serial.println(F("PTP coarse clock step; acquiring residual offset"));
#endif
    return;
  }
#if TINYPTP_SERVO == 2
  const uint8_t previousServoMode = clockServo.mode();
#endif
  frequencyPpb = clockServo.update(offset, t1.sec * 1000UL + t1.ns / 1000000UL, frequencyPpb);
  if (!ptpSetFrequency(frequencyPpb)) {
    ptpRunning = false; ptpLogPrefix(); Serial.println(F("frequency update failed")); return;
  }
#if TINYPTP_SERVO == 2
  if (clockServo.mode() != previousServoMode) {
    ptpLogPrefix(); Serial.print(F("servo AUTO_PI mode ")); Serial.print(clockServo.mode());
    Serial.print(F(" kp_milli ")); Serial.print(clockServo.kpMilli());
    Serial.print(F(" ki_milli ")); Serial.println(clockServo.kiMilli());
  }
#endif
  if (offset >= -1000 && offset <= 1000) {
    if (servoStableSamples < 8) ++servoStableSamples;
  } else servoStableSamples = 0;
#if TINYPTP_PTP4L_LOG
  ptpLogSample(offset, path, frequencyPpb, servoStableSamples >= 8 ? 3 : 2, ptpSamples, delaySequence);
#endif
}
// At most 78 Ethernet bytes, padded to an eight-byte USB boundary after the
// ASIX header. All transmitted PTP frames have zero correction on this direct link.
bool sendPtpMessage(uint8_t type, uint16_t seq, const PtpTime *stamp = NULL,
                    const uint8_t *requester = NULL) {
  uint8_t tx[88] = {};
  const uint8_t messageLength = type == 11 ? 64 : type == 9 ? 54 : 44;
  const uint8_t frameLength = type == 11 ? 78 : type == 9 ? 68 : 60;
  const uint32_t header = uint32_t(frameLength) | (uint32_t(frameLength) << 21);
  for (uint8_t i = 0; i < 4; ++i) tx[i] = header >> (8 * i);
  const uint8_t dest[6] = {1,0x1b,0x19,0,0,0};
  memcpy(tx + 8, dest, 6); memcpy(tx + 14, probeMac, 6);
  tx[20] = 0x88; tx[21] = 0xf7;
  uint8_t *h = tx + 22;
  h[0] = type; h[1] = 2; h[3] = messageLength; h[4] = 42;
  if (type == 0) h[6] = 2; // two-step Sync
  memcpy(h + 20, ptpIdentity, 10);
  h[30] = seq >> 8; h[31] = seq;
  h[32] = type == 0 ? 0 : type == 1 ? 1 : type == 8 ? 2 : type == 9 ? 3 : 5;
  h[33] = type == 11 ? 1 : type == 1 ? 0x7f : 0; // 1 Hz Sync and Delay_Resp interval
  if (type == 11) {
    // Free-running oscillator, arbitrary timescale: no UTC-valid/traceability flags.
    h[47] = 128; h[48] = 248; h[49] = 0xfe;
    h[50] = h[51] = 0xff; h[52] = 128;
    memcpy(h + 53, ptpIdentity, 8); h[63] = 0xa0;
  }
  if (stamp) {
    for (uint8_t i = 0; i < 4; ++i) {
      h[36 + i] = stamp->sec >> (24 - 8 * i);
      h[40 + i] = stamp->ns >> (24 - 8 * i);
    }
  }
  if (type == 9 && requester) memcpy(h + 44, requester, 10);
  const uint8_t bytes = 8 + ((frameLength + 7) & ~uint8_t(7));
  const uint8_t error = Usb.outTransfer(verifiedAsixAddress, 3, bytes, tx);
  if (error) { Serial.print(F("PTP TX USB error=")); Serial.println(error); }
  return error == 0;
}
bool sendPtpDelayRequest() { return sendPtpMessage(1, ++delaySequence); }
void handleMasterDelayRequest(const uint8_t *h, uint16_t size, const uint8_t *stamp) {
  if ((h[0] & 0xf) != 1 || size != 44 || !memcmp(h + 20, ptpIdentity, 10)) return;
  if (masterFollowerBound && memcmp(h + 20, selectedFollower, 10)) return;
  PtpTime received;
  if (!stamp || !ptpHardwareTime(stamp, received)) { ++ptpRxMissing; return; }
  // Direct-link experiment: no transparent clocks or nonzero Delay_Req correction.
  if (ptpCorrection(h + 8) != 0) return;
  if (!masterFollowerBound) {
    memcpy(selectedFollower, h + 20, 10); masterFollowerBound = true;
    ptpLogPrefix(); Serial.print(F("port 1: received Delay_Req from follower "));
    ptpLogIdentity(selectedFollower); Serial.println();
  }
  if (!sendPtpMessage(9, ptpBe16(h + 30), &received, h + 20)) {
    ptpRunning = false; return;
  }
  ++masterResponses;
}
void handlePtpFrame(const uint8_t *frame, uint16_t length, const uint8_t *stamp) {
  if (length < 58 || frame[12] != 0x88 || frame[13] != 0xf7) return;
  if (int32_t(millis() - quarantineUntil) < 0) return;
  const uint8_t *h = frame + 14;
  const uint16_t size = ptpBe16(h + 2), seq = ptpBe16(h + 30);
  if (size < 44 || size > length - 14 || (h[0] & 0xf0) ||
      (h[1] & 0xf) != 2 || h[4] != 42) return;
  if (ptpMasterRole) { handleMasterDelayRequest(h, size, stamp); return; }
  if (ptpLearnMaster && !ptpPeerBound) {
    PtpTime received;
    if ((h[0] & 0xf) != 0 || !(h[6] & 2) || !stamp ||
        !ptpHardwareTime(stamp, received) || !memcmp(h + 20, ptpIdentity, 10) ||
        ptpCorrection(h + 8) != 0) return;
    memcpy(selectedMaster, h + 20, 10); ptpPeerBound = true;
    ptpLogPrefix(); Serial.print(F("selected master ")); ptpLogIdentity(selectedMaster); Serial.println();
  }
  if (memcmp(h + 20, ptpLearnMaster ? selectedMaster : ptpMaster, 10)) return;
  const uint8_t type = h[0] & 0xf;
  const int64_t correction = ptpCorrection(h + 8);
  // Bound corrections to one second, keeping all subsequent arithmetic safe.
  if (correction > 65536000000000LL || correction < -65536000000000LL) return;
  if (type == 0) {
    if (haveSync || !(h[6] & 2)) return; // fixed two-step master
    if (!stamp || !ptpHardwareTime(stamp, t2)) { ++ptpRxMissing; return; }
    if (!followerReceiving) {
      ptpLogPrefix(); Serial.println(F("port 1: LISTENING to SLAVE on MASTER_SELECTED"));
      followerReceiving = true;
    }
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
  if (diagnosticsEnabled && tsReports <= 3) {
    Serial.print(F("TX timestamp report bytes=")); Serial.println(tsUsed);
    for (uint8_t i = 0; i < tsUsed; ++i) { hexByte(tsReport[i]); Serial.print(' '); }
    Serial.println();
  }
  if (!tsOverflow && tsUsed == sizeof(tsReport)) {
    const uint8_t base = (tsReport[130] & 1) ? 0 : 65;
    for (uint8_t i = 0; i < 5; ++i) {
      const uint8_t *r = tsReport + base + i * 13;
      const uint16_t seq = uint16_t(r[1]) | (uint16_t(r[2]) << 8);
      if (ptpMasterRole) {
        PtpTime sent;
        if ((r[0] & 8) && (r[0] >> 4) == 0 && masterSyncPending &&
            seq == syncSequence && ptpHardwareTime(r + 3, sent)) {
          masterSyncPending = false;
          if (!sendPtpMessage(8, seq, &sent)) ptpRunning = false;
          else ++ptpSamples;
        }
        continue;
      }
      if ((r[0] & 8) && (r[0] >> 4) == 1 && haveFollow && seq == delaySequence &&
          ptpHardwareTime(r + 3, t3)) { haveTx = true; finishPtpExchange(); }
    }
  }
  tsUsed = 0; tsOverflow = false;
}
void startPtpClient(bool discipline, bool masterRole = false, bool learnMaster = false) {
  if (!ethernetReady) { Serial.println(F("Run e first")); return; }
  ptpRunning = false; clearPtpExchange();
  ptpMasterRole = masterRole; ptpLearnMaster = learnMaster;
  ptpPeerBound = masterFollowerBound = masterSyncPending = false;
  masterResponses = 0; servoStableSamples = 0; followerReceiving = false;
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
  if (!enablePpsOutput()) return;
  memcpy(ptpIdentity, probeMac, 3); ptpIdentity[3] = 0xff; ptpIdentity[4] = 0xfe;
  memcpy(ptpIdentity + 5, probeMac + 3, 3); ptpIdentity[8] = 0; ptpIdentity[9] = 1;
  tsUsed = rxUsed = 0; tsOverflow = rxOverflow = false;
  tsPackets = tsReports = 0;
  ptpSamples = ptpTimeouts = ptpRxMissing = ptpRxDiscard = 0;
  clockServo.reset(); frequencyPpb = 0; quarantineUntil = millis() + 200;
  ptpDiscipline = discipline && !masterRole;
  if (!ptpSetFrequency(0)) return;
  ptpRunning = true;
  masterLastSync = masterLastAnnounce = lastCompletedExchange = millis();
  ptpLogPrefix(); Serial.print(F("clock identity ")); ptpLogIdentity(ptpIdentity); Serial.println();
  ptpLogPrefix();
  if (masterRole) Serial.println(F("port 1: INITIALIZING to MASTER on FIXED_ROLE"));
  else Serial.println(F("port 1: INITIALIZING to LISTENING on FIXED_ROLE"));
  if (masterRole) {
    ptpLogPrefix(); Serial.println(F("port 1: assuming the grandmaster role"));
    ptpLogPrefix(); Serial.println(F("grandmaster: free-running oscillator, UTC not valid"));
  } else if (ptpDiscipline) {
    ptpLogPrefix();
#if TINYPTP_SERVO == 2
    Serial.println(F("servo AUTO_PI mode 2 kp_milli 750 ki_milli 125 window 16 limit 500000 ppb"));
#elif TINYPTP_SERVO == 1
    Serial.println(F("servo LINREG window 8 phase_tau 4s limit 500000 ppb"));
#else
    Serial.println(F("servo PI kp 1/2 ki 1/20 limit 500000 ppb"));
#endif
  }

}
void pollPtpMasterSchedule() {
  if (uint32_t(millis() - masterLastAnnounce) >= 2000) {
    masterLastAnnounce = millis();
    if (!sendPtpMessage(11, ++announceSequence)) { ptpRunning = false; return; }
  }
  if (masterSyncPending && uint32_t(millis() - masterSyncStarted) > 2500) {
    masterSyncPending = false; ++ptpTimeouts;
    ptpLogPrefix(); Serial.println(F("port 1: Sync TX timestamp timeout"));
  }
  if (!masterSyncPending && uint32_t(millis() - masterLastSync) >= 1000) {
    masterLastSync = masterSyncStarted = millis();
    ++syncSequence;
    if (!sendPtpMessage(0, syncSequence)) { ptpRunning = false; return; }
    masterSyncPending = true;
  }
}
void pollPtpClient() {
  if (!ptpRunning) return;
  static uint32_t lastInterrupt = 0;
  if (uint32_t(millis() - lastInterrupt) >= 160) {
    lastInterrupt = millis();
    uint8_t data[16]; uint16_t length = sizeof(data);
    uint8_t error = Usb.inTransfer(verifiedAsixAddress, 1, &length, data);
    if (!error && length >= 6) {
      if (!(data[2] & 1)) {
        ptpRunning = ethernetReady = false; clearPtpExchange(); servoStableSamples = 0;
        ptpLogPrefix(); Serial.println(F("port 1: link down")); return;
      }
      Diag.print(F("PTP interrupt="));
      for (uint8_t i = 0; i < length; ++i) { hexByte(data[i]); Diag.print(' '); }
      Diag.println();
    }
  }
  pollPtpRx();
  if (!ptpRunning) return;
  pollPtpTxTimestamp();
  if (!ptpRunning) return;
  if (ptpMasterRole) pollPtpMasterSchedule();
  if (haveSync && uint32_t(millis() - exchangeStarted) > 2500) {
    clockServo.clearHistory();
    ++ptpTimeouts; servoStableSamples = 0;
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
  if (!ptpMasterRole && uint32_t(millis() - lastCompletedExchange) > 5000) {
    clockServo.clearHistory();
    if (followerReceiving) {
      ptpLogPrefix(); Serial.println(F("port 1: SLAVE to LISTENING on SYNC_TIMEOUT"));
      followerReceiving = false; servoStableSamples = 0;
    }
    // Preserve the selected peer but require fresh timestamps for reacquisition.
    clearPtpExchange(); lastCompletedExchange = millis();
  }
  static uint32_t lastReport = 0;
  const uint16_t reportInterval = TINYPTP_PTP4L_LOG && ptpMasterRole ? 1000 : 10000;
  if (uint32_t(millis() - lastReport) >= reportInterval) {
    lastReport = millis();
#if TINYPTP_PTP4L_LOG
    if (ptpMasterRole) {
      ptpLogMaster(ptpSamples, masterResponses, frequencyPpb,
                   ptpTimeouts, ptpRxMissing, ptpRxDiscard);
    } else {
      ptpLogPrefix(); Serial.print(F("FOLLOWER"));
      Serial.print(F(" samples ")); Serial.print(ptpSamples);
      Serial.print(F(" timeouts ")); Serial.print(ptpTimeouts);
      Serial.print(F(" missing_rx ")); Serial.print(ptpRxMissing);
      Serial.print(F(" discarded ")); Serial.println(ptpRxDiscard);
    }
#else
    if (ptpMasterRole) {
      Serial.print(F("PTP master Delay_Resp sent=")); Serial.println(masterResponses);
    }
    Serial.print(F("PTP stats samples=")); Serial.print(ptpSamples);
    Serial.print(F(" timeouts=")); Serial.print(ptpTimeouts);
    Serial.print(F(" no_rx_timestamp=")); Serial.print(ptpRxMissing);
    Serial.print(F(" discarded=")); Serial.println(ptpRxDiscard);
    Serial.print(F("PTP TX reports=")); Serial.print(tsReports);
    Serial.print(F(" packets=")); Serial.print(tsPackets);
    Serial.print(F(" pending_bytes=")); Serial.println(tsUsed);
#endif
  }
}
