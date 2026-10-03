/* Weather acquisition and retry scheduling. */
#include <ESPAsyncTCP.h>
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN_TARGET "AsyncHTTPRequest_Generic v1.13.0"
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN 1013000
#include <AsyncHTTPRequest_Generic.h>
#include "globals.h"
#include <ArduinoJson.h>
#include <math.h>

static AsyncHTTPRequest weatherRequest;
static bool weatherStarted = false;

static void ICACHE_FLASH_ATTR weatherFailed(const String& message) {
  // Keep the last good reading visible, but explicitly mark it stale.
  lastError = message;
  weather.stale = true;
  weatherState = WEATHER_IDLE;
  if (!weatherRetry.scheduleRetry(millis())) {
    weatherRetry.reset();
    lastWeatherUpdate = millis();  // exhausted burst: wait a full normal interval
  }
  invalidateDisplay();
}

static bool ICACHE_FLASH_ATTR parseSunTime(const char* value, char* text, int& minutes) {
  if (!value || strlen(value) < 16 || value[10] != 'T' || value[13] != ':') return false;
  const int positions[] = {11, 12, 14, 15};
  for (int pos : positions) if (value[pos] < '0' || value[pos] > '9') return false;
  int hour = (value[11] - '0') * 10 + value[12] - '0';
  int minute = (value[14] - '0') * 10 + value[15] - '0';
  if (hour > 23 || minute > 59) return false;
  memcpy(text, value + 11, 5);
  text[5] = '\0';
  minutes = hour * 60 + minute;
  return true;
}

void ICACHE_FLASH_ATTR onWeatherResponse(void*, AsyncHTTPRequest* request, int readyState) {
  if (readyState != 4 || weatherState != WEATHER_REQUESTING) return;
  int code = request->responseHTTPcode();
  if (code != 200) {
    weatherFailed(String("Weather API: ") + code);
    return;
  }

  JsonDocument doc;
  String payload = request->responseText();
  if (deserializeJson(doc, payload.c_str())) {
    weatherFailed(F("Weather: invalid JSON"));
    return;
  }
  JsonObject current = doc["current_weather"];
  if (!current["temperature"].is<float>() || !current["windspeed"].is<float>() ||
      !current["weathercode"].is<int>()) {
    weatherFailed(F("Weather: missing fields"));
    return;
  }
  float temperature = current["temperature"];
  float windspeed = current["windspeed"];
  int codeValue = current["weathercode"];
  if (!isfinite(temperature) || !isfinite(windspeed) || temperature < -100 ||
      temperature > 70 || windspeed < 0 || codeValue < 0 || codeValue > 99) {
    weatherFailed(F("Weather: invalid values"));
    return;
  }

  weather.temperature = temperature;
  weather.windspeed = windspeed;
  weather.weathercode = codeValue;
  weather.lastUpdate = millis();
  weather.valid = true;
  weather.stale = false;

  SunTimes nextSun;
  if (parseSunTime(doc["daily"]["sunrise"][0], nextSun.sunrise, nextSun.sunriseMinutes) &&
      parseSunTime(doc["daily"]["sunset"][0], nextSun.sunset, nextSun.sunsetMinutes)) {
    time_t epoch = getAsyncEpoch();
    nextSun.lastDay = timeIsSynced ? gmtime(&epoch)->tm_yday : 0;
  }
  sunTimes = nextSun;  // do not leave yesterday's sun times after a partial response
  weatherRetry.reset();
  weatherState = WEATHER_IDLE;
  lastError = "";
  invalidateDisplay();
}

void ICACHE_FLASH_ATTR fetchWeatherAsync() {
  if (!config.weather_enabled || WiFi.status() != WL_CONNECTED ||
      weatherState != WEATHER_IDLE) return;

  char url[240];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
    "&current_weather=true&daily=sunrise,sunset&timezone=auto&forecast_days=1",
    config.latitude, config.longitude);

  weatherStarted = true;
  lastWeatherUpdate = millis();
  weatherRetry.consume();
  weatherRequestStart = millis();
  // Set state before open/send: callbacks can run synchronously on failure.
  weatherState = WEATHER_REQUESTING;
  weatherRequest.onReadyStateChange(onWeatherResponse);
  weatherRequest.setTimeout(10);
  if (!weatherRequest.open("GET", url) || !weatherRequest.send()) {
    if (weatherState == WEATHER_REQUESTING) weatherFailed(F("Weather: request failed"));
  }
}

void ICACHE_FLASH_ATTR resetWeather() {
  // Suppress the abort callback before releasing the transport.
  weatherState = WEATHER_IDLE;
  weatherRequest.abort();
  weatherRetry.reset();
  weatherStarted = false;
  weather = WeatherData();
  sunTimes = SunTimes();
  invalidateDisplay();
}

void ICACHE_FLASH_ATTR processWeather() {
  if (weather.valid && !weather.stale &&
      uint32_t(millis() - weather.lastUpdate) >= config.weather_interval * 1000UL) {
    weather.stale = true;
    invalidateDisplay();
  }
  if (weatherState == WEATHER_REQUESTING && uint32_t(millis() - weatherRequestStart) > 15000UL) {
    weatherState = WEATHER_IDLE;
    weatherRequest.abort();
    weatherFailed(F("Weather: request timeout"));
  }
  static bool bootReady = false;
  if (millis() > 10000UL) bootReady = true;
  if (!bootReady || !config.weather_enabled || WiFi.status() != WL_CONNECTED ||
      weatherState != WEATHER_IDLE) return;

  if (weatherRetry.pending) {
    if (weatherRetry.isRetryTime(millis())) fetchWeatherAsync();
  } else if (!weatherStarted || uint32_t(millis() - lastWeatherUpdate) >= config.weather_interval * 1000UL) {
    fetchWeatherAsync();
  }
}
