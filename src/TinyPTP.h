#pragma once

// Include once, in the main .ino, after selecting TINYPTP_BOOT_ROLE and
// TINYPTP_SERVO. Compiling in the sketch preserves compile-time selection
// without building every servo into the UNO's limited flash.
#include <Arduino.h>
#include <SPI.h>
#include <Usb.h>

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error "tinyPTP currently targets a 16 MHz ATmega328P (classic Arduino UNO R3)"
#endif

namespace tinyptp_detail {
#include "detail/Application.h"
}

namespace TinyPTP {
// Initialize serial at 115200 baud and the MAX3421E USB host controller.
inline void begin() { tinyptp_detail::begin(); }

// Call frequently from loop(). Services USB, role startup, PTP, and serial.
// Initialization and diagnostic commands may block; avoid other long delays.
inline void poll() { tinyptp_detail::poll(); }
}
