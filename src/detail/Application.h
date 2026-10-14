#pragma once

#include "BuildConfig.h"
#include "DiagnosticLog.h"

// USB bring-up for one TimeStick directly attached to a MAX3421E.
// Descriptors on boot; optional Ethernet and fixed-peer PTP diagnostics.
// This is an experimental bounded client, not a general-purpose PTP stack.
USB Usb;
bool reported = false;
bool hostReady = false;
uint8_t lastState = 0xff;
bool automaticRoleEnabled = TINYPTP_BOOT_ROLE != 0;
uint32_t bootRetryAt = 0;

void hexByte(uint8_t value) {
  if (value < 16) Diag.print('0');
  Diag.print(value, HEX);
}

void hexWord(uint16_t value) {
  hexByte(value >> 8);
  hexByte(value & 0xff);
}

#include "AsixControlProbe.h"
#include "HardwareInit.h"
#include "EthernetProbe.h"
#include "PtpClient.h"

// Read-only fallback for the standard UNO shield wiring (chip select D10).
// A failed oscillator check can also mean the controller is not reachable.
uint8_t slowRegisterRead(uint8_t reg, uint32_t hz) {
  SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
  digitalWrite(10, LOW);
  SPI.transfer(reg);
  const uint8_t value = SPI.transfer(0);
  digitalWrite(10, HIGH);
  SPI.endTransaction();
  return value;
}

void reportHostRegisters(uint32_t hz) {
  Diag.print(F("SPI Hz=")); Diag.print(hz);
  Diag.print(F(" REVISION=")); hexByte(slowRegisterRead(rREVISION, hz));
  Diag.print(F(" USBIRQ=")); hexByte(slowRegisterRead(rUSBIRQ, hz));
  Diag.print(F(" USBCTL=")); hexByte(slowRegisterRead(rUSBCTL, hz));
  Diag.print(F(" PINCTL=")); hexByte(slowRegisterRead(rPINCTL, hz));
  Diag.println();
}

// Keep only the first nine bytes of each descriptor. Configuration trees may
// span many USB packets and must not be buffered in their entirety on an UNO.
class DescriptorSummary : public USBReadParser {
  uint8_t head[9];
  uint8_t used;
  uint8_t length;
  bool invalid;

  void emit() {
    const uint8_t type = head[1];
    Diag.print(F("  descriptor type=0x")); hexByte(type);
    Diag.print(F(" length=")); Diag.print(length);
    if (type == USB_DESCRIPTOR_CONFIGURATION && length >= 9) {
      Diag.print(F(" config=")); Diag.print(head[5]);
      Diag.print(F(" interfaces=")); Diag.print(head[4]);
      Diag.print(F(" power_mA=")); Diag.print(uint16_t(head[8]) * 2);
    } else if (type == USB_DESCRIPTOR_INTERFACE && length >= 9) {
      Diag.print(F(" interface=")); Diag.print(head[2]);
      Diag.print(F(" alternate=")); Diag.print(head[3]);
      Diag.print(F(" endpoints=")); Diag.print(head[4]);
      Diag.print(F(" class/subclass/protocol="));
      hexByte(head[5]); Diag.print('/');
      hexByte(head[6]); Diag.print('/'); hexByte(head[7]);
    } else if (type == USB_DESCRIPTOR_ENDPOINT && length >= 7) {
      Diag.print(F(" address=0x")); hexByte(head[2]);
      Diag.print(F(" attributes=0x")); hexByte(head[3]);
      Diag.print(F(" maxPacket="));
      Diag.print(uint16_t(head[4]) | (uint16_t(head[5]) << 8));
      Diag.print(F(" interval=")); Diag.print(head[6]);
    }
    Diag.println();
  }

public:
  DescriptorSummary() : used(0), length(0), invalid(false) {}

  void Parse(const uint16_t len, const uint8_t *data,
             const uint16_t &offset) override {
    if (offset == 0) { used = 0; length = 0; invalid = false; }
    if (invalid) return;
    for (uint16_t i = 0; i < len; ++i) {
      if (used == 0) {
        length = data[i];
        if (length < 2) {
          invalid = true;
          Diag.println(F("  malformed descriptor length"));
          return;
        }
      }
      if (used < sizeof(head)) head[used] = data[i];
      ++used;
      if (used == length) { emit(); used = 0; }
    }
  }

  bool complete() const { return !invalid && used == 0; }
};

