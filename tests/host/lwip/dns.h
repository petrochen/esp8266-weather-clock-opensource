#pragma once
#include <stdint.h>
struct ip_addr_t { uint32_t address = 1; };
using err_t = int;
const int ERR_OK = 0, ERR_INPROGRESS = -5;
using DNSCallback = void(*)(const char*, const ip_addr_t*, void*);
inline DNSCallback dnsCallback;
inline void* dnsArgument;
inline int dnsResult = ERR_INPROGRESS;
inline err_t dns_gethostbyname(const char*, ip_addr_t* address, DNSCallback callback, void* argument) {
  dnsCallback = callback; dnsArgument = argument; address->address = 1; return dnsResult;
}
