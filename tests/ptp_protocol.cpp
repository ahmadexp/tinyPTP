#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#define F(x) x
#define HEX 16
const uint8_t hrNAK = 4;
struct SerialStub {
  template<class T> void print(T) {}
  template<class T> void println(T) {}
  template<class T> void println(T,int) {}
  void println() {}
} Serial;
uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }
void delay(unsigned) {}
void hexByte(uint8_t) {}
bool ethernetReady=true,hardwareReady=true;
uint8_t verifiedAsixAddress=1,probeMac[6]={0x9c,0x69,0xd3,0x2b,0xa1,0x2f};
uint8_t rxAggregate[256]; uint16_t rxUsed=0; bool rxOverflow=false;
uint32_t readLe32(const uint8_t *p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
bool asixRead(uint8_t,uint8_t,uint16_t,uint16_t,uint8_t,uint8_t*) {return false;}
bool asixWrite(uint8_t,uint8_t,uint16_t,uint16_t,uint8_t,uint32_t) {return true;}
bool pbusWrite(uint8_t,uint16_t,uint16_t,uint32_t) {return true;}
bool macWrite(uint8_t,uint8_t,uint32_t) {return true;}
bool reportClockProgress(uint8_t) {return true;}
struct UsbStub {
  std::vector<uint8_t> sent;
  std::vector<std::vector<uint8_t>> queued;
  uint8_t ctrlReq(uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint16_t,uint16_t,uint16_t,uint8_t*,void*) {return 0;}
  uint8_t outTransfer(uint8_t,uint8_t,uint16_t n,uint8_t *p) {sent.assign(p,p+n);return 0;}
  uint8_t inTransfer(uint8_t,uint8_t,uint16_t *n,uint8_t *p) {
    if(queued.empty()) return hrNAK;
    assert(queued.front().size()<=*n); *n=queued.front().size();
    std::copy(queued.front().begin(),queued.front().end(),p);queued.erase(queued.begin());return 0;
  }
} Usb;
#include "../examples/TimeStickProbe/PtpClient.h"
void be32(uint8_t *p,uint32_t n) {for(int i=3;i>=0;--i){p[i]=n;n>>=8;}}
void le32(uint8_t *p,uint32_t n) {for(int i=0;i<4;++i){p[i]=n;n>>=8;}}
void header(uint8_t *f,uint8_t type,uint16_t seq,uint16_t length) {
  memset(f,0,80);f[12]=0x88;f[13]=0xf7;
  uint8_t *h=f+14;h[0]=type;h[1]=2;h[3]=length;h[4]=42;
  h[6]=2;memcpy(h+20,ptpMaster,10);h[30]=seq>>8;h[31]=seq;
}
int main() {
  uint8_t frame[80], stamp[10]={};
  for(int i=0;i<10;++i) ptpIdentity[i]=i+1;
  le32(stamp,1200);le32(stamp+4,1700000000);
  header(frame,0,7,44);frame[18]=41;
  handlePtpFrame(frame,60,stamp);assert(!haveSync);
  frame[18]=42;frame[34]^=1;
  handlePtpFrame(frame,60,stamp);assert(!haveSync);frame[34]^=1;
  handlePtpFrame(frame,60,nullptr);assert(!haveSync && ptpRxMissing==1);
  handlePtpFrame(frame,60,stamp);assert(haveSync);
  header(frame,8,8,44);be32(frame+50,1700000000);
  handlePtpFrame(frame,60,nullptr);assert(!haveFollow);
  frame[45]=7;handlePtpFrame(frame,60,nullptr);
  assert(haveFollow && delaySequence==1 && Usb.sent.size()==72);
  assert(Usb.sent[22]==1 && Usb.sent[26]==42);
  header(frame,9,1,54);be32(frame+50,1700000000);be32(frame+54,10002000);
  memcpy(frame+58,ptpIdentity,10);frame[58]^=1;
  handlePtpFrame(frame,68,nullptr);assert(!haveResponse);frame[58]^=1;
  handlePtpFrame(frame,68,nullptr);assert(haveResponse && !haveTx);
  std::vector<uint8_t> report(131);report[65]=0x18;report[66]=1;
  le32(report.data()+68,10001200);le32(report.data()+72,1700000000);report[130]=2;
  Usb.queued={{report.begin(),report.begin()+64},{report.begin()+64,report.begin()+128},{report.begin()+128,report.end()}};
  pollPtpTxTimestamp();pollPtpTxTimestamp();assert(!haveTx);
  pollPtpTxTimestamp();assert(ptpSamples==1 && !haveSync && !haveResponse);
  // A timestamped Sync inside a complete 96-byte ASIX aggregate.
  memset(rxAggregate,0,sizeof(rxAggregate));header(frame,0,8,44);
  memcpy(rxAggregate,frame,60);rxAggregate[65]=8;rxAggregate[66]=60;rxAggregate[71]=1;
  memcpy(rxAggregate+72,stamp,10);le32(rxAggregate+88,(64UL<<13)|1);rxUsed=96;
  parsePtpAggregate();assert(haveSync && syncSequence==8);clearPtpExchange();
  le32(rxAggregate+88,(255UL<<13)|1);parsePtpAggregate();assert(!haveSync && ptpRxDiscard);
  // Exercise malformed aggregate bounds under ASan/UBSan.
  uint32_t random=17;
  for(unsigned trial=0;trial<20000;++trial) {
    for(auto &byte:rxAggregate) {random^=random<<13;random^=random>>17;random^=random<<5;byte=random;}
    rxUsed=trial%257;parsePtpAggregate();
  }
}