void reportDevice(UsbDevice *device) {
  const uint8_t address = device->address.devAddress;
  uint8_t first[8];
  Diag.print(F("Device address=")); Diag.println(address);
  Diag.println(device->lowspeed ? F("LOW speed: unsuitable for Ethernet bulk transfers")
                                 : F("FULL speed (12 Mbit/s host)"));
  uint8_t error = Usb.getDevDescr(address, 0, sizeof(first), first);
  if (error) {
    Diag.print(F("Device descriptor prefix error=0x")); hexByte(error);
    Diag.println(); return;
  }
  const uint8_t packet = first[7];
  if (!device->epinfo || (packet != 8 && packet != 16 && packet != 32 && packet != 64)) {
    Diag.println(F("Invalid endpoint-zero packet size")); return;
  }
  // The library's generic addressing path leaves EP0 at its initial size.
  device->epinfo->maxPktSize = packet;
  USB_DEVICE_DESCRIPTOR descriptor;
  error = Usb.getDevDescr(address, 0, sizeof(descriptor),
                         reinterpret_cast<uint8_t *>(&descriptor));
  if (error) {
    Diag.print(F("Device descriptor error=0x")); hexByte(error);
    Diag.println(); return;
  }
  Diag.print(F("VID:PID=")); hexWord(descriptor.idVendor);
  Diag.print(':'); hexWord(descriptor.idProduct);
  Diag.print(F(" bcdDevice=0x")); hexWord(descriptor.bcdDevice);
  Diag.print(F(" EP0=")); Diag.println(packet);
  for (uint8_t config = 0; config < descriptor.bNumConfigurations; ++config) {
    Diag.print(F("Configuration index=")); Diag.println(config);
    DescriptorSummary summary;
    error = Usb.getConfDescr(address, 0, config, &summary);
    if (error) {
      Diag.print(F("Configuration error=0x")); hexByte(error); Diag.println();
    } else if (!summary.complete()) {
      Diag.println(F("Incomplete or malformed configuration"));
    }
  }
}

void begin() {
  Serial.begin(115200);
  ptpLogPrefix(); Serial.print(F("boot role "));
  Serial.println(TINYPTP_BOOT_ROLE == 1 ? F("GRANDMASTER") :
                 TINYPTP_BOOT_ROLE == 2 ? F("FOLLOWER") : F("DIAGNOSTIC"));
  ptpLogPrefix(); Serial.println(F("serial 115200 8N1; offset/path ns; frequency ppb"));
  // Common shields also carry an SD socket. Keep its chip select inactive.
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Diag.println(F("TimeStick USB descriptor probe"));
  const int8_t initResult = Usb.Init();
  hostReady = initResult == 0;
  Diag.print(F("MAX3421E init result=")); Diag.println(initResult);
  Diag.print(F("MAX3421E revision=0x")); hexByte(Usb.regRd(rREVISION));
  Diag.println();
  if (!hostReady) {
    Diag.println(F("MAX3421E initialization failed: check shield/power/SPI"));
    reportHostRegisters(125000UL);
    reportHostRegisters(1000000UL);
    reportHostRegisters(8000000UL);
    Diag.println(F("Expected revision: 01, 12, or 13. Repeated 00/FF suggests no SPI response."));
  }
}

void poll() {
  if (!hostReady) {
    if (automaticRoleEnabled && int32_t(millis() - bootRetryAt) >= 0) {
      hostReady = Usb.Init() == 0; bootRetryAt = millis() + 5000;
      ptpLogPrefix(); Serial.println(hostReady ? F("USB host ready") : F("USB host init failed; retrying"));
    }
    return;
  }
  Usb.Task();
  const uint8_t state = Usb.getUsbTaskState();
  if (state != lastState) {
    Diag.print(F("USB state=0x")); hexByte(state); Diag.println();
    lastState = state;
  }
  if (state != USB_STATE_RUNNING) {
    if (reported) { ptpLogPrefix(); Serial.println(F("port 1: USB disconnected; waiting")); }
    reported = false; hardwareReady = false; ethernetReady = false;
    ptpRunning = false; clearPtpExchange();
    verifiedAsixAddress = 0; return;
  }
  if (!reported) {
    Usb.ForEachUsbDevice(reportDevice);
    Diag.println(F("Commands: r descriptors; v reads; i PHY/clock; e Ethernet; t echo; p PTP observe; s PTP servo; m master; f follower; l PHY; d debug; a auto; x stop."));
    reported = true;
  }
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'e' || command == 'i' || command == 'v' || command == 't' || command == 'x') {
      ptpRunning = false; clearPtpExchange();
    }
    if (command == 'x') automaticRoleEnabled = false;
    if (command == 'a') { automaticRoleEnabled = TINYPTP_BOOT_ROLE != 0; bootRetryAt = millis(); }
    if (command == 'd') { diagnosticsEnabled = !diagnosticsEnabled; }
    if (command == 'r') reported = false;
    if (command == 'l') reportPhyRegisters();
    if (command == 'q') reportPpsState();
    if (command == 'v' || command == 'i') ethernetReady = false;
    if (command == 'v') Usb.ForEachUsbDevice(reportAsixControls);
    if (command == 'i') Usb.ForEachUsbDevice(initializeAsixHardware);
    if (command == 'e') Usb.ForEachUsbDevice(initializeEthernetProbe);
    if (command == 't') runEthernetProbe();
    if (command == 'p') startPtpClient(false);
    if (command == 's') startPtpClient(true);
    if (command == 'm') startPtpClient(false, true);
    if (command == 'f') startPtpClient(true, false, true);
    if (command == 'x') ptpSetFrequency(0);
  }
  if (automaticRoleEnabled && !ptpRunning && int32_t(millis() - bootRetryAt) >= 0) {
    ptpLogPrefix(); Serial.println(F("port 1: initializing Ethernet and hardware clock"));
    Usb.ForEachUsbDevice(initializeEthernetProbe);
    if (ethernetReady) startPtpClient(TINYPTP_BOOT_ROLE == 2,
                                     TINYPTP_BOOT_ROLE == 1, TINYPTP_BOOT_ROLE == 2);
    if (!ptpRunning) { ptpLogPrefix(); Serial.println(F("initialization failed; retrying in 5 seconds (d for diagnostics)")); }
    bootRetryAt = millis() + 5000;
  }
  pollPtpClient();
}
