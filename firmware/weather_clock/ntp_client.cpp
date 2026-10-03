/*
 * ntp_client.cpp - NTP client and time functions
 * TJ-56-654 Weather Clock
 */

#include "globals.h"
#include <lwip/dns.h>

// DST calculation for European rules
// DST starts: last Sunday of March at 01:00 UTC
// DST ends: last Sunday of October at 01:00 UTC
bool ICACHE_FLASH_ATTR isDST(unsigned long epochTime) {
  if (!config.dst_enabled) return false;

  time_t t = epochTime;
  struct tm *timeinfo = gmtime(&t);

  int month = timeinfo->tm_mon + 1; // 1-12
  int day = timeinfo->tm_mday;      // 1-31
  int hour = timeinfo->tm_hour;

  // Not DST: November - February
  if (month < 3 || month > 10) return false;

  // Always DST: April - September
  if (month > 3 && month < 10) return true;

  // March: DST starts last Sunday at 01:00 UTC
  if (month == 3) {
    // Compute weekday of the 31st from current day's weekday (tm_wday: 0=Sun)
    int weekdayOf31 = (timeinfo->tm_wday + (31 - day)) % 7;
    int lastSunday = 31 - weekdayOf31;
    if (day < lastSunday) return false;
    if (day > lastSunday) return true;
    if (hour < 1) return false;
    return true;
  }

  // October: DST ends last Sunday at 01:00 UTC
  if (month == 10) {
    int weekdayOf31 = (timeinfo->tm_wday + (31 - day)) % 7;
    int lastSunday = 31 - weekdayOf31;
    if (day < lastSunday) return true;
    if (day > lastSunday) return false;
    if (hour < 1) return true;
    return false;
  }

  return false;
}

// Get total timezone offset including DST
long ICACHE_FLASH_ATTR getTotalOffset(unsigned long epochTime) {
  long offset = config.timezone_offset;
  if (isDST(epochTime)) {
    offset += 3600; // Add 1 hour for DST
  }
  return offset;
}

// Advance the anchor regularly, preserving milliseconds. This also keeps time
// through a millis() rollover during an extended network outage.
unsigned long ICACHE_FLASH_ATTR getAsyncEpoch() {
  if (!timeIsSynced) return 0;
  uint32_t seconds = uint32_t(millis() - syncedMillis) / 1000;
  syncedEpoch += seconds;
  syncedMillis += seconds * 1000UL;
  return syncedEpoch;
}

void ICACHE_FLASH_ATTR formatClockTime(char* out, size_t size, bool local) {
  if (!timeIsSynced) {
    snprintf(out, size, "--:--:--");
    return;
  }
  unsigned long epoch = getAsyncEpoch();
  if (local) epoch += getTotalOffset(epoch);
  snprintf(out, size, "%02u:%02u:%02u", unsigned(epoch / 3600 % 24),
           unsigned(epoch / 60 % 60), unsigned(epoch % 60));
}

static IPAddress ntpAddress;
static uint8_t requestToken[8];
static uintptr_t dnsGeneration = 0;
static bool dnsReady = false;
static bool dnsSucceeded = false;
static bool ntpStarted = false;

static void ICACHE_FLASH_ATTR ntpFailed(const char* error) {
  ntpState = NTP_IDLE;
  internetConnected = false;
  lastError = error;
  if (!ntpRetry.scheduleRetry(millis())) {
    ntpRetry.reset();
    lastNTPUpdate = millis();
  }
}

static void ICACHE_FLASH_ATTR onNTPResolved(const char*, const ip_addr_t* address, void* generation) {
  // Timed-out lookups may still complete; never apply them to a newer request.
  if (ntpState != NTP_RESOLVING || uintptr_t(generation) != dnsGeneration) return;
  dnsSucceeded = address != nullptr;
  if (address) ntpAddress = IPAddress(address);
  dnsReady = true;
}

