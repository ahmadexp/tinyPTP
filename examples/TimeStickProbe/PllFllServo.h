#pragma once
#include <stdint.h>

// 1 Hz experimental hybrid: proportional phase loop plus an eight-interval
// frequency loop. Frequency is milli-ppb; integrated correction is ns * 1000.
// No absolute epoch, floating point, heap, or sample ring buffer is needed.
class PllFllServo {
  int64_t correctionMilliNs = 0;
  int32_t anchorOffset = 0, frequencyMilliPpb = 0;
  uint32_t previousMs = 0;
  uint16_t windowMs = 0;
  uint8_t intervals = 0;
  bool haveAnchor = false, haveFrequency = false;
  static int32_t limit(int64_t value, int32_t bound) {
    return value > bound ? bound : value < -int64_t(bound) ? -bound : int32_t(value);
  }
public:
  void clearHistory() {
    haveAnchor = haveFrequency = false;
    correctionMilliNs = 0; windowMs = 0; intervals = 0;
  }
  void reset() { clearHistory(); frequencyMilliPpb = 0; }
  int32_t estimatedFrequencyMilliPpb() const { return frequencyMilliPpb; }
  int32_t update(int64_t offset, uint32_t masterMs, int32_t appliedPpb) {
    if (offset > 500000000LL || offset < -500000000LL) {
      reset(); return offset > 0 ? -500000 : 500000;
    }
    appliedPpb = limit(appliedPpb, 500000);
    const uint32_t dt = masterMs - previousMs;
    previousMs = masterMs;
    if (!haveAnchor || dt < 500 || dt > 1500) {
      clearHistory(); haveAnchor = true;
      anchorOffset = int32_t(offset);
      frequencyMilliPpb = -appliedPpb * 1000L;
      return appliedPpb;
    }
    correctionMilliNs += int64_t(appliedPpb) * dt;
    windowMs += uint16_t(dt);
    if (++intervals == 8) {
      // Remove the integral of the commands actually applied during this
      // window. Regressing controlled phase alone would erase the drift.
      const int64_t freePhaseMilliNs = (offset - anchorOffset) * 1000 - correctionMilliNs;
      const int32_t measured = limit(freePhaseMilliNs * 1000 / windowMs, 500000000L);
      if (!haveFrequency) { frequencyMilliPpb = measured; haveFrequency = true; }
      else frequencyMilliPpb += (int64_t(measured) - frequencyMilliPpb) / 2;
      anchorOffset = int32_t(offset);
      correctionMilliNs = 0; windowMs = 0; intervals = 0;
    }
    // Cancel estimated intrinsic frequency and remove current phase in 4 s.
    // Keep fractional frequency internally before quantizing the hardware command.
    return limit((-int64_t(frequencyMilliPpb) - offset * 250) / 1000, 500000);
  }
};
