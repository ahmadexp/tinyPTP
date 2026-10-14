#pragma once

// 0: interactive diagnostic, 1: automatic grandmaster, 2: automatic follower.
// Set here in the IDE, or override via compiler.cpp.extra_flags in arduino-cli.
#ifndef TINYPTP_BOOT_ROLE
#define TINYPTP_BOOT_ROLE 0
#endif

// 0: fixed PI; 1: linear regression; 2: adaptive PI; 3: two-state Kalman.
#ifndef TINYPTP_SERVO
#define TINYPTP_SERVO 0
#endif
#if TINYPTP_SERVO < 0 || TINYPTP_SERVO > 3
#error "TINYPTP_SERVO must be 0 (PI), 1 (LINREG), 2 (AUTO_PI), or 3 (KALMAN)"
#endif
#if TINYPTP_BOOT_ROLE < 0 || TINYPTP_BOOT_ROLE > 2
#error "TINYPTP_BOOT_ROLE must be 0, 1, or 2"
#endif
#ifndef TINYPTP_PTP4L_LOG
#define TINYPTP_PTP4L_LOG (TINYPTP_BOOT_ROLE != 0)
#endif
