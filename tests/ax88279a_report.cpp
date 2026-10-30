#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "../src/detail/Ax88279aReport.h"

static void record(uint8_t *p, uint16_t seq, uint8_t flags=0x41) {
  p[0]=50; p[4]=2; p[10]=seq; p[11]=seq>>8; p[12]=flags;
}
static bool transfer(Ax88279aReport &report, const uint8_t *data, uint16_t seq) {
  report.reset();
  for(unsigned i=0;i<16;++i) {
    assert(!report.valid());
    const bool done=report.feed(data+i*64,64,1,seq);
    assert(done==(i==15));
  }
  return report.valid();
}
int main() {
  uint8_t data[1024]={}; Ax88279aReport report;
  // Firmware 1.2.9.0, first Sync in timestamp-format-probe.log.
  const uint8_t observed[] = {0x28,0xcb,0xee,0x02,4,0,0,0,0,0,1,0,0x40,0,0,0};
  assert(report.feed(observed,sizeof(observed),0,1) && report.valid());
  assert(!memcmp(report.candidate,observed,sizeof(observed)));
  record(data+31*16,0xabcd);
  assert(transfer(report,data,0xabcd));
  assert(report.candidate[0]==50 && report.candidate[10]==0xcd);
  assert(!transfer(report,data,0xabce)); // Other exchanges do not match.
  record(data,0xabcd);assert(!transfer(report,data,0xabcd)); // Ambiguous duplicate.
  memset(data,0,sizeof(data));record(data+512,0xabcd);
  assert(!transfer(report,data,0xabcd)); // Ignore the non-record half.
  for(uint8_t flags: {uint8_t(0x21),uint8_t(0x51),uint8_t(0x71),uint8_t(0x40)}) {
    memset(data,0,sizeof(data));record(data,0xabcd,flags);
    assert(!transfer(report,data,0xabcd)); // Invalid status, user-tagged packets, wrong type.
  }
  memset(data,0,sizeof(data));record(data,0xabcd);
  report.reset();assert(!report.feed(data,64,1,0xabcd));
  assert(report.feed(data+64,31,1,0xabcd) && !report.valid());
  report.reset();assert(report.feed(data,65,1,0xabcd) && !report.valid());
  report.reset();assert(report.feed(data,0,1,0xabcd) && !report.valid());
  for (unsigned length : {16,32,48,64,128,512}) {
    memset(data,0,sizeof(data));record(data,0xabcd);
    report.reset();
    for(unsigned offset=0;offset<length;offset+=64) {
      unsigned n=length-offset;if(n>64)n=64;
      bool done=report.feed(data+offset,n,1,0xabcd);
      assert(done==(n<64));
    }
    if (!(length%64)) {
      assert(!report.valid());
      assert(report.feed(data,0,1,0xabcd));
    }
    assert(report.valid());
  }
  // Firmware may set bit 5; status and packet identity remain decisive.
  memset(data,0,sizeof(data));record(data,0xabcd,0x61);
  report.reset();assert(report.feed(data,16,1,0xabcd) && report.valid());
  assert(report.feed(data,16,1,0xabcd) && !report.valid());
  static_assert(sizeof(Ax88279aReport)<48,"Keep report state bounded on AVR");
}
