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
  template<class T> void print(T,int) {}
  template<class T> void println(T) {}
  template<class T> void println(T,int) {}
  void println() {}
} Serial;
#define Diag Serial
bool diagnosticsEnabled = true;
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
  uint8_t outError = 0;
  std::vector<std::vector<uint8_t>> queued;
  uint8_t ctrlReq(uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint16_t,uint16_t,uint16_t,uint8_t*,void*) {return 0;}
  uint8_t outTransfer(uint8_t,uint8_t,uint16_t n,uint8_t *p) {sent.assign(p,p+n);return outError;}
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
  // Direct follower binds only a valid timestamped two-step Sync, then pins it.
  ptpLearnMaster=true;ptpPeerBound=false;
  header(frame,8,10,44);handlePtpFrame(frame,60,stamp);assert(!ptpPeerBound);
  header(frame,0,10,44);handlePtpFrame(frame,60,nullptr);assert(!ptpPeerBound);
  frame[20]=0;handlePtpFrame(frame,60,stamp);assert(!ptpPeerBound);frame[20]=2;
  handlePtpFrame(frame,60,stamp);assert(ptpPeerBound && haveSync);
  assert(!memcmp(selectedMaster,ptpMaster,10));clearPtpExchange();
  frame[34]^=1;handlePtpFrame(frame,60,stamp);assert(!haveSync);
  ptpLearnMaster=false;

  // Master sends two-step Sync at 1 Hz, no Follow_Up until matching hardware TX.
  ptpMasterRole=true;ptpRunning=true;ptpSamples=0;ptpTimeouts=0;
  nowMs=10000;masterLastSync=masterLastAnnounce=nowMs;syncSequence=65535;
  Usb.sent.clear();pollPtpMasterSchedule();assert(Usb.sent.empty());
  nowMs+=1000;pollPtpMasterSchedule();assert(masterSyncPending && syncSequence==0);
  assert(Usb.sent.size()==72 && Usb.sent[22]==0 && Usb.sent[28]==2);
  assert(ptpBe16(Usb.sent.data()+24)==44 && !memcmp(Usb.sent.data()+42,ptpIdentity,10));
  assert(readLe32(Usb.sent.data())==(60UL|(60UL<<21)));
  auto queueReport = [&]() {
    Usb.queued={{report.begin(),report.begin()+64},{report.begin()+64,report.begin()+128},{report.begin()+128,report.end()}};
    pollPtpTxTimestamp();pollPtpTxTimestamp();pollPtpTxTimestamp();
  };
  report.assign(131,0);report[0]=8;report[1]=1;report[130]=1;
  le32(report.data()+3,123456789);le32(report.data()+7,1234);
  queueReport();assert(masterSyncPending && Usb.sent[22]==0); // wrong sequence
  report[1]=0;report[0]=0x18;queueReport();assert(masterSyncPending); // wrong type
  report[0]=8;queueReport();assert(!masterSyncPending && ptpSamples==1);
  assert(Usb.sent[22]==8 && Usb.sent[28]==0 && Usb.sent[54]==2);
  PtpTime sentTime;assert(ptpWireTime(Usb.sent.data()+56,sentTime));
  assert(sentTime.sec==1234 && sentTime.ns==123456789);

  // Announce advertises this free-running oscillator without false UTC claims.
  assert(sendPtpMessage(11, 17));
  assert(Usb.sent.size()==88 && ptpBe16(Usb.sent.data()+24)==64);
  assert(Usb.sent[22]==11 && Usb.sent[28]==0 && Usb.sent[29]==0);
  assert(Usb.sent[54]==5 && Usb.sent[55]==1);
  assert(Usb.sent[69]==128 && Usb.sent[70]==248 && Usb.sent[71]==0xfe);
  assert(Usb.sent[72]==0xff && Usb.sent[73]==0xff && Usb.sent[74]==128);
  assert(!memcmp(Usb.sent.data()+75,ptpIdentity,8));
  assert(Usb.sent[83]==0 && Usb.sent[84]==0 && Usb.sent[85]==0xa0);

  // Delay_Resp copies requester and sequence, carrying the hardware RX time.
  header(frame,1,4321,44);masterResponses=0;masterFollowerBound=false;
  handlePtpFrame(frame,60,nullptr);assert(!masterFollowerBound && masterResponses==0);
  handlePtpFrame(frame,60,stamp);assert(masterFollowerBound && masterResponses==1);
  assert(Usb.sent.size()==80 && Usb.sent[22]==9 && ptpBe16(Usb.sent.data()+24)==54);
  assert(readLe32(Usb.sent.data())==(68UL|(68UL<<21)));
  assert(ptpBe16(Usb.sent.data()+52)==4321 && Usb.sent[54]==3);
  assert(!memcmp(Usb.sent.data()+66,frame+34,10));
  assert(ptpWireTime(Usb.sent.data()+56,sentTime));
  assert(sentTime.sec==1700000000 && sentTime.ns==1200);
  frame[34]^=1;handlePtpFrame(frame,60,stamp);assert(masterResponses==1);frame[34]^=1;
  frame[29]=1;handlePtpFrame(frame,60,stamp);assert(masterResponses==1);frame[29]=0;
  // Timeout never fabricates a Follow_Up; retry has a new sequence.
  nowMs+=1000;pollPtpMasterSchedule();assert(masterSyncPending);
  nowMs+=2501;pollPtpMasterSchedule();assert(ptpTimeouts==1 && Usb.sent[22]==0 && syncSequence==2);
  // A failed frame transfer stops the master rather than claiming an exchange.
  masterSyncPending=false;nowMs+=1000;Usb.outError=1;
  pollPtpMasterSchedule();assert(!ptpRunning && !masterSyncPending);Usb.outError=0;
  ptpMasterRole=false;

  // Exercise malformed aggregate bounds under ASan/UBSan.
  uint32_t random=17;
  for(unsigned trial=0;trial<20000;++trial) {
    for(auto &byte:rxAggregate) {random^=random<<13;random^=random>>17;random^=random<<5;byte=random;}
    rxUsed=trial%257;parsePtpAggregate();
  }
}
