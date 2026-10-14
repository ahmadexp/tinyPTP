#pragma once

// 0: interactive diagnostic, 1: automatic grandmaster, 2: automatic follower.
// Set here in the IDE, or override via compiler.cpp.extra_flags in arduino-cli.
#ifndef TINYPTP_BOOT_ROLE
#define TINYPTP_BOOT_ROLE 0
#endif

// 0: existing PI controller; 1: experimental eight-sample linear regression.
#ifndef TINYPTP_SERVO
#define TINYPTP_SERVO 0
#endif
#if TINYPTP_SERVO < 0 || TINYPTP_SERVO > 1
#error "TINYPTP_SERVO must be 0 (PI) or 1 (LINREG)"
#endif
#if TINYPTP_BOOT_ROLE < 0 || TINYPTP_BOOT_ROLE > 2
#error "TINYPTP_BOOT_ROLE must be 0, 1, or 2"
#endif
#ifndef TINYPTP_PTP4L_LOG
#define TINYPTP_PTP4L_LOG (TINYPTP_BOOT_ROLE != 0)
#endif
