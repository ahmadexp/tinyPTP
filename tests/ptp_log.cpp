#include <cassert>
#include <cstdint>
#include <sstream>
#include <limits>
#define F(x) x
struct SerialSpy {
  std::ostringstream output;
  template<class T> void print(T value) { output << value; }
  void print(uint8_t value) { output << unsigned(value); }
  template<class T> void println(T value) { print(value); output << '\n'; }
  void println() { output << '\n'; }
} Serial;
uint32_t nowMs = 1005;
uint32_t millis() { return nowMs; }
#include "../examples/TimeStickProbe/PtpLog.h"
int main() {
  ptpLogSample(-150, 735, -10500, 3, 7, 9);
  assert(Serial.output.str()=="tinyptp[1.005]: master offset -150 s3 freq -10500 path delay 735 sample 7 seq 9\n");
  Serial.output.str(""); nowMs=4294967295UL;
  ptpLogSample(0, 0, 123, 1, 8, 10);
  assert(Serial.output.str()=="tinyptp[4294967.295]: master offset 0 s1 freq +123 path delay 0 sample 8 seq 10\n");
  Serial.output.str(""); printSigned64(std::numeric_limits<int64_t>::min());
  assert(Serial.output.str()=="-9223372036854775808");
  Serial.output.str(""); const uint8_t identity[8]={0x9c,0x69,0xd3,0xff,0xfe,0x2b,0xa1,0x8d};
  ptpLogIdentity(identity); assert(Serial.output.str()=="9c69d3.fffe.2ba18d");
}
