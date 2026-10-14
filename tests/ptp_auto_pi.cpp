#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "../src/detail/PtpServo.h"

int main() {
  // Closed-loop comparison under quiet drift, a frequency ramp, and noise.
  // Score true phase, not the noisy observation used by the controller.
  for (int scenario = 0; scenario < 3; ++scenario) {
    AutoPiServo adaptive;
    PiServo fixed;
    int64_t phaseA = 20000000, phaseP = phaseA;
    int32_t controlA = 0, controlP = 0;
    double squaresA = 0, squaresP = 0;
    uint32_t rng = 12345;
    unsigned modes[3] = {};
    for (uint32_t i = 0; i < 1800; ++i) {
      const int drift = 11000 + (scenario == 1 && i > 200 ? (i - 200) / 2 : 0);
      if (i) { phaseA += drift + controlA; phaseP += drift + controlP; }
      rng = rng * 1664525U + 1013904223U;
      int noise = int(rng % 201) - 100;
      if (scenario != 2) noise /= 10;
      controlA = adaptive.update(phaseA + noise, 0xffff0000U + i * 1000, controlA);
      controlP = fixed.update(phaseP + noise, i * 1000, controlP);
      assert(controlA >= -500000 && controlA <= 500000);
      assert(adaptive.mode() <= 2);
      if (i > 200) {
        squaresA += double(phaseA) * phaseA; squaresP += double(phaseP) * phaseP;
        ++modes[adaptive.mode()];
      }
    }
    assert(squaresA < squaresP);
    assert(modes[0] > 0);
    if (scenario == 1) assert(modes[1] > 0 && modes[2] > 0);
    std::cout << "model " << scenario << " true-phase RMS: PI " << std::sqrt(squaresP / 1599)
              << " ns, AUTO_PI " << std::sqrt(squaresA / 1599) << " ns\n";
  }

  AutoPiServo servo;
  // Two quiet windows per reduction; no one-sample downshift.
  for (uint32_t i = 0; i <= 32; ++i) servo.update(0, i * 1000, 0);
  assert(servo.mode() == 1);
  for (uint32_t i = 33; i <= 64; ++i) servo.update(0, i * 1000, 0);
  assert(servo.mode() == 0);
  // Persistent signed bias must increase bandwidth.
  int32_t beforeGainChange = 0, afterGainChange = 0;
  for (uint32_t i = 65; i <= 80; ++i) {
    const int32_t command = servo.update(100, i * 1000, 0);
    if (i == 79) beforeGainChange = command;
    if (i == 80) afterGainChange = command;
  }
  assert(servo.mode() == 1);
  // Only the new integral increment appears, not a proportional-gain jump.
  assert(afterGainChange - beforeGainChange == -5);
  servo.update(1001, 81000, 0); assert(servo.mode() == 2);
  assert(servo.update(5, 81000, -12000) == -12000); // Duplicate timestamp.
  assert(servo.update(5, 90000, -12000) == -12000); // Missing interval.
  assert(servo.update(5, 92000, -12000) == -12000); // A missed 1 Hz Sync.
  servo.clearHistory();
  assert(servo.update(5, 91000, -12000) == -12000);
  servo.reset(); assert(servo.mode() == 2);
  assert(servo.update(0, 0, 0) == 0);

  // Nonuniform update intervals, frequency polarity, saturation and recovery.
  for (int drift : {-15000, 11000}) {
    servo.reset();
    int64_t phaseMilli = -20000000000LL;
    int32_t applied = 0;
    uint32_t ms = 0xfffff000U;
    for (int i = 0; i < 1500; ++i) {
      const uint32_t dt = i % 2 ? 995 : 1005;
      if (i) { phaseMilli += int64_t(drift + applied) * dt; ms += dt; }
      if (i == 600) phaseMilli += 2000000000LL;
      applied = servo.update(phaseMilli / 1000, ms, applied);
      assert(applied >= -500000 && applied <= 500000);
      if ((i > 300 && i < 600) || i > 900) assert(std::abs(phaseMilli) < 15000);
    }
  }
  for (uint32_t i = 0; i < 2000; ++i) {
    const int32_t out = servo.update(i % 2 ? 500000000LL : -500000000LL,
                                     i * 1500, i % 3 ? 500000 : -500000);
    assert(out >= -500000 && out <= 500000);
  }
  assert(servo.update(INT64_MAX, 0, 0) == -500000);
  assert(servo.update(INT64_MIN, 0, 0) == 500000);
  std::cout << "Adaptive gains, hysteresis, gap handling, saturation and recovery passed\n";
}
