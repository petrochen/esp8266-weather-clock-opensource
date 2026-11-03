#pragma once
#include <ArduinoJson.h>
#include <stdlib.h>

// Bound the parser independently of the HTTP body size. Host pointer slots are
// twice as large as ESP8266 slots, so tests use a proportionally sized budget.
class BoundedJsonAllocator : public ArduinoJson::Allocator {
  size_t used = 0;
  static constexpr size_t limit = 1024 * sizeof(void*);
public:
  void* ICACHE_FLASH_ATTR allocate(size_t size) override { return reallocate(nullptr, size); }
  void ICACHE_FLASH_ATTR deallocate(void* ptr) override {
    if (!ptr) return;
    size_t* header = static_cast<size_t*>(ptr) - 1;
    used -= *header;
    free(header);
  }
  void* ICACHE_FLASH_ATTR reallocate(void* ptr, size_t size) override {
    size_t old = ptr ? *(static_cast<size_t*>(ptr) - 1) : 0;
    if (size > limit || used - old > limit - size) return nullptr;
    size_t* header = static_cast<size_t*>(realloc(ptr ? static_cast<size_t*>(ptr) - 1 : nullptr,
                                               size + sizeof(size_t)));
    if (!header) return nullptr;
    used = used - old + size;
    *header = size;
    return header + 1;
  }
};
