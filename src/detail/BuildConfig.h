#pragma once

// Select the Ethernet silicon explicitly. AX88279A has a different PTP
// register map and report format despite sharing the USB VID/PID.
#ifndef TINYPTP_AX88279A
#define TINYPTP_AX88279A 0
#endif
#if TINYPTP_AX88279A != 0 && TINYPTP_AX88279A != 1
#error "TINYPTP_AX88279A must be 0 (AX88279) or 1 (AX88279A)"
#endif

// 0: interactive diagnostic, 1: automatic grandmaster, 2: automatic follower.
// Select in the .ino before TinyPTP.h, or override via Arduino CLI flags.
#ifndef TINYPTP_BOOT_ROLE
#define TINYPTP_BOOT_ROLE 0
#endif
#ifndef TINYPTP_FIRST_STEP_THRESHOLD_NS
#if defined(ARDUINO_UNOR4_MINIMA)
#define TINYPTP_FIRST_STEP_THRESHOLD_NS 1000000LL
#else
#define TINYPTP_FIRST_STEP_THRESHOLD_NS 500000000LL
#endif
#endif
#if TINYPTP_FIRST_STEP_THRESHOLD_NS < 1000000LL || TINYPTP_FIRST_STEP_THRESHOLD_NS > 500000000LL
#error "TINYPTP_FIRST_STEP_THRESHOLD_NS must be 1000000..500000000 ns"
#endif

// 0: fixed PI; 1: linear regression; 2: adaptive PI; 3: two-state Kalman; 4: PLL/FLL.
#ifndef TINYPTP_SERVO
#define TINYPTP_SERVO 0
#endif
#if TINYPTP_SERVO < 0 || TINYPTP_SERVO > 4
#error "TINYPTP_SERVO must be 0 (PI), 1 (LINREG), 2 (AUTO_PI), 3 (KALMAN), or 4 (PLL_FLL)"
#endif
#if TINYPTP_BOOT_ROLE < 0 || TINYPTP_BOOT_ROLE > 2
#error "TINYPTP_BOOT_ROLE must be 0, 1, or 2"
#endif
#ifndef TINYPTP_PTP4L_LOG
#define TINYPTP_PTP4L_LOG (TINYPTP_BOOT_ROLE != 0)
#endif

// The R4 has 32 KB SRAM. Retain larger ASIX aggregates and service several
// USB packets per loop, while preserving the R3's memory and polling budgets.
#ifndef TINYPTP_RX_BUFFER_SIZE
#if defined(ARDUINO_UNOR4_MINIMA)
#define TINYPTP_RX_BUFFER_SIZE 4096
#else
#define TINYPTP_RX_BUFFER_SIZE 256
#endif
#endif
#if TINYPTP_RX_BUFFER_SIZE < 256 || TINYPTP_RX_BUFFER_SIZE > 8192
#error "TINYPTP_RX_BUFFER_SIZE must be 256..8192 bytes"
#endif
#ifndef TINYPTP_USB_POLL_BUDGET
#if defined(ARDUINO_UNOR4_MINIMA)
#define TINYPTP_USB_POLL_BUDGET 4
#else
#define TINYPTP_USB_POLL_BUDGET 1
#endif
#endif
#if TINYPTP_USB_POLL_BUDGET < 1 || TINYPTP_USB_POLL_BUDGET > 16
#error "TINYPTP_USB_POLL_BUDGET must be 1..16"
#endif
#ifndef TINYPTP_CPU_PROFILE
#if defined(ARDUINO_UNOR4_MINIMA)
#define TINYPTP_CPU_PROFILE 1
#else
#define TINYPTP_CPU_PROFILE 0
#endif
#endif
