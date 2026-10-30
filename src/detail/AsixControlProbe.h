#pragma once
#include "AsixChip.h"

// ASIX vendor control-transfer protocol references:
// TimeStick2/DRV/ax_main.c: ax_get_chip_version, ax_get_chip_feature
// TimeStick2/DRV/ax_ptp.c: ax_ptp_clk_read, ax88279_ptp_gettime64
// The 'v' diagnostic only changes SET_CONFIGURATION. HardwareInit.h adds 'i'.
uint8_t verifiedAsixAddress = 0;
class ControlReadLength : public USBReadParser {
public:
  uint16_t received = 0;
  void Parse(const uint16_t len, const uint8_t *, const uint16_t &) override {
    received += len;
  }
};

bool asixRead(uint8_t address, uint8_t request, uint16_t value,
              uint16_t index, uint8_t length, uint8_t *data) {
  memset(data, 0, length);
  ControlReadLength count;
  const uint8_t error = Usb.ctrlReq(address, 0, 0xc0, request,
      uint8_t(value), uint8_t(value >> 8), index, length, length, data, &count);
  if (error || count.received != length) {
    Diag.print(F("ASIX read request=0x")); hexByte(request);
    Diag.print(F(" error=0x")); hexByte(error);
    Diag.print(F(" received=")); Diag.println(count.received);
    return false;
  }
  return true;
}

void reportAsixControls(UsbDevice *device) {
  verifiedAsixAddress = 0;
  const uint8_t address = device->address.devAddress;
  USB_DEVICE_DESCRIPTOR descriptor;
  if (Usb.getDevDescr(address, 0, sizeof(descriptor),
                     reinterpret_cast<uint8_t *>(&descriptor))) return;
  if (descriptor.idVendor != 0x0b95 || descriptor.idProduct != 0x1790) {
    Diag.println(F("ASIX diagnostic skipped: unsupported VID/PID"));
    return;
  }
  // Require the vendor configuration observed on this TimeStick before using it.
  uint8_t config[18] = {};
  if (Usb.getConfDescr(address, 0, sizeof(config), uint8_t(0), config) ||
      config[0] != 9 || config[1] != 2 || config[5] != 1 ||
      config[9] != 9 || config[10] != 4 || config[14] != 0xff) {
    Diag.println(F("ASIX diagnostic skipped: unexpected configuration"));
    return;
  }
  const uint8_t error = Usb.setConf(address, 0, 1);
  Diag.print(F("SET_CONFIGURATION(1) error=0x")); hexByte(error); Diag.println();
  if (error) return;
#if TINYPTP_AX88279A
  // Use the USB control timeout rather than a packet-count limit while the
  // A firmware services native-mode commands. Zero disables only NAK count.
  device->epinfo[0].bmNakPower = 0;
#endif
  delay(100);
  uint8_t chip;
  if (!asixRead(address, 0x01, 0x0005, 1, 1, &chip)) return;
  // Current ASIX drivers use all four high bits: 7 is AX88279, 15 is AX88279A.
  const uint8_t code = (chip & 0xf0) >> 4;
  Diag.print(F("ASIX chip status=0x")); hexByte(chip);
  Diag.print(F(" chip code=")); Diag.println(code);
  uint8_t revision;
  if (asixRead(address, 0x2a, 0x00fb, 1, 1, &revision)) {
    Diag.print(F("ASIX silicon revision=0x")); hexByte(revision); Diag.println();
  }
  uint8_t firmware[4];
  bool firmwareRead = true;
  for (uint8_t i = 0; i < 4; ++i) {
    if (!asixRead(address, 0x2a, i < 3 ? 0xfd + i : 0xfc,
                  1, 1, &firmware[i])) { firmwareRead = false; break; }
  }
  if (firmwareRead) {
    Diag.print(F("ASIX firmware="));
    for (uint8_t i = 0; i < 4; ++i) {
      if (i) Diag.print('.');
      Diag.print(i == 3 ? firmware[i] & 0x0f : firmware[i]);
    }
    Diag.println();
  }
  uint8_t mac[6];
  if (asixRead(address, 0x01, 0x0010, 6, 6, mac)) {
    Diag.print(F("ASIX MAC="));
    for (uint8_t i = 0; i < sizeof(mac); ++i) {
      if (i) Diag.print(':');
      hexByte(mac[i]);
    }
    Diag.println();
  }
  if (code != AsixChip::code) {
    Diag.print(F("Chip does not match build; expected code=")); Diag.println(AsixChip::code);
    return;
  }
  verifiedAsixAddress = address;
#if TINYPTP_AX88279A
  // Unlike AX88279's dedicated clock request, the A part's PBus clock read
  // needs firmware/PHY setup first. Follow the reference driver's ordering.
  Diag.println(F("AX88279A clock read deferred until PHY setup"));
#else
  for (uint8_t sample = 0; sample < 3; ++sample) {
    uint8_t clock[AsixChip::clockBytes];
    if (!asixRead(address, AsixChip::clockRequest, AsixChip::clockValue,
                  AsixChip::clockIndex, sizeof(clock), clock)) return;
    const uint32_t ns = uint32_t(clock[0]) | (uint32_t(clock[1]) << 8) |
                        (uint32_t(clock[2]) << 16) | (uint32_t(clock[3]) << 24);
    Diag.print(F("Clock seconds(hex)="));
    for (int8_t i = 9; i >= 4; --i) hexByte(clock[i]);
    Diag.print(F(" nanoseconds=")); Diag.print(ns);
    if (ns >= 1000000000UL) Diag.print(F(" INVALID"));
    Diag.println();
    if (sample < 2) delay(1000);
  }
  Diag.println(F("Clock reads complete; clock has not been initialized or adjusted."));
#endif
}
