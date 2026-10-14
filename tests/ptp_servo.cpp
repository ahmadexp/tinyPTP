#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "../src/detail/PtpServo.h"

int main() {
  PiServo pi;
  assert(pi.update(1000, 0, 0) == -550);
  assert(pi.update(1000, 1000, -550) == -600);
  assert(pi.update(500000000, 2000, -600) == -500000);
  assert(pi.update(0, 3000, -500000) == -100); // Saturation did not wind up.
  pi.reset(); assert(pi.update(-1000, 0, 0) == 550);

  // Known free-running drift, deliberately changing external corrections,
  // nonuniform intervals, and wrap of the 32-bit millisecond timestamp.
  LinregServo reg;
  uint32_t ms = 0xfffff000U;
  int64_t phaseMilliNs = 1200000;
  int32_t applied = 0;
  for (int i = 0; i < 200; ++i) {
    if (i) {
      const uint32_t dt = i % 2 ? 995 : 1005;
      phaseMilliNs += int64_t(12000 + applied) * dt;
      ms += dt;
    }
    const int64_t phase = phaseMilliNs / 1000;
    const int32_t output = reg.update(phase, ms, applied);
    if (i >= 8) assert(std::abs(output - servoLimit(-12000 - phase / 4)) <= 3);
    applied = i % 3 == 0 ? -30000 : 20000;
  }

  // Closed-loop physical model: a constant frequency error plus the command
  // actually applied during the previous interval, with deterministic noise.
  for (int32_t drift : {-15000, 11000}) {
    reg.reset(); applied = 0;
    int64_t phase = 20000000;
    for (uint32_t i = 0; i < 400; ++i) {
      if (i) phase += drift + applied;
      const int noise = int(i % 5) * 5 - 10;
      applied = reg.update(phase + noise, i * 1000, applied);
      assert(applied >= -500000 && applied <= 500000);
      if (i > 150) {
        assert(std::abs(phase) < 30);
        assert(std::abs(applied + drift) < 30);
      }
    }
    // A long outage or explicit history invalidation must not fit stale points.
    assert(reg.update(0, 500000, applied) == applied);
    reg.clearHistory(); assert(reg.update(0, 501000, applied) == applied);
    reg.reset(); assert(reg.update(0, 0, 0) == 0);
  }
  // Exercise arithmetic limits under sanitizers.
  for (uint32_t i = 0; i < 2000; ++i) {
    const int32_t out = reg.update(i % 2 ? 500000000LL : -500000000LL,
                                  i * 2500, i % 3 ? 500000 : -500000);
    assert(out >= -500000 && out <= 500000);
  }
  assert(reg.update(INT64_MAX, 0, 0) == -500000);
  assert(reg.update(INT64_MIN, 0, 0) == 500000);
  std::cout << "PI limits, compensated regression, wrap, gap recovery and closed-loop acquisition passed\n";
}
