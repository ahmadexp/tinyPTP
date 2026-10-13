#include <cassert>
#include <cstdint>
#define F(x) x
#define HEX 16
struct SerialStub {
  template<class T> void print(T) {}
  template<class T> void print(T,int) {}
  template<class T> void println(T) {}
  template<class T> void println(T,int) {}
} Serial;
void ptpLogPrefix() {}
uint8_t verifiedAsixAddress = 1;
uint32_t gate = 0x12340001, clockControl = 0x5f, width = 500000000;
bool readOk = true, writeOk = true, latchWrite = true;
unsigned writes = 0;
uint32_t readLe32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool asixRead(uint8_t address, uint8_t request, uint16_t reg,
              uint16_t bank, uint8_t length, uint8_t *data) {
  assert(address == 1 && request == 0x11 && length == 4);
  assert((bank == 0x0c && reg == 0xf8c8) ||
         (bank == 0x12 && (reg == 0x1000 || reg == 0x1040)));
  uint32_t value = bank == 0x0c ? gate : (reg == 0x1000 ? clockControl : width);
  for (int i = 0; i < 4; ++i) data[i] = value >> (8 * i);
  return readOk;
}
bool pbusWrite(uint8_t address, uint16_t bank, uint16_t reg, uint32_t value) {
  assert(address == 1 && bank == 0x0c && reg == 0xf8c8);
  assert(value == (gate | 0x4000)); // Preserve unrelated bits.
  ++writes;
  if (writeOk && latchWrite) gate = value;
  return writeOk;
}
#include "../examples/TimeStickProbe/PpsOutput.h"
int main() {
  assert(!reportPpsState());
  assert(enablePpsOutput() && gate == 0x12344001 && writes == 1);
  assert(enablePpsOutput() && writes == 1); // Already enabled, no write.
  clockControl &= ~0x10; assert(!reportPpsState()); clockControl |= 0x10;
  width = 0; assert(!reportPpsState()); width = 500000000;
  gate &= ~0x4000; writeOk = false; assert(!enablePpsOutput());
  writeOk = true; latchWrite = false; assert(!enablePpsOutput());
  unsigned previousWrites = writes;
  readOk = false; assert(!enablePpsOutput() && writes == previousWrites);
  readOk = true; gate = 0xdeadbeef; assert(!enablePpsOutput() && writes == previousWrites);
  verifiedAsixAddress = 0; assert(!enablePpsOutput() && writes == previousWrites);
}
