#pragma once

// AX88279: TimeStick2/DRV/ax_ptp.c, ax88279_ptp_pps_ctrl.
// The output gate is separate from the clock's PPS_EN bit. Volatile only.
bool readPpsRegister(uint16_t bank, uint16_t reg, uint32_t &value) {
  uint8_t data[4];
  if (!verifiedAsixAddress ||
      !asixRead(verifiedAsixAddress, 0x11, reg, bank, 4, data)) return false;
  value = readLe32(data);
  return value != 0xdeadbeefUL;
}

bool reportPpsState() {
  uint32_t gate, clock, width;
  if (!readPpsRegister(0x0c, 0xf8c8, gate) ||
      !readPpsRegister(0x12, 0x1000, clock) ||
      !readPpsRegister(0x12, 0x1040, width)) {
    ptpLogPrefix(); Serial.println(F("PPS register read failed"));
    return false;
  }
  ptpLogPrefix(); Serial.print(F("PPS gate=0x")); Serial.print(gate, HEX);
  Serial.print(F(" clock=0x")); Serial.print(clock, HEX);
  Serial.print(F(" active_ns=")); Serial.print(width);
  const bool enabled = (gate & 0x4000UL) && (clock & 0x10UL) &&
                       width == 500000000UL;
  Serial.println(enabled ? F(" enabled (register readback)") : F(" NOT enabled"));
  return enabled;
}

bool enablePpsOutput() {
  uint32_t gate;
  if (!readPpsRegister(0x0c, 0xf8c8, gate)) {
    ptpLogPrefix(); Serial.println(F("PPS gate read failed"));
    return false;
  }
  ptpLogPrefix(); Serial.print(F("PPS gate before=0x")); Serial.println(gate, HEX);
  if (!(gate & 0x4000UL) &&
      !pbusWrite(verifiedAsixAddress, 0x0c, 0xf8c8, gate | 0x4000UL)) {
    ptpLogPrefix(); Serial.println(F("PPS gate write failed"));
    return false;
  }
  return reportPpsState();
}
