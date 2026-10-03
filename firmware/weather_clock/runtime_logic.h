#ifndef RUNTIME_LOGIC_H
#define RUNTIME_LOGIC_H

#include <stdint.h>

// Deadlines remain valid across the 32-bit millis() rollover. A separate flag
// represents an inactive timer, so a deadline equal to zero is also usable.
struct RetryConfig {
  uint8_t maxRetries = 3;
  uint8_t currentRetry = 0;
  uint32_t nextRetryTime = 0;
  uint32_t maxBackoffMs = 8000;
  bool pending = false;

  bool scheduleRetry(uint32_t now) {
    if (currentRetry >= maxRetries) {
      pending = false;
      return false;
    }
    uint32_t delay = 1000UL << currentRetry++;
    nextRetryTime = now + (delay > maxBackoffMs ? maxBackoffMs : delay);
    pending = true;
    return true;
  }

  bool isRetryTime(uint32_t now) const {
    return pending && uint32_t(now - nextRetryTime) < 0x80000000UL;
  }

  void consume() { pending = false; }
  void reset() { currentRetry = 0; pending = false; nextRetryTime = 0; }
};

// A bijection over 8192 pixels gives nested, repeatable dissolve masks without
// random retries or duplicate pixels. All pixels disappear at 100%.
inline uint16_t dissolveRank(uint16_t pixel) {
  return uint16_t((uint32_t(pixel) * 4051U + 997U) & 8191U);
}

#endif
