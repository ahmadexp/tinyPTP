#pragma once
#include "BuildConfig.h"
#include <stdint.h>

// Register addresses from ASIX's ax_ptp.h/ax_ptp.c, reference revision
// 42feb1252fe0669845a8d6714bcc77bfc19eea1d. No nonvolatile writes.
namespace AsixChip {
constexpr uint8_t code = TINYPTP_AX88279A ? 15 : 7;
constexpr uint8_t clockRequest = TINYPTP_AX88279A ? 0x11 : 0x13;
constexpr uint16_t clockValue = TINYPTP_AX88279A ? 0x1010 : 2;
constexpr uint16_t clockIndex = TINYPTP_AX88279A ? 0x12 : 0x1014;
constexpr uint8_t clockBytes = TINYPTP_AX88279A ? 12 : 10;
constexpr uint16_t setClock = TINYPTP_AX88279A ? 0x1004 : 0x1008;
constexpr uint16_t addendRegister = TINYPTP_AX88279A ? 0x102c : 0x1030;
constexpr uint32_t baseAddend = TINYPTP_AX88279A ? 0x80000000UL : 0xccccccccUL;
constexpr uint16_t pulseWidthRegister = TINYPTP_AX88279A ? 0x103c : 0x1040;
constexpr uint32_t ppsEnable = TINYPTP_AX88279A ? 0x20UL : 0x10UL;
constexpr uint16_t receiveDelayNs = TINYPTP_AX88279A ? 250 : 0;
}
