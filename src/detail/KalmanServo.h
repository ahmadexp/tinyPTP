#pragma once
#include <stdint.h>

// Two-state clock model: phase ns, intrinsic frequency error ppb (ns/s).
// Single precision, using the R4's FPU when enabled by its core toolchain.
// No absolute epoch is converted to floating point.
class KalmanServo {
  float phase = 0, frequency = 0;
  float p00 = 100, p01 = 0, p11 = 1000000;
  uint32_t previousMs = 0;
  bool initialized = false;
  void initialize(int64_t offset, uint32_t ms, int32_t applied) {
    phase = float(offset); frequency = -float(applied);
    p00 = 100; p01 = 0; p11 = 1000000;
    previousMs = ms; initialized = true;
  }
public:
  void clearHistory() { initialized = false; }
  void reset() { clearHistory(); }
  float estimatedPhase() const { return phase; }
  float estimatedFrequency() const { return frequency; }
  float phaseVariance() const { return p00; }
  float crossCovariance() const { return p01; }
  float frequencyVariance() const { return p11; }
  int32_t update(int64_t offset, uint32_t masterMs, int32_t appliedPpb) {
    if (offset > 500000000LL || offset < -500000000LL) {
      clearHistory(); return offset > 0 ? -500000 : 500000;
    }
    if (appliedPpb > 500000) appliedPpb = 500000;
    if (appliedPpb < -500000) appliedPpb = -500000;
    const uint32_t elapsed = masterMs - previousMs;
    if (!initialized || elapsed < 500 || elapsed > 1500) {
      initialize(offset, masterMs, appliedPpb);
      return appliedPpb; // No valid interval yet; preserve the current command.
    }
    previousMs = masterMs;
    const float dt = float(elapsed) * 0.001f;
    // R = 100 ns^2; frequency random-walk density = 4 ppb^2/s;
    // independent phase random-walk density = 1 ns^2/s.
    const float r = 100.0f, qf = 4.0f;
    phase += (frequency + float(appliedPpb)) * dt;
    const float a = p00 + 2.0f * dt * p01 + dt * dt * p11 +
                    qf * dt * dt * dt / 3.0f + dt;
    const float b = p01 + dt * p11 + qf * dt * dt / 2.0f;
    const float c = p11 + qf * dt;
    const float s = a + r;
    // Bounds also reject NaN/Inf without needing a floating-point I/O library.
    if (!(s > 0.0f && s < 1000000000.0f && c > 0.0f && c < 1000000000.0f)) {
      initialize(offset, masterMs, appliedPpb); return appliedPpb;
    }
    const float k0 = a / s, k1 = b / s;
    const float innovation = float(offset) - phase;
    phase += k0 * innovation;
    frequency += k1 * innovation;
    if (frequency > 500000.0f) frequency = 500000.0f;
    if (frequency < -500000.0f) frequency = -500000.0f;
    // Joseph covariance update, exploiting H = [1, 0] and symmetry.
    const float residualGain = 1.0f - k0;
    p00 = residualGain * residualGain * a + k0 * k0 * r;
    p01 = residualGain * (b - k1 * a) + k0 * k1 * r;
    p11 = c - 2.0f * k1 * b + k1 * k1 * a + k1 * k1 * r;
    if (p11 < 0.001f) p11 = 0.001f; // Roundoff floor, not a noise estimator.
    // Cancel estimated intrinsic drift and remove phase over four seconds.
    const float command = -frequency - phase * 0.25f;
    if (command > 500000.0f) return 500000;
    if (command < -500000.0f) return -500000;
    return int32_t(command);
  }
};
