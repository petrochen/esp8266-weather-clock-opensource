#pragma once
#include <Arduino.h>
#include <stdarg.h>
class Print {
public:
 virtual ~Print() = default;
 virtual size_t write(uint8_t value)=0;
 size_t write(const char* s){size_t n=0;while(*s){n+=write(uint8_t(*s++));}return n;}
 size_t print(const char* s){return write(s);}
 size_t print(const String& s){return write(s.c_str());}
 size_t print(char value){return write(uint8_t(value));}
 size_t print(int n){return write(std::to_string(n).c_str());}
 size_t println(const char* s){return print(s)+write(uint8_t('\n'));}
 size_t printf(const char* format,...){char s[512];va_list args;va_start(args,format);vsnprintf(s,sizeof(s),format,args);va_end(args);return write(s);}
};
