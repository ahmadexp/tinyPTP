#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "../src/detail/PtpServo.h"

int main() {
  // Independent physical-clock oracle: reconstruct a constant intrinsic drift
  // while an external actor changes the applied command every sample.
  for (int drift : {-15000, 12000}) {
    PllFllServo s;
    uint32_t ms = 0xfffff000U;
    int64_t phaseMilliNs = 1234000;
    int32_t applied = 0;
    assert(s.update(phaseMilliNs/1000,ms,applied)==0);
    for (int i=1;i<=400;++i) {
      const uint32_t dt = i%2 ? 995 : 1005;
      phaseMilliNs += int64_t(drift+applied)*dt; ms+=dt;
      s.update(phaseMilliNs/1000,ms,applied);
      if (i>=8) assert(std::abs(s.estimatedFrequencyMilliPpb()-drift*1000)<=125);
      applied = i%3 ? 20000 : -30000;
    }
  }
  // Closed loop with both signs of drift, large acquisition errors, variable
  // sample intervals, wrap, bounded timestamp noise and a later phase jump.
  for (int drift : {-15000,11000}) {
    PllFllServo s; uint32_t ms=0xffff0000U;
    int64_t phaseMilliNs=-20000000000LL; int32_t applied=0;
    for (int i=0;i<1500;++i) {
      const uint32_t dt=i%2 ? 995 : 1005;
      if(i){phaseMilliNs+=int64_t(drift+applied)*dt;ms+=dt;}
      if(i==600) phaseMilliNs+=2000000000LL;
      const int noise=(i%5)*5-10;
      applied=s.update(phaseMilliNs/1000+noise,ms,applied);
      assert(applied>=-500000 && applied<=500000);
      if((i>300 && i<600)||i>950) assert(std::abs(phaseMilliNs)<30000);
    }
    // An outage, duplicate timestamp, or explicit invalidation holds the
    // current hardware command and discards the contaminated frequency window.
    assert(s.update(0,ms+2000,applied)==applied);
    assert(s.update(0,ms+2000,applied)==applied);
    s.clearHistory(); assert(s.update(0,ms+3000,applied)==applied);
    s.reset(); assert(s.update(0,0,0)==0);
  }
  // Hold the last correction through a 30-second measurement outage, then
  // resume with a changed oscillator drift. The gap must not poison the fit.
  {
    PllFllServo f; int64_t phase=0; int32_t command=0; uint32_t ms=0;
    for (int i=0;i<900;++i) {
      const int drift=i<400 ? 11000 : 12000;
      const uint32_t dt=i==400 ? 30000 : 1000;
      if(i){phase+=int64_t(drift+command)*dt;ms+=dt;}
      const int32_t prior=command;
      command=f.update(phase/1000,ms,command);
      if(i==400)assert(command==prior);
      if(i>700)assert(std::abs(phase)<30000);
    }
  }
  // Arithmetic stress includes boundaries, out-of-range caller input,
  // sub-second intervals and the maximum allowed interval/window length.
  PllFllServo s;
  for(uint32_t i=0;i<4000;++i){
    int32_t c=s.update(i%2 ? 500000000LL : -500000000LL,i*1500,i%3 ? 500000 : -500000);
    assert(c>=-500000 && c<=500000);
  }
  assert(s.update(INT64_MAX,0,0)==-500000);
  assert(s.update(INT64_MIN,0,0)==500000);
  s.reset(); assert(s.update(0,0,INT32_MAX)==500000);
  assert(s.update(0,499,INT32_MIN)==-500000);
  assert(s.update(0,2000,12345)==12345);
  // Synthetic true-phase scores are descriptive, not hardware accuracy claims.
  for(int scenario=0;scenario<3;++scenario){
    PllFllServo f; PiServo pi; int64_t pf=20000000,pp=pf;
    int32_t cf=0,cp=0; uint32_t rng=12345; double sf=0,sp=0;
    for(uint32_t i=0;i<1800;++i){
      int drift=11000+(scenario==1 && i>200 ? (i-200)/2 : 0);
      if(i){pf+=drift+cf;pp+=drift+cp;}
      rng=rng*1664525U+1013904223U;
      int noise=int(rng%201)-100;if(scenario!=2)noise/=10;
      cf=f.update(pf+noise,i*1000,cf);cp=pi.update(pp+noise,i*1000,cp);
      if(i>200){assert(std::abs(pf)<1000);sf+=double(pf)*pf;sp+=double(pp)*pp;}
    }
    std::cout<<"model "<<scenario<<" true-phase RMS: PI "<<std::sqrt(sp/1599)
             <<" ns, PLL_FLL "<<std::sqrt(sf/1599)<<" ns\n";
  }
  std::cout<<"PLL/FLL compensation, wrap, intervals, acquisition, phase-step and limits passed\n";
}
