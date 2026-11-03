#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#define PROGMEM
#define ICACHE_FLASH_ATTR
#define F(x) x
#define PSTR(x) (x)
#define snprintf_P snprintf
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif
using byte = uint8_t;
unsigned long millis();
class String : public std::string {
public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
};
inline String operator+(const String& a, int n) { return String(std::string(a) + std::to_string(n)); }