void ICACHE_FLASH_ATTR sendNTPRequestAsync() {
  if (ntpState != NTP_IDLE || WiFi.status() != WL_CONNECTED) return;
  ntpRetry.consume();
  ntpStarted = true;
  lastNTPUpdate = millis();
  ntpRequestTime = millis();
  ++ntpAttempts;
  dnsReady = false;
  dnsSucceeded = false;
  ntpState = NTP_RESOLVING;
  ++dnsGeneration;
  ip_addr_t address;
  err_t result = dns_gethostbyname(config.ntp_server, &address, onNTPResolved,
                                 reinterpret_cast<void*>(dnsGeneration));
  if (result == ERR_OK) onNTPResolved(nullptr, &address, reinterpret_cast<void*>(dnsGeneration));
  else if (result != ERR_INPROGRESS) ntpFailed("NTP: DNS resolution failed");
}

void ICACHE_FLASH_ATTR processNTPResponse() {
  getAsyncEpoch();
  if (ntpState != NTP_IDLE &&
      (WiFi.status() != WL_CONNECTED || uint32_t(millis() - ntpRequestTime) > NTP_TIMEOUT_MS)) {
    ntpFailed("NTP: timeout or WiFi disconnected");
  }
  if (ntpState == NTP_RESOLVING && dnsReady) {
    if (!dnsSucceeded) {
      ntpFailed("NTP: DNS resolution failed");
    } else {
      // Discard delayed datagrams from previous attempts.
      while (ntpUDP.parsePacket()) while (ntpUDP.available()) ntpUDP.read();
      memset(ntpPacketBuffer, 0, sizeof(ntpPacketBuffer));
      ntpPacketBuffer[0] = 0x23;  // NTP v4 client request
      ESP.random(requestToken, sizeof(requestToken));
      memcpy(ntpPacketBuffer + 40, requestToken, sizeof(requestToken));
      if (!ntpUDP.beginPacket(ntpAddress, 123) ||
          ntpUDP.write(ntpPacketBuffer, sizeof(ntpPacketBuffer)) != sizeof(ntpPacketBuffer) ||
          !ntpUDP.endPacket()) {
        ntpFailed("NTP: send failed");
      } else {
        ntpState = NTP_REQUEST_SENT;
        ntpRequestTime = millis();
      }
    }
  }
  if (ntpState == NTP_REQUEST_SENT) {
    int length = ntpUDP.parsePacket();
    if (length > 0) {
      bool sourceMatches = ntpUDP.remoteIP() == ntpAddress && ntpUDP.remotePort() == 123;
      int received = ntpUDP.read(ntpPacketBuffer, sizeof(ntpPacketBuffer));
      while (ntpUDP.available()) ntpUDP.read();
      // Ignore unrelated, unsynchronized, truncated or old replies.
      if (sourceMatches && received == 48 && (ntpPacketBuffer[0] & 7) == 4 &&
          (ntpPacketBuffer[0] >> 6) != 3 && ntpPacketBuffer[1] > 0 && ntpPacketBuffer[1] < 16 &&
          memcmp(ntpPacketBuffer + 24, requestToken, sizeof(requestToken)) == 0) {
        uint32_t seconds = (uint32_t(ntpPacketBuffer[40]) << 24) |
          (uint32_t(ntpPacketBuffer[41]) << 16) | (uint32_t(ntpPacketBuffer[42]) << 8) | ntpPacketBuffer[43];
        // Unsigned subtraction supports the NTP era rollover in 2036.
        uint32_t epoch = seconds - 2208988800UL;
        if (epoch >= 1700000000UL) {
          syncedEpoch = epoch;
          syncedMillis = millis();
          timeIsSynced = true;
          internetConnected = true;
          ntpState = NTP_IDLE;
          ++ntpSuccesses;
          ntpRetry.reset();
          lastError = "";
          invalidateDisplay();
        }
      }
    }
  }
  if (ntpState == NTP_IDLE && WiFi.status() == WL_CONNECTED) {
    if (ntpRetry.pending) {
      if (ntpRetry.isRetryTime(millis())) sendNTPRequestAsync();
    } else if (!ntpStarted || uint32_t(millis() - lastNTPUpdate) >= config.ntp_interval * 1000UL) {
      sendNTPRequestAsync();
    }
  }
}
