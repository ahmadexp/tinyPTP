#pragma once

// Deliberately limited to full-speed USB and a negotiated 1 Gb/s full-duplex
// Ethernet link. Register values follow ax88279_link_setting in the driver.
EpInfo ethernetEndpoints[5];
bool ethernetReady = false;
uint8_t probeMac[6];
uint8_t rxAggregate[256];
uint16_t rxUsed = 0;
bool rxOverflow = false;

bool macWrite(uint8_t reg, uint8_t length, uint32_t word) {
  return asixWrite(verifiedAsixAddress, 1, reg, length, length, word);
}

void initializeEthernetProbe(UsbDevice *device) {
  ethernetReady = false;
  initializeAsixHardware(device);
  if (!hardwareReady) return;
  ethernetEndpoints[0] = device->epinfo[0];
  for (uint8_t i = 1; i < 5; ++i) {
    ethernetEndpoints[i].epAddr = i;
    ethernetEndpoints[i].maxPktSize = i == 1 ? 16 : 64;
    ethernetEndpoints[i].epAttribs = 0;
    ethernetEndpoints[i].bmNakPower = i == 3 ? USB_NAK_MAX_POWER : USB_NAK_NOWAIT;
  }
  if (Usb.setEpInfoEntry(verifiedAsixAddress, 5, ethernetEndpoints)) return;
  // AX88279 reports speed/duplex in byte 1 of its interrupt report. Its PHY
  // register 17 has a different layout from the older AX88179 PHY.
  bool gigabitFull = false;
  for (uint8_t attempt = 0; attempt < 10; ++attempt) {
    uint8_t status[16];
    uint16_t count = sizeof(status);
    const uint8_t error = Usb.inTransfer(verifiedAsixAddress, 1, &count, status);
    if (!error && count >= 3) {
      Diag.print(F("Link interrupt="));
      for (uint8_t i = 0; i < count; ++i) { hexByte(status[i]); Diag.print(' '); }
      Diag.println();
      if ((status[1] & 7) == 3 && (status[2] & 1)) {
        gigabitFull = true; break;
      }
    }
    delay(160);
  }
  uint16_t localGigabit, partnerGigabit;
  if (!phyRead(verifiedAsixAddress, 9, localGigabit) ||
      !phyRead(verifiedAsixAddress, 10, partnerGigabit)) return;
  // Resolve duplex from the standard MII advertisement registers as the Linux
  // driver does, rather than relying on firmware-specific interrupt bitfields.
  if (!gigabitFull || !(localGigabit & 0x0200) || !(partnerGigabit & 0x0800)) {
    Diag.println(F("Ethernet probe requires 1000/full link")); return;
  }
  if (!asixRead(verifiedAsixAddress, 1, 0x10, 6, 6, probeMac)) return;
  // Disable offloads and VLAN transformations for the raw-frame diagnostic.
  if (!macWrite(0x0b, 2, 0) || !macWrite(0xb7, 1, 0) ||
      !macWrite(0x34, 1, 0) || !macWrite(0x35, 1, 0) ||
      !macWrite(0xb9, 1, 2) || !macWrite(0x2b, 1, 0) ||
      !macWrite(0x41, 1, 0xff) || !macWrite(0x43, 1, 0) ||
      !macWrite(0x46, 1, 0) || !macWrite(0x9e, 1, 0) ||
      !macWrite(0xb1, 1, 0) || !macWrite(0xcc, 1, 1) ||
      !macWrite(0xb0, 1, 0) || !macWrite(0xb5, 1, 0xa5) ||
      !macWrite(0x54, 2, 0x0410) || !macWrite(0x0d, 1, 0) ||
      !macWrite(0xf9, 1, 7) || !macWrite(0xb8, 1, 0x29) ||
      !macWrite(0xb2, 3, 0x3ef148UL) || !macWrite(0x6d, 3, 0x006078UL) ||
      !macWrite(0xc0, 2, 0x3440) || !macWrite(0x0e, 1, 0) ||
      !macWrite(0xc3, 1, 0)) return;
  uint8_t bulk[5] = {7, 0, 0, 3, 0x3f};
  if (Usb.ctrlReq(verifiedAsixAddress, 0, 0x40, 1, 0x2e, 0,
                  5, 5, 5, bulk, NULL)) {
    Diag.println(F("Bulk-IN setup failed")); return;
  }
  if (!macWrite(0x22, 2, 0x0133) || !macWrite(0x0b, 2, 0x01aa) ||
      !macWrite(0xb7, 1, 3)) return;
  rxUsed = 0; rxOverflow = false;
  ethernetReady = true;
  Diag.println(F("Ethernet probe ready; t sends 10 echo-test frames"));
}

