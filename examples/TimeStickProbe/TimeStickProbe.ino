// Interactive USB, Ethernet, PPS, and PTP diagnostics.
// Hardware: classic UNO R3 + MAX3421E USB Host Shield + AX88279 TimeStick.
// Install tinyPTP and USB Host Shield Library 2.0 before compiling.
// Serial Monitor: 115200 baud. Connect the two TimeSticks directly by Ethernet.
// Role: 0 diagnostic, 1 grandmaster, 2 follower.
// Servo: 0 PI, 1 linreg, 2 adaptive PI, 3 Kalman, 4 PLL/FLL.
#ifndef TINYPTP_BOOT_ROLE
#define TINYPTP_BOOT_ROLE 0
#endif
#ifndef TINYPTP_SERVO
#define TINYPTP_SERVO 0
#endif
#include <TinyPTP.h>

void setup() {
  TinyPTP::begin();
}

void loop() {
  TinyPTP::poll();
}
