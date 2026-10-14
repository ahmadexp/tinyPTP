#include <cassert>
#include "../src/detail/PtpMath.h"
int main() {
  PtpTime a{100,999999900}, b{101,100};
  assert(ptpDiff(b,a)==200);
  assert(ptpAdd(a,200) && a.sec==101 && a.ns==100);
  assert(ptpAdd(a,-201) && a.sec==100 && a.ns==999999899);
  PtpTime zero{0,0}; assert(!ptpAdd(zero,-1));
  uint8_t wire[10]={0,0,0x6b,0x00,0x00,0x01,0,0,0,42};
  assert(ptpWireTime(wire,a) && a.sec==0x6b000001UL && a.ns==42);
  wire[0]=1; assert(!ptpWireTime(wire,a)); wire[0]=0;
  wire[6]=0xff; assert(!ptpWireTime(wire,a));
  uint8_t negative[8]={255,255,255,255,255,255,128,0};
  assert(ptpCorrection(negative)==-32768);
  int64_t off,delay;
  PtpTime t1{1700000000,0},t2{1700000000,1200},t3{1700000000,10001200},t4{1700000000,10002000};
  ptpExchange(t1,t2,t3,t4,0,0,off,delay); assert(off==200 && delay==1000);
  ptpExchange(t1,t2,t3,t4,100LL*65536,300LL*65536,off,delay); assert(off==300 && delay==800);
  t2.sec=0; t3.sec=0;
  ptpExchange(t1,t2,t3,t4,0,0,off,delay); assert(off==-1700000000000000000LL+200 && delay==1000);
}