// Only accept a single 60-byte test frame. Larger/multi-frame aggregates are
// drained and ignored, not truncated into a plausible frame. This is not yet
// a general Ethernet receive driver.
bool isEchoAggregate(uint16_t length, uint8_t sequence) {
  if (length < 16) return false;
  const uint32_t trailer = readLe32(rxAggregate + length - 8);
  const uint32_t offset = trailer >> 13;
  if ((trailer & 0x1fff) != 1 || offset != 64 || length != 80) return false;
  const uint8_t *meta = rxAggregate + offset;
  if (!(meta[1] & 8) || (meta[3] & 0x80) || (meta[7] & 1)) return false;
  if ((uint16_t(meta[2]) | (uint16_t(meta[3] & 0x7f) << 8)) != 60) return false;
  if (memcmp(rxAggregate, probeMac, 6) || rxAggregate[12] != 0x88 ||
      rxAggregate[13] != 0xb5 || rxAggregate[14] != 0xa2 ||
      rxAggregate[15] != sequence) return false;
  for (uint8_t i = 16; i < 60; ++i)
    if (rxAggregate[i] != (uint8_t)(i ^ sequence ^ 0x5a)) return false;
  return true;
}

bool pollEcho(uint8_t sequence) {
  uint8_t packet[64];
  uint16_t count = sizeof(packet);
  const uint8_t error = Usb.inTransfer(verifiedAsixAddress, 2, &count, packet);
  if (error == hrNAK) return false;
  if (error) {
    Diag.print(F("RX USB error=0x")); hexByte(error); Diag.println();
    ethernetReady = false; return false;
  }
  if (count > sizeof(rxAggregate) - rxUsed) rxOverflow = true;
  if (!rxOverflow) { memcpy(rxAggregate + rxUsed, packet, count); rxUsed += count; }
  if (count == 64) return false;
  const bool matched = !rxOverflow && isEchoAggregate(rxUsed, sequence);
  if (!matched && rxUsed) {
    Diag.print(F("Ignored RX aggregate bytes=")); Diag.print(rxUsed);
    if (rxOverflow) Diag.print(F("+ (oversize)"));
    Diag.println();
  }
  rxUsed = 0; rxOverflow = false;
  return matched;
}

void runEthernetProbe() {
  if (!ethernetReady) { Diag.println(F("Run e to initialize Ethernet first")); return; }
  uint8_t successes = 0;
  for (uint8_t sequence = 0; sequence < 10 && ethernetReady; ++sequence) {
    uint8_t tx[72] = {};  // 8-byte USB header, 60-byte frame, 4-byte alignment.
    const uint32_t header = 60UL | (60UL << 21);
    for (uint8_t i = 0; i < 4; ++i) tx[i] = header >> (8 * i);
    memset(tx + 8, 0xff, 6);
    memcpy(tx + 14, probeMac, 6);
    tx[20] = 0x88; tx[21] = 0xb5;
    tx[22] = 0xa1; tx[23] = sequence;
    for (uint8_t i = 16; i < 60; ++i) tx[8 + i] = i ^ sequence ^ 0x5a;
    const uint8_t error = Usb.outTransfer(verifiedAsixAddress, 3, sizeof(tx), tx);
    Diag.print(F("TX seq=")); Diag.print(sequence);
    Diag.print(F(" USB status=0x")); hexByte(error); Diag.println();
    if (error) { ethernetReady = false; break; }
    const uint32_t start = millis();
    bool matched = false;
    while (!matched && ethernetReady && uint32_t(millis() - start) < 1500) {
      matched = pollEcho(sequence);
    }
    if (matched) ++successes;
    Diag.println(matched ? F("RX matching echo PASS") : F("RX echo timeout"));
  }
  Diag.print(F("Ethernet echo result=")); Diag.print(successes); Diag.println(F("/10"));
}
