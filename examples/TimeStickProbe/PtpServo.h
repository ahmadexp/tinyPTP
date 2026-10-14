#pragma once
#include <stdint.h>
#include "BuildConfig.h"
#include "KalmanServo.h"
#include "PllFllServo.h"

inline int32_t servoLimit(int64_t value) {
  return value > 500000 ? 500000 : value < -500000 ? -500000 : int32_t(value);
}

class PiServo {
  int32_t integral = 0;
public:
  void reset() { integral = 0; }
  void clearHistory() {} // Preserve the existing PI behavior across gaps.
  int32_t update(int64_t offset, uint32_t, int32_t) {
    int64_t proposed = int64_t(integral) - offset / 20;
    if (proposed > 200000) proposed = 200000;
    if (proposed < -200000) proposed = -200000;
    const int64_t control = proposed - offset / 2;
    if (control <= 500000 && control >= -500000) integral = proposed;
    return servoLimit(control);
  }
};

// Bounded gain scheduling at 1 Hz. Gain units are 1/1000; integral state
// retains fractional ppb so small residuals do not suffer PI's integer deadband.
class AutoPiServo {
  int32_t integralMilli = 0, sum = 0, absoluteSum = 0, previousOffset = 0;
  uint32_t previousMs = 0;
  uint8_t level = 2, count = 0, reversals = 0, quietWindows = 0;
  bool haveTime = false;
  void clearWindow() { count = reversals = 0; sum = absoluteSum = 0; }
  static int32_t boundIntegral(int64_t value) {
    return value > 200000000 ? 200000000 : value < -200000000 ? -200000000 : int32_t(value);
  }
public:
  uint8_t mode() const { return level; }
  uint16_t kpMilli() const { return level == 0 ? 250 : level == 1 ? 500 : 750; }
  uint16_t kiMilli() const { return level == 0 ? 16 : level == 1 ? 50 : 125; }
  void clearHistory() { clearWindow(); quietWindows = 0; haveTime = false; }
  void reset() { integralMilli = 0; level = 2; clearHistory(); }
  int32_t update(int64_t offset, uint32_t masterMs, int32_t appliedPpb) {
    if (offset > 500000000LL || offset < -500000000LL) {
      reset();
      return offset > 0 ? -500000 : 500000;
    }
    const uint32_t dt = masterMs - previousMs;
    previousMs = masterMs;
    if (!haveTime || dt < 500 || dt > 1500) {
      clearHistory(); haveTime = true;
      // This gain bank targets 1 Hz, not arbitrary Sync rates. Hold the last
      // command outside the supported 0.5..1.5-second integration interval,
      // and reanchor the integral as far as its bound permits.
      integralMilli = boundIntegral(int64_t(servoLimit(appliedPpb)) * 1000 + offset * kpMilli());
      return servoLimit(appliedPpb);
    }
    const uint16_t oldKp = kpMilli();
    const int32_t error = int32_t(offset);
    const int32_t magnitude = error < 0 ? -error : error;
    if (magnitude > 1000) {
      level = 2; clearWindow(); quietWindows = 0;
    } else {
      if (count && ((error > 0 && previousOffset < 0) || (error < 0 && previousOffset > 0))) ++reversals;
      previousOffset = error; sum += error; absoluteSum += magnitude;
      if (++count == 16) {
        const int32_t bias = sum < 0 ? -sum : sum;
        if (absoluteSum > 160 && bias * 2 > absoluteSum) {
          if (level < 2) ++level;
          quietWindows = 0;
        } else if (absoluteSum <= 160 || (reversals >= 9 && bias * 4 < absoluteSum)) {
          // Two qualifying windows before reducing bandwidth; one before
          // increasing it. No tuning on individual offset spikes.
          if (++quietWindows >= 2) {
            if (level) --level;
            quietWindows = 0;
          }
        } else quietWindows = 0;
        clearWindow();
      }
    }
    // Compensate the proportional gain change to avoid a control jump,
    // subject to the same bounded integral. The next integration is normal.
    integralMilli = boundIntegral(int64_t(integralMilli) + offset * (int32_t(kpMilli()) - oldKp));
    const int32_t proposed = boundIntegral(int64_t(integralMilli) - offset * kiMilli() * dt / 1000);
    const int64_t controlMilli = int64_t(proposed) - offset * kpMilli();
    if (controlMilli <= 500000000LL && controlMilli >= -500000000LL) integralMilli = proposed;
    return servoLimit(controlMilli / 1000);
  }
};

// Ordinary least squares on offset minus accumulated frequency correction.
// x: master timestamp in milliseconds; y: reconstructed free-running phase ns.
// Recenter x/y at the oldest point for bounded 64-bit integer products.
// This is a fixed-window experiment, not LinuxPTP's adaptive linreg servo.
class LinregServo {
  struct Point { uint32_t ms; int64_t phase; } points[8];
  int64_t correctionNs = 0;
  int32_t correctionRemainder = 0;
  uint32_t previousMs = 0;
  uint8_t count = 0, next = 0;
public:
  void clearHistory() {
    count = next = 0;
    correctionNs = 0; correctionRemainder = 0;
  }
  void reset() { clearHistory(); }
  int32_t update(int64_t offset, uint32_t masterMs, int32_t appliedPpb) {
    // The caller steps offsets beyond this bound. Also guard standalone use.
    if (offset > 500000000LL || offset < -500000000LL) {
      clearHistory();
      return offset > 0 ? -500000 : 500000;
    }
    if (count) {
      const uint32_t dt = masterMs - previousMs; // Handles uint32_t wrap.
      if (!dt || dt > 2500) clearHistory();
      else {
        const int64_t increment = int64_t(servoLimit(appliedPpb)) * dt + correctionRemainder;
        correctionNs += increment / 1000;
        correctionRemainder = increment % 1000;
      }
    }
    previousMs = masterMs;
    points[next].ms = masterMs;
    points[next].phase = offset - correctionNs;
    next = (next + 1) & 7;
    if (count < 8) ++count;
    if (count < 4) return servoLimit(appliedPpb);

    const Point &origin = points[count == 8 ? next : 0];
    int64_t sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const int64_t x = uint32_t(points[i].ms - origin.ms);
      const int64_t y = points[i].phase - origin.phase;
      sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const int64_t denominator = count * sxx - sx * sx;
    if (!denominator) return servoLimit(appliedPpb);
    // ns/ms * 1000 = ns/s = ppb. At most 8 points, <=2.5 s gaps, and
    // bounded offsets keep these products within signed 64-bit range.
    const int64_t driftPpb = (count * sxy - sx * sy) * 1000 / denominator;
    const int64_t xNow = uint32_t(masterMs - origin.ms);
    const int64_t phaseNow = origin.phase +
        (sy * 1000 + driftPpb * (count * xNow - sx)) / (count * 1000L) + correctionNs;
    // Cancel estimated free-running frequency and remove phase over four seconds.
    return servoLimit(-driftPpb - phaseNow / 4);
  }
};

#if TINYPTP_SERVO == 4
typedef PllFllServo PtpServo;
#elif TINYPTP_SERVO == 3
typedef KalmanServo PtpServo;
#elif TINYPTP_SERVO == 2
typedef AutoPiServo PtpServo;
#elif TINYPTP_SERVO == 1
typedef LinregServo PtpServo;
#else
typedef PiServo PtpServo;
#endif
