#pragma once

bool hardwareReady = false;

// Register values and USB command encodings are from the TimeStick v2 ASIX
// driver. This diagnostic does not program flash, eFuse, or EEPROM.
bool asixWrite(uint8_t address, uint8_t request, uint16_t value,
               uint16_t index, uint8_t length, uint32_t word) {
  uint8_t data[4];
  for (uint8_t i = 0; i < sizeof(data); ++i) data[i] = word >> (8 * i);
  if (length > sizeof(data)) return false;
  if (request == 0x11) {
    Serial.print(F("PBUS write ")); hexWord(index); Serial.print(':'); hexWord(value);
    Serial.print(F(" bytes="));
    for (uint8_t i = 0; i < length; ++i) { hexByte(data[i]); Serial.print(' '); }
    Serial.println();
  }
  const uint8_t error = Usb.ctrlReq(address, 0, 0x40, request,
      uint8_t(value), uint8_t(value >> 8), index, length, length,
      length ? data : NULL, NULL);
  if (error) {
    Serial.print(F("ASIX write request=0x")); hexByte(request);
    Serial.print(F(" value=0x")); hexWord(value);
    Serial.print(F(" error=0x")); hexByte(error); Serial.println();
  }
  return error == 0;
}

uint32_t readLe32(const uint8_t *data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
         (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

bool phyRead(uint8_t address, uint8_t reg, uint16_t &word) {
  uint8_t data[2];
  if (!asixRead(address, 0x02, 3, reg, 2, data)) return false;
  word = uint16_t(data[0]) | (uint16_t(data[1]) << 8);
  return true;
}

void reportPhyRegisters() {
  if (!verifiedAsixAddress) { Serial.println(F("Run v first")); return; }
  const uint8_t regs[] = {0, 1, 4, 5, 9, 10, 17};
  for (uint8_t i = 0; i < sizeof(regs); ++i) {
    uint16_t value;
    if (!phyRead(verifiedAsixAddress, regs[i], value)) return;
    Serial.print(F("PHY reg=")); Serial.print(regs[i]);
    Serial.print(F(" value=0x")); hexWord(value); Serial.println();
  }
}

bool pbusWrite(uint8_t address, uint16_t bank, uint16_t reg, uint32_t word) {
  return asixWrite(address, 0x11, reg, bank, 4, word);
}

bool reportClockProgress(uint8_t address) {
  uint8_t previous[10] = {};
  bool advancing = true;
  for (uint8_t sample = 0; sample < 4; ++sample) {
    uint8_t value[10];
    if (!asixRead(address, 0x13, 2, 0x1014, sizeof(value), value)) return false;
    Serial.print(F("Initialized clock sec(hex)="));
    for (int8_t i = 9; i >= 4; --i) hexByte(value[i]);
    Serial.print(F(" ns=")); Serial.println(readLe32(value));
    if (readLe32(value) >= 1000000000UL) advancing = false;
    if (sample) {
      // Compare little-endian 48-bit seconds before the nanosecond field.
      int8_t order = 0;
      for (int8_t i = 9; i >= 0; --i) {
        if (value[i] != previous[i]) { order = value[i] > previous[i] ? 1 : -1; break; }
      }
      if (order <= 0) advancing = false;
    }
    memcpy(previous, value, sizeof(value));
    if (sample < 3) delay(1000);
  }
  Serial.println(advancing ? F("Clock progression PASS (rate accuracy unmeasured)")
                           : F("Clock progression FAIL"));
  return advancing;
}

void initializeAsixHardware(UsbDevice *device) {
  hardwareReady = false;
  reportAsixControls(device);
  const uint8_t address = device->address.devAddress;
  if (!verifiedAsixAddress || verifiedAsixAddress != address) return;
  Serial.println(F("AX88279 volatile initialization"));
  // Native firmware mode with the timestamp endpoint enabled; reload the saved
  // configuration into volatile registers, then bring the PHY out of power-down.
  if (!asixWrite(address, 0x08, 1, 2, 0, 0) ||
      !asixWrite(address, 0x06, 0, 0, 0, 0) ||
      !asixWrite(address, 0x31, 0, 0, 1, 2)) return;
  delay(500);
  uint8_t power;
  if (!asixRead(address, 0x31, 0, 0, 1, &power)) return;
  Serial.print(F("PHY power=0x")); hexByte(power); Serial.println();
  // Match AX88279 autonegotiation setup from the vendor driver's hw_init.
  uint16_t adv, gigabit;
  if (!phyRead(address, 4, adv) || !phyRead(address, 9, gigabit)) return;
  Serial.print(F("PHY initial advertisement=0x")); hexWord(adv);
  Serial.print(F(" gigabit_control=0x")); hexWord(gigabit); Serial.println();
  // Both AX88279s can advertise 2.5G. This driver implements only 1000/full,
  // so constrain advertisement instead of rejecting a faster negotiated link.
  // ASIX Clause 45 request 0x27; AN MMD 7 register 32, IEEE 2.5G advertisement bit.
  uint8_t multigigData[2];
  if (!asixRead(address, 0x27, 7, 32, 2, multigigData)) return;
  uint16_t multigig = uint16_t(multigigData[0]) | (uint16_t(multigigData[1]) << 8);
  Serial.print(F("PHY AN 2.5G advertisement=0x")); hexWord(multigig); Serial.println();
  if (!asixWrite(address, 0x27, 7, 32, 2, multigig & ~uint16_t(0x0080)) ||
      !asixRead(address, 0x27, 7, 32, 2, multigigData)) return;
  if (multigigData[0] & 0x80) { Serial.println(F("PHY 2.5G disable failed")); return; }
  if (!asixWrite(address, 0x02, 3, 4, 2, (adv & ~uint16_t(0x01e0)) | 0x1000) ||
      !asixWrite(address, 0x02, 3, 9, 2, (gigabit & ~uint16_t(0x0100)) | 0x0a00) ||
      !asixWrite(address, 0x02, 3, 0, 2, 0x1200)) return;

  bool linked = false;
  for (uint8_t attempt = 0; attempt < 15; ++attempt) {
    uint16_t status;
    // BMSR link status is latched low; read twice for the current state.
    if (!phyRead(address, 1, status) || !phyRead(address, 1, status)) return;
    Serial.print(F("PHY BMSR=0x")); hexWord(status); Serial.println();
    if (status & 0x0004) { linked = true; break; }
    delay(1000);
  }
  Serial.println(linked ? F("Ethernet PHY link UP") : F("Ethernet PHY link timeout"));
  if (!linked) { reportPhyRegisters(); return; }
  // The PHY can report link before firmware has completed its link-change
  // setup. PBus reads can temporarily return DEADBEEF during that transition.
  delay(500);

  // Retry after native-mode and PHY setup. Chip code alone does not establish
  // support for the PTP USB requests, so do not write PTP registers on failure.
  uint8_t clock[10];
  bool clockAccessible = false;
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    if (asixRead(address, 0x13, 2, 0x1014, sizeof(clock), clock) &&
        readLe32(clock) < 1000000000UL) {
      clockAccessible = true; break;
    }
    delay(100);
  }
  if (!clockAccessible) {
    Serial.println(F("PTP initialization skipped: clock request failed after PHY setup"));
    return;
  }
  // PTP CPU/report memory and counter setup from the AX88279 driver. Physical
  // PPS pin routing and packet parsers are not enabled by this phase.
  if (!pbusWrite(address, 0x12, 0x0000, 0x41080001UL) ||
      !pbusWrite(address, 0x12, 0x1040, 500000000UL) ||
      !pbusWrite(address, 0x12, 0x1000, 0x0000005fUL) ||
      !pbusWrite(address, 0x12, 0x1030, 0xccccccccUL) ||
      !pbusWrite(address, 0x12, 0x1034, 10) ||
      !pbusWrite(address, 0x13, 0x3004, 0) ||
      !pbusWrite(address, 0x13, 0x3004, 0x003f0100UL)) return;
  const uint16_t regs[] = {0x0000, 0x1000, 0x1030, 0x1034};
  const uint32_t expected[] = {0x41080001UL, 0x5f, 0xccccccccUL, 10};
  bool matching = true;
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t data[4];
    if (!asixRead(address, 0x11, regs[i], 0x12, 4, data)) return;
    Serial.print(F("PTP register ")); hexWord(regs[i]);
    Serial.print(F("=")); Serial.println(readLe32(data), HEX);
    if (readLe32(data) != expected[i]) matching = false;
  }
  if (!matching) Serial.println(F("PTP register readback mismatch"));
  hardwareReady = reportClockProgress(address) && matching;
}
