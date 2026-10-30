// UNO R3 + MAX3421E shield + AX88279A (TimeStick V3 or development board).
// Fixed-role PI follower. Serial Monitor: 115200 baud.
// The initial coarse clock step uses a 1 ms threshold; runtime remains 500 ms.
#define TINYPTP_AX88279A 1
#define TINYPTP_BOOT_ROLE 2
#define TINYPTP_SERVO 0
#define TINYPTP_FIRST_STEP_THRESHOLD_NS 1000000LL
#include <TinyPTP.h>

void setup() { TinyPTP::begin(); }
void loop() { TinyPTP::poll(); }
