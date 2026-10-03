#include <cassert>
#include <iostream>
// The runner stages the original modules next to the hardware substitutes.
#include "weather.cpp"
#include "ntp_client.cpp"

static const char* goodWeather = R"({"current":{"time":1791028800,"temperature_2m":22.5,"weather_code":1,"wind_speed_10m":4.0,"apparent_temperature":21,"relative_humidity_2m":60,"is_day":1,"wind_direction_10m":270},"daily":{"time":[1790985600,1791072000],"sunrise":[1791012600,1791099000],"sunset":[1791054000,1791140400],"temperature_2m_min":[16,15],"temperature_2m_max":[23,22],"uv_index_max":[4.2,3.5]},"hourly":{"time":[1791032400,1791036000,1791039600],"temperature_2m":[22,21,20],"precipitation_probability":[0,40,80]}})";
int main() {
  // One failure must retry at the deadline, not immediately or never.
  processWeather(); assert(weatherRequest.sends == 1);
  weatherRequest.code = 502; weatherRequest.complete();
  assert(weatherState == WEATHER_IDLE && weatherRetry.pending);
  processWeather(); assert(weatherRequest.sends == 1);
  fakeMillis += 1000; processWeather(); assert(weatherRequest.sends == 2);
  weatherRequest.code = 200; weatherRequest.payload = goodWeather; weatherRequest.complete();
  assert(weather.valid && !weather.stale && weather.temperature == 22.5f);
  // Keep cached data and back off on invalid JSON/schema.
  fetchWeatherAsync(); weatherRequest.payload = "{}"; weatherRequest.complete();
  assert(weather.valid && weather.stale && weather.temperature == 22.5f);
  assert(weatherRetry.pending);
  // Exhaust all 3 retries, then resume after the configured normal interval.
  for(int i=0;i<3;++i){fakeMillis=weatherRetry.nextRetryTime;processWeather();weatherRequest.complete();}
  assert(!weatherRetry.pending && weatherState == WEATHER_IDLE);
  int sent=weatherRequest.sends;
  fakeMillis += config.weather_interval*1000-1; processWeather();assert(weatherRequest.sends==sent);
  ++fakeMillis;processWeather();assert(weatherRequest.sends==sent+1);
  // Transport timeout aborts once; an abort callback cannot double-schedule.
  auto aborts=weatherRequest.aborts;
  fakeMillis += 15001; processWeather();assert(weatherRequest.aborts==aborts+1);
  assert(weatherRetry.currentRetry==1 && weatherState==WEATHER_IDLE);
  resetWeather(); weatherRequest.openResult=false; processWeather();assert(weatherRetry.pending);
  weatherRequest.openResult=true; weatherRequest.synchronous=true; weatherRequest.payload=goodWeather;
  fakeMillis=weatherRetry.nextRetryTime;processWeather();
  assert(weatherState==WEATHER_IDLE && weather.valid && !weatherRetry.pending);
  weatherRequest.synchronous=false;
  assert(weather.comfortValid && weather.feelsLike == 21 && weather.humidity == 60);
  assert(weather.windDirection == 270 && forecast.count == 3 && forecast.hours[0].rain == 0);
  assert(forecast.days[0].valid && forecast.days[1].valid && sunTimes.lastDay >= 0);
  // Header/stream limits abort once, keeping the previous valid reading.
  fetchWeatherAsync(); weatherRequest.payload = std::string(3073, 'x');
  aborts = weatherRequest.aborts; weatherRequest.dataCallback(nullptr, &weatherRequest, 3073);
  assert(weatherRequest.aborts == aborts + 1 && weather.valid && weather.stale && weatherRetry.pending);
  weatherRequest.complete(); assert(weatherRequest.aborts == aborts + 1);
  weatherRequest.payload = goodWeather; fakeMillis = weatherRetry.nextRetryTime; processWeather(); weatherRequest.complete();
  // Small JSON can still exceed the parser allocation budget (many tiny nodes).
  fetchWeatherAsync(); weatherRequest.payload = "{\"extra\":[" + std::string("0");
  for (int i = 0; i < 900; ++i) weatherRequest.payload += ",0";
  weatherRequest.payload += "]}"; weatherRequest.complete();
  assert(weather.valid && weather.stale && weather.temperature == 22.5f && weatherRetry.pending);
  // Invalid parallel arrays/ordering and out-of-range optional values are absent.
  fetchWeatherAsync(); weatherRequest.payload = R"({"current":{"time":1791028800,"temperature_2m":12,"weather_code":0,"wind_speed_10m":1},"hourly":{"time":[1791032400,1791032400,1791036000],"temperature_2m":[12,13,null],"precipitation_probability":[101,30,40]},"daily":{"time":[1790985600],"temperature_2m_min":[20],"temperature_2m_max":[10],"uv_index_max":[31]}})";
  weatherRequest.complete();
  assert(weather.valid && !weather.stale && forecast.count == 1 && forecast.hours[0].rain == -1);
  assert(!forecast.days[0].valid && forecast.days[0].uv == -1 && !forecast.days[1].valid);
  // A valid current reading may have missing forecast/comfort data, never fake zeroes.
  fetchWeatherAsync(); weatherRequest.payload = R"({"current":{"time":1791028800,"temperature_2m":0,"weather_code":0,"wind_speed_10m":0,"apparent_temperature":null,"relative_humidity_2m":null}})";
  weatherRequest.complete();
  assert(weather.valid && !weather.comfortValid && weather.humidity == -1 && !forecast.count && sunTimes.lastDay == -1);
  fetchWeatherAsync(); weatherRequest.payload = goodWeather; weatherRequest.complete();
  std::cout << "PASS: weather 502 recovery, stale cache, invalid schema, retry exhaustion, watchdog abort, open failure, synchronous callback\n";

  // Failed startup DNS does not gate future attempts.
  fakeMillis=20000;processNTPResponse();assert(ntpState==NTP_RESOLVING);
  auto oldCallback=dnsCallback;auto oldArgument=dnsArgument;
  dnsCallback(nullptr,nullptr,dnsArgument);processNTPResponse();assert(ntpRetry.pending);
  fakeMillis=ntpRetry.nextRetryTime;processNTPResponse();assert(ntpState==NTP_RESOLVING);
  ip_addr_t address;
  oldCallback(nullptr,&address,oldArgument);assert(!dnsReady);
  dnsCallback(nullptr,&address,dnsArgument);processNTPResponse();
  assert(ntpUDP.sends==1 && ntpState==NTP_REQUEST_SENT);
  // Valid NTP reply echoes our token and is accepted without another request.
  ntpUDP.incoming.assign(48,0);ntpUDP.cursor=0;
  ntpUDP.incoming[0]=0x24;ntpUDP.incoming[1]=2;
  memcpy(ntpUDP.incoming.data()+24,requestToken,8);
  uint32_t timestamp=1791028800UL+2208988800UL;
  for(int i=0;i<4;++i) ntpUDP.incoming[40+i]=uint8_t(timestamp>>(24-8*i));
  processNTPResponse();assert(timeIsSynced && ntpState==NTP_IDLE && ntpUDP.sends==1);
  assert(getAsyncEpoch()==1791028800UL);
  config.dst_enabled = false; config.timezone_offset = 3600; refreshSunTimes();
  assert(!strcmp(sunTimes.sunrise, "08:30")); // UTC event + selected clock offset
  config.timezone_offset = -3600; refreshSunTimes();
  assert(!strcmp(sunTimes.sunrise, "06:30"));
  syncedEpoch = 1791259200; syncedMillis = fakeMillis; refreshSunTimes();
  assert(sunTimes.lastDay == -1); // never keep yesterday's event after cache expires
  processWeather(); assert(weather.stale && weatherSourceAge() >= 172800);
  syncedEpoch = 1791028800;
  syncedMillis=UINT32_MAX-499;fakeMillis=500;
  assert(getAsyncEpoch()==1791028801UL);
  char text[9]; config.dst_enabled=false;config.timezone_offset=3600;
  formatClockTime(text,sizeof(text),true);assert(!strcmp(text,"13:00:01"));
  assert(isDST(0)==false);
  config.dst_enabled=true;
  assert(!isDST(1774745999UL));assert(isDST(1774746000UL));
  assert(isDST(1792889999UL));assert(!isDST(1792890000UL));
  // WiFi deadline zero is valid too (5-second first retry).
  WiFiRetryConfig wifiRetry;
  fakeMillis = UINT32_MAX - 4999;
  wifiRetry.scheduleRetry();
  assert(wifiRetry.nextRetryTime == 0 && !wifiRetry.isRetryTime());
  fakeMillis = 0; assert(wifiRetry.isRetryTime());
  wifiRetry.reset(); assert(!wifiRetry.isRetryTime());
  std::cout << "PASS: startup DNS recovery, stale DNS callback, one NTP request, clock rollover, timezone and DST boundaries\n";
}
