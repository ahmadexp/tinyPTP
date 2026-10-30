// UNO R3 + MAX3421E shield + AX88279A (TimeStick V3 or development board).
// Connect directly to the AX88279AFollowerPI node by Ethernet.
// Serial Monitor: 115200 baud. Free-running time, no UTC reference.
#define TINYPTP_AX88279A 1
#define TINYPTP_BOOT_ROLE 1
#define TINYPTP_SERVO 0
#define TINYPTP_FIRST_STEP_THRESHOLD_NS 1000000LL
#include <TinyPTP.h>

void setup() { TinyPTP::begin(); }
void loop() { TinyPTP::poll(); }
