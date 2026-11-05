#pragma once
#include <stddef.h>
#include <stdint.h>

// Caller serializes access. Fixed storage: never allocates in the NFC task.
template <size_t Capacity> class DebugLogBuffer {
 public:
  void append(const char* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      bytes_[end_ % Capacity] = text[i];
      ++end_;
      if (count_ < Capacity) ++count_;
    }
  }
  size_t read(uint32_t& cursor, bool initial, char* out, size_t limit, bool& lost) const {
    uint32_t available = end_ - cursor;
    lost = !initial && available > count_;
    if (initial || lost) cursor = end_ - count_;
    available = end_ - cursor;
    size_t length = available < limit ? available : limit;
    for (size_t i = 0; i < length; ++i) out[i] = bytes_[(cursor + i) % Capacity];
    cursor += length;
    return length;
  }
 private:
  // Capacity must divide 2^32 so indexing is continuous across cursor wrap.
  static_assert(Capacity && (Capacity & (Capacity - 1)) == 0, "power-of-two capacity required");
  char bytes_[Capacity] = {};
  uint32_t end_ = 0;
  uint32_t count_ = 0;
};
