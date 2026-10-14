#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "../src/detail/PtpServo.h"

// Host double-precision reference using full 2x2 matrix operations.
struct Reference {
  double x[2] = {1234, 12000};
  double p[2][2] = {{100, 0}, {0, 1000000}};
  void update(double z, double dt, int applied) {
    double f[2][2] = {{1, dt}, {0, 1}};
    double q[2][2] = {{4*dt*dt*dt/3+dt, 2*dt*dt}, {2*dt*dt, 4*dt}};
    double prior[2][2] = {};
    for (int i=0;i<2;++i) for (int j=0;j<2;++j) {
      prior[i][j]=q[i][j];
      for (int k=0;k<2;++k) for (int l=0;l<2;++l) prior[i][j]+=f[i][k]*p[k][l]*f[j][l];
    }
    x[0]+=(x[1]+applied)*dt;
    const double innovation=z-x[0];
    double gain[2]={prior[0][0]/(prior[0][0]+100),prior[1][0]/(prior[0][0]+100)};
    double a[2][2]={{1-gain[0],0},{-gain[1],1}};
    for (int i=0;i<2;++i) {
      x[i]+=gain[i]*innovation;
      for (int j=0;j<2;++j) {
        p[i][j]=gain[i]*gain[j]*100;
        for (int k=0;k<2;++k) for (int l=0;l<2;++l) p[i][j]+=a[i][k]*prior[k][l]*a[j][l];
      }
    }
  }
};

void healthy(const KalmanServo &s) {
  assert(std::isfinite(s.estimatedPhase()) && std::isfinite(s.estimatedFrequency()));
  const double a=s.phaseVariance(), b=s.crossCovariance(), c=s.frequencyVariance();
  assert(std::isfinite(a) && std::isfinite(b) && std::isfinite(c));
  assert(a>0 && c>0 && a*c-b*b >= -0.01);
}

int main() {
  KalmanServo s;
  Reference ref;
  uint32_t ms=0xfffff000U;
  assert(s.update(1234,ms,-12000)==-12000);
  int64_t phaseMilli=1234000;
  int applied=-12000;
  for (int i=0;i<500;++i) {
    const uint32_t dt=i%2 ? 995 : 1005;
    phaseMilli+=int64_t(12000+applied)*dt; ms+=dt;
    const int64_t z=phaseMilli/1000 + (i%5)*5-10;
    s.update(z,ms,applied); ref.update(double(z),double(dt)/1000,applied);
    healthy(s);
    assert(std::abs(s.estimatedPhase()-ref.x[0]) < 0.1 + 4e-7*std::abs(ref.x[0]));
    assert(std::abs(s.estimatedFrequency()-ref.x[1]) < 0.5);
    if (i>5) {
      assert(std::abs(s.phaseVariance()-ref.p[0][0]) < 0.1);
      assert(std::abs(s.frequencyVariance()-ref.p[1][1]) < 0.1);
    }
    applied=i%3==0 ? -30000 : 20000;
  }

  for (int scenario=0;scenario<3;++scenario) {
    KalmanServo filter; PiServo pi;
    int64_t phaseK=20000000, phaseP=phaseK;
    int32_t ck=0, cp=0; uint32_t rng=12345;
    double sk=0,sp=0;
    for (uint32_t i=0;i<1800;++i) {
      const int drift=11000+(scenario==1 && i>200 ? (i-200)/2 : 0);
      if (i) { phaseK+=drift+ck; phaseP+=drift+cp; }
      rng=rng*1664525U+1013904223U;
      int noise=int(rng%201)-100; if (scenario!=2) noise/=10;
      ck=filter.update(phaseK+noise,i*1000,ck);
      cp=pi.update(phaseP+noise,i*1000,cp);
      healthy(filter); assert(ck>=-500000 && ck<=500000);
      if (i>200) { assert(std::abs(phaseK)<1000); sk+=double(phaseK)*phaseK; sp+=double(phaseP)*phaseP; }
    }
    std::cout << "model " << scenario << " true-phase RMS: PI " << std::sqrt(sp/1599)
              << " ns, KALMAN " << std::sqrt(sk/1599) << " ns\n";
  }
  // Opposite clock drift, nonuniform intervals and a phase discontinuity.
  for (int drift : {-15000, 11000}) {
    s.reset(); int64_t phase=-20000000000LL; int32_t applied=0; ms=0xffff0000U;
    for (int i=0;i<1500;++i) {
      const uint32_t dt=i%2 ? 995 : 1005;
      if (i) { phase+=int64_t(drift+applied)*dt; ms+=dt; }
      if (i==600) phase+=2000000000LL;
      applied=s.update(phase/1000,ms,applied); healthy(s);
      assert(applied>=-500000 && applied<=500000);
      if ((i>300 && i<600) || i>900) assert(std::abs(phase)<20000);
    }
    assert(s.update(5,ms,applied)==applied);
    assert(s.update(5,ms+2000,applied)==applied);
    s.clearHistory(); assert(s.update(5,ms+3000,applied)==applied);
  }
  s.reset();
  for (uint32_t i=0;i<5000;++i) {
    int32_t out=s.update(i%2 ? 500000000LL : -500000000LL,i*1500,i%3 ? 500000 : -500000);
    healthy(s); assert(out>=-500000 && out<=500000);
  }
  assert(s.update(INT64_MAX,0,0)==-500000);
  assert(s.update(INT64_MIN,0,0)==500000);
  std::cout << "Double-reference agreement, covariance, correction input, wrap, gaps and recovery passed\n";
}
