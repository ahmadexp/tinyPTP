#pragma once
#include <stdint.h>
#include <string.h>

// The vendor EP4 receive buffer is at most 1024 bytes, with 32 timestamp
// slots. Firmware 1.2.9.0 also returns single 16-byte records. Stream USB
// packets instead of buffering 1 KB on an UNO.
// Expose a candidate only after the complete transfer passes framing checks.
struct Ax88279aReport {
  uint16_t used = 0;
  uint8_t record[16] = {};
  uint8_t candidate[16] = {};
  uint8_t matches = 0;
  bool malformed = false;
  bool complete = false;
  void reset() { used = 0; matches = 0; malformed = complete = false; }
  bool feed(const uint8_t *data, uint16_t count, uint8_t type, uint16_t sequence) {
    if (complete || count > 64 || used > 1024 - count) malformed = true;
    if (!malformed) {
      for (uint16_t i = 0; i < count; ++i, ++used) {
        if (used >= 512) continue;
        record[used & 15] = data[i];
        if ((used & 15) != 15) continue;
        const uint16_t seq = uint16_t(record[10]) | (uint16_t(record[11]) << 8);
        // EP4 supplies transmit records. Match the vendor's status bit;
        // tested firmware leaves tx_ts (bit 5) clear. Reject user-tagged data.
        if ((record[12] & 0x50) == 0x40 && (record[12] & 15) == type && seq == sequence) {
          if (!matches) memcpy(candidate, record, sizeof(candidate));
          if (matches < 2) ++matches;
        }
      }
    }
    complete = count < 64 || used == 1024 || malformed;
    return complete;
  }
  bool valid() const { return complete && !malformed && used && !(used & 15) && matches == 1; }
};
