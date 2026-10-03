#pragma once
#include "config.h"
#include "lwip/dns.h"
#include <vector>
#include <array>
#include <ctime>

inline uint32_t fakeMillis = 20000;
inline unsigned long millis() { return fakeMillis; }
struct SerialMock {
 template<class... T> void printf(const char*, T...) {}
 template<class T> void println(T) {}
 template<class T> void print(T) {}
} inline Serial;
struct IPAddress {
 uint32_t value = 1;
 IPAddress() = default;
 IPAddress(const ip_addr_t* ip):value(ip->address){}
 uint8_t operator[](size_t i) const {return uint8_t(value >> (8*i));}
 bool operator==(const IPAddress& other) const {return value == other.value;}
};
constexpr int WL_CONNECTED = 3;
struct WiFiMock {
 int connected = WL_CONNECTED;
 String ssid = "test";
 IPAddress ip;
 int status(){return connected;}
 String SSID(){return ssid;}
 IPAddress localIP(){return ip;}
} inline WiFi;
struct ESPMock {
 uint32_t nextValue=0;
 int randomCalls=0;
 std::vector<uint32_t> randomValues;
 uint32_t random() {
  ++randomCalls;
  if(randomValues.empty())return nextValue++;
  uint32_t value=randomValues.front();randomValues.erase(randomValues.begin());return value;
 }
 void random(uint8_t* out, size_t n) {for(size_t i=0;i<n;++i)out[i]=uint8_t(i+1);}
} inline ESP;
struct UDPMock {
  std::vector<uint8_t> incoming;
  std::array<uint8_t,48> sent{};
  IPAddress from;
  unsigned short port = 123;
  int sends = 0;
  size_t cursor = 0;
  bool sendOK = true;
  int parsePacket() { return cursor < incoming.size() ? int(incoming.size()-cursor) : 0; }
  int available() { return parsePacket(); }
  int read() {return available() ? incoming[cursor++] : -1;}
  int read(uint8_t* out, size_t n) {size_t count=0; while(count<n && available())out[count++]=incoming[cursor++]; return int(count);}
  bool beginPacket(IPAddress, int) {return sendOK;}
  size_t write(const uint8_t* p, size_t n){memcpy(sent.data(),p,n);return n;}
  bool endPacket(){++sends;return sendOK;}
  IPAddress remoteIP(){return from;}
  unsigned short remotePort(){return port;}
} inline ntpUDP;

inline Config config;
inline WeatherData weather;
inline SunTimes sunTimes;
inline FeatureSettings featureSettings;
inline ForecastData forecast;
inline ExternalCard externalCard;
inline bool displayPaused = false;
inline RetryConfig weatherRetry, ntpRetry;
inline volatile WeatherState weatherState = WEATHER_IDLE;
inline volatile NTPState ntpState = NTP_IDLE;
inline unsigned long lastWeatherUpdate = 0, weatherRequestStart = 0;
inline unsigned long syncedEpoch = 0, syncedMillis = 0, lastNTPUpdate = 0, ntpRequestTime = 0;
inline bool timeIsSynced = false, internetConnected = false;
inline int ntpAttempts = 0, ntpSuccesses = 0, invalidations = 0;
inline byte ntpPacketBuffer[48];
inline String lastError;
#ifndef HOST_DISPLAY_TEST
inline void invalidateDisplay(){++invalidations;}
inline void wakeDisplay(){}
#else
void invalidateDisplay();
void wakeDisplay();
#endif
unsigned long getAsyncEpoch();
long getTotalOffset(unsigned long);
extern NightSettings nightSettings;
bool isNightModeActive();

// Maintenance API substitutes: never use real EEPROM, display, or HTTP.
#ifndef HOST_DISPLAY_TEST
constexpr int SSD1306_WHITE = 1;
struct DisplayMock {
  std::string text;
  void clearDisplay(){text.clear();}
  void setTextSize(int){}
  void setRotation(int){}
  void setTextColor(int){}
  void setCursor(int,int){}
  void println(const char* s){text += s; text += '\n';}
  void display(){}
} inline display;
inline unsigned long lastModeSwitch = 0;
#endif
struct ServerMock {
  int status = 0;
  std::string username,password,body;
  String authorization;
  String header(const char*){return authorization;}
  void send(int code,const char*,const char* text){status=code;body=text;}
  bool authenticate(const char* user,const char* pass){return username==user && password==pass;}
  void requestAuthentication(int,const char*,const char*){status=401;}
} inline server;
constexpr int BASIC_AUTH = 0;
