#include <SPI.h>
#include <Usb.h>

// USB bring-up for one TimeStick directly attached to a MAX3421E.
// Descriptors on boot; optional Ethernet and fixed-peer PTP diagnostics.
// This is an experimental bounded client, not a general-purpose PTP stack.
USB Usb;
bool reported = false;
bool hostReady = false;
uint8_t lastState = 0xff;

void hexByte(uint8_t value) {
  if (value < 16) Serial.print('0');
  Serial.print(value, HEX);
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
  Serial.print(F("SPI Hz=")); Serial.print(hz);
  Serial.print(F(" REVISION=")); hexByte(slowRegisterRead(rREVISION, hz));
  Serial.print(F(" USBIRQ=")); hexByte(slowRegisterRead(rUSBIRQ, hz));
  Serial.print(F(" USBCTL=")); hexByte(slowRegisterRead(rUSBCTL, hz));
  Serial.print(F(" PINCTL=")); hexByte(slowRegisterRead(rPINCTL, hz));
  Serial.println();
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
    Serial.print(F("  descriptor type=0x")); hexByte(type);
    Serial.print(F(" length=")); Serial.print(length);
    if (type == USB_DESCRIPTOR_CONFIGURATION && length >= 9) {
      Serial.print(F(" config=")); Serial.print(head[5]);
      Serial.print(F(" interfaces=")); Serial.print(head[4]);
      Serial.print(F(" power_mA=")); Serial.print(uint16_t(head[8]) * 2);
    } else if (type == USB_DESCRIPTOR_INTERFACE && length >= 9) {
      Serial.print(F(" interface=")); Serial.print(head[2]);
      Serial.print(F(" alternate=")); Serial.print(head[3]);
      Serial.print(F(" endpoints=")); Serial.print(head[4]);
      Serial.print(F(" class/subclass/protocol="));
      hexByte(head[5]); Serial.print('/');
      hexByte(head[6]); Serial.print('/'); hexByte(head[7]);
    } else if (type == USB_DESCRIPTOR_ENDPOINT && length >= 7) {
      Serial.print(F(" address=0x")); hexByte(head[2]);
      Serial.print(F(" attributes=0x")); hexByte(head[3]);
      Serial.print(F(" maxPacket="));
      Serial.print(uint16_t(head[4]) | (uint16_t(head[5]) << 8));
      Serial.print(F(" interval=")); Serial.print(head[6]);
    }
    Serial.println();
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
          Serial.println(F("  malformed descriptor length"));
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
  Serial.print(F("Device address=")); Serial.println(address);
  Serial.println(device->lowspeed ? F("LOW speed: unsuitable for Ethernet bulk transfers")
                                 : F("FULL speed (12 Mbit/s host)"));
  uint8_t error = Usb.getDevDescr(address, 0, sizeof(first), first);
  if (error) {
    Serial.print(F("Device descriptor prefix error=0x")); hexByte(error);
    Serial.println(); return;
  }
  const uint8_t packet = first[7];
  if (!device->epinfo || (packet != 8 && packet != 16 && packet != 32 && packet != 64)) {
    Serial.println(F("Invalid endpoint-zero packet size")); return;
  }
  // The library's generic addressing path leaves EP0 at its initial size.
  device->epinfo->maxPktSize = packet;
  USB_DEVICE_DESCRIPTOR descriptor;
  error = Usb.getDevDescr(address, 0, sizeof(descriptor),
                         reinterpret_cast<uint8_t *>(&descriptor));
  if (error) {
    Serial.print(F("Device descriptor error=0x")); hexByte(error);
    Serial.println(); return;
  }
  Serial.print(F("VID:PID=")); hexWord(descriptor.idVendor);
  Serial.print(':'); hexWord(descriptor.idProduct);
  Serial.print(F(" bcdDevice=0x")); hexWord(descriptor.bcdDevice);
  Serial.print(F(" EP0=")); Serial.println(packet);
  for (uint8_t config = 0; config < descriptor.bNumConfigurations; ++config) {
    Serial.print(F("Configuration index=")); Serial.println(config);
    DescriptorSummary summary;
    error = Usb.getConfDescr(address, 0, config, &summary);
    if (error) {
      Serial.print(F("Configuration error=0x")); hexByte(error); Serial.println();
    } else if (!summary.complete()) {
      Serial.println(F("Incomplete or malformed configuration"));
    }
  }
}

void setup() {
  Serial.begin(115200);
  // Common shields also carry an SD socket. Keep its chip select inactive.
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial.println(F("TimeStick USB descriptor probe"));
  const int8_t initResult = Usb.Init();
  hostReady = initResult == 0;
  Serial.print(F("MAX3421E init result=")); Serial.println(initResult);
  Serial.print(F("MAX3421E revision=0x")); hexByte(Usb.regRd(rREVISION));
  Serial.println();
  if (!hostReady) {
    Serial.println(F("MAX3421E initialization failed: check shield/power/SPI"));
    reportHostRegisters(125000UL);
    reportHostRegisters(1000000UL);
    reportHostRegisters(8000000UL);
    Serial.println(F("Expected revision: 01, 12, or 13. Repeated 00/FF suggests no SPI response."));
  }
}

void loop() {
  if (!hostReady) return;
  Usb.Task();
  const uint8_t state = Usb.getUsbTaskState();
  if (state != lastState) {
    Serial.print(F("USB state=0x")); hexByte(state); Serial.println();
    lastState = state;
  }
  if (state != USB_STATE_RUNNING) {
    reported = false; hardwareReady = false; ethernetReady = false;
    ptpRunning = false; clearPtpExchange();
    verifiedAsixAddress = 0; return;
  }
  if (!reported) {
    Usb.ForEachUsbDevice(reportDevice);
    Serial.println(F("Commands: r descriptors; v reads; i PHY/clock; e Ethernet; t echo; p PTP observe; s PTP servo; m master; f follower; l PHY; x stop."));
    reported = true;
  }
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'e' || command == 'i' || command == 'v' || command == 't' || command == 'x') {
      ptpRunning = false; clearPtpExchange();
    }
    if (command == 'r') reported = false;
    if (command == 'l') reportPhyRegisters();
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
  pollPtpClient();
}
