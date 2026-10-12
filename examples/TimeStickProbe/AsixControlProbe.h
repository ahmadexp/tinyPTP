#pragma once

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
    Serial.print(F("ASIX read request=0x")); hexByte(request);
    Serial.print(F(" error=0x")); hexByte(error);
    Serial.print(F(" received=")); Serial.println(count.received);
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
    Serial.println(F("ASIX diagnostic skipped: unsupported VID/PID"));
    return;
  }
  // Require the vendor configuration observed on this TimeStick before using it.
  uint8_t config[18] = {};
  if (Usb.getConfDescr(address, 0, sizeof(config), uint8_t(0), config) ||
      config[0] != 9 || config[1] != 2 || config[5] != 1 ||
      config[9] != 9 || config[10] != 4 || config[14] != 0xff) {
    Serial.println(F("ASIX diagnostic skipped: unexpected configuration"));
    return;
  }
  const uint8_t error = Usb.setConf(address, 0, 1);
  Serial.print(F("SET_CONFIGURATION(1) error=0x")); hexByte(error); Serial.println();
  if (error) return;
  delay(100);
  uint8_t chip;
  if (!asixRead(address, 0x01, 0x0005, 1, 1, &chip)) return;
  const uint8_t code = (chip & 0x70) >> 4;
  Serial.print(F("ASIX chip status=0x")); hexByte(chip);
  Serial.print(F(" chip code=")); Serial.println(code);
  uint8_t revision;
  if (asixRead(address, 0x2a, 0x00fb, 1, 1, &revision)) {
    Serial.print(F("ASIX silicon revision=0x")); hexByte(revision); Serial.println();
  }
  uint8_t firmware[4];
  bool firmwareRead = true;
  for (uint8_t i = 0; i < 4; ++i) {
    if (!asixRead(address, 0x2a, i < 3 ? 0xfd + i : 0xfc,
                  1, 1, &firmware[i])) { firmwareRead = false; break; }
  }
  if (firmwareRead) {
    Serial.print(F("ASIX firmware="));
    for (uint8_t i = 0; i < 4; ++i) {
      if (i) Serial.print('.');
      Serial.print(i == 3 ? firmware[i] & 0x0f : firmware[i]);
    }
    Serial.println();
  }
  uint8_t mac[6];
  if (asixRead(address, 0x01, 0x0010, 6, 6, mac)) {
    Serial.print(F("ASIX MAC="));
    for (uint8_t i = 0; i < sizeof(mac); ++i) {
      if (i) Serial.print(':');
      hexByte(mac[i]);
    }
    Serial.println();
  }
  if (code != 7) {
    Serial.println(F("AX88279 clock read skipped: chip code is not 7"));
    return;
  }
  verifiedAsixAddress = address;
  for (uint8_t sample = 0; sample < 3; ++sample) {
    uint8_t clock[10];
    if (!asixRead(address, 0x13, 0x0002, 0x1014, sizeof(clock), clock)) return;
    const uint32_t ns = uint32_t(clock[0]) | (uint32_t(clock[1]) << 8) |
                        (uint32_t(clock[2]) << 16) | (uint32_t(clock[3]) << 24);
    Serial.print(F("Clock seconds(hex)="));
    for (int8_t i = 9; i >= 4; --i) hexByte(clock[i]);
    Serial.print(F(" nanoseconds=")); Serial.print(ns);
    if (ns >= 1000000000UL) Serial.print(F(" INVALID"));
    Serial.println();
    if (sample < 2) delay(1000);
  }
  Serial.println(F("Clock reads complete; clock has not been initialized or adjusted."));
}
