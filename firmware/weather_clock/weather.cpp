/* Weather acquisition and retry scheduling. */
#include <ESPAsyncTCP.h>
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN_TARGET "AsyncHTTPRequest_Generic v1.13.0"
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN 1013000
#include <AsyncHTTPRequest_Generic.h>
#include "globals.h"
#include <ArduinoJson.h>
#include <math.h>
#include "bounded_json.h"

static AsyncHTTPRequest weatherRequest;
static bool weatherStarted = false;
static constexpr size_t WEATHER_RESPONSE_LIMIT = 3072;

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

static bool ICACHE_FLASH_ATTR numberIn(JsonVariantConst value, float low, float high) {
  if (!value.is<float>() || value.is<bool>()) return false;
  const float number = value.as<float>();
  return isfinite(number) && number >= low && number <= high;
}

static uint32_t ICACHE_FLASH_ATTR readEpoch(JsonVariantConst value) {
  return value.is<uint32_t>() && value.as<uint32_t>() >= 1577836800UL &&
    value.as<uint32_t>() < 4102444800UL ? value.as<uint32_t>() : 0;
}

uint32_t ICACHE_FLASH_ATTR weatherSourceAge() {
  uint32_t elapsed = uint32_t(millis() - weather.lastUpdate) / 1000;
  uint32_t now = getAsyncEpoch();
  if (timeIsSynced && weather.sourceEpoch && now > weather.sourceEpoch && now - weather.sourceEpoch > elapsed)
    return now - weather.sourceEpoch;
  return elapsed;
}

void ICACHE_FLASH_ATTR refreshSunTimes() {
  SunTimes next;
  uint32_t now = getAsyncEpoch();
  uint32_t day = (now + getTotalOffset(now)) / 86400;
  for (const auto& item : forecast.days) {
    if (!item.epoch || !item.sunrise || !item.sunset) continue;
    const uint32_t localSunrise = item.sunrise + getTotalOffset(item.sunrise);
    if (timeIsSynced && localSunrise / 86400 != day) continue;
    next.sunriseMinutes = localSunrise / 60 % 1440;
    next.sunsetMinutes = (item.sunset + getTotalOffset(item.sunset)) / 60 % 1440;
    next.lastDay = int(localSunrise / 86400);
    break;
  }
  // This runs every loop. Format only changed values, including date/offset
  // changes; missing or expired events retain the default "--:--" labels.
  if (next.lastDay != sunTimes.lastDay || next.sunriseMinutes != sunTimes.sunriseMinutes ||
      next.sunsetMinutes != sunTimes.sunsetMinutes) {
    if (next.lastDay >= 0) {
      snprintf(next.sunrise, sizeof(next.sunrise), "%02u:%02u", unsigned(next.sunriseMinutes / 60),
               unsigned(next.sunriseMinutes % 60));
      snprintf(next.sunset, sizeof(next.sunset), "%02u:%02u", unsigned(next.sunsetMinutes / 60),
               unsigned(next.sunsetMinutes % 60));
    }
    sunTimes = next;
    invalidateDisplay();
  }
}

static bool ICACHE_FLASH_ATTR responseWithinBudget(AsyncHTTPRequest* request) {
  if (request->responseLength() <= WEATHER_RESPONSE_LIMIT &&
      request->available() <= WEATHER_RESPONSE_LIMIT)
    return true;
  weatherState = WEATHER_IDLE; // suppress synchronous abort callbacks
  request->abort();
  weatherFailed(F("Weather response too large"));
  return false;
}

void ICACHE_FLASH_ATTR onWeatherResponse(void*, AsyncHTTPRequest* request, int readyState) {
  if (weatherState != WEATHER_REQUESTING || !responseWithinBudget(request) || readyState != 4) return;
  int code = request->responseHTTPcode();
  if (code != 200) {
    weatherFailed(String("Weather API: ") + code);
    return;
  }

  BoundedJsonAllocator allocator;
  JsonDocument doc(&allocator);
  String payload = request->responseText();
  if (payload.length() > WEATHER_RESPONSE_LIMIT ||
      deserializeJson(doc, payload.c_str(), DeserializationOption::NestingLimit(4))) {
    weatherFailed(F("Weather: invalid JSON"));
    return;
  }
  JsonObject current = doc["current"];
  if (!numberIn(current["temperature_2m"], -100, 70) || !numberIn(current["wind_speed_10m"], 0, 500) ||
      !current["weather_code"].is<int>() || !readEpoch(current["time"])) {
    weatherFailed(F("Weather: missing fields"));
    return;
  }
  float temperature = current["temperature_2m"];
  float windspeed = current["wind_speed_10m"];
  int codeValue = current["weather_code"];
  // Temperature and wind already passed numberIn(), including finite/range checks.
  if (codeValue < 0 || codeValue > 99) {
    weatherFailed(F("Weather: invalid values"));
    return;
  }

  weather.temperature = temperature;
  weather.windspeed = windspeed;
  weather.weathercode = codeValue;
  weather.sourceEpoch = readEpoch(current["time"]);
  weather.comfortValid = numberIn(current["apparent_temperature"], -120, 100);
  weather.feelsLike = weather.comfortValid ? current["apparent_temperature"].as<float>() : 0;
  weather.humidity = numberIn(current["relative_humidity_2m"], 0, 100) ? current["relative_humidity_2m"].as<int>() : -1;
  weather.windDirection = numberIn(current["wind_direction_10m"], 0, 360) ? current["wind_direction_10m"].as<int>() : -1;
  weather.isDay = current["is_day"].is<int>() ? current["is_day"].as<int>() != 0 : true;
  weather.lastUpdate = millis();
  weather.valid = true;
  weather.stale = false;

  ForecastData next;
  JsonObject hourly = doc["hourly"];
  for (uint8_t i = 0; i < 6; ++i) {
    uint32_t epoch = readEpoch(hourly["time"][i]);
    if (!epoch || !numberIn(hourly["temperature_2m"][i], -100, 70) ||
        (next.count && epoch <= next.hours[next.count - 1].epoch)) continue;
    auto& hour = next.hours[next.count++];
    hour.epoch = epoch;
    hour.temperature = hourly["temperature_2m"][i];
    if (numberIn(hourly["precipitation_probability"][i], 0, 100)) hour.rain = hourly["precipitation_probability"][i].as<int>();
  }
  JsonObject daily = doc["daily"];
  for (uint8_t i = 0; i < 2; ++i) {
    auto& day = next.days[i];
    day.epoch = readEpoch(daily["time"][i]);
    day.sunrise = readEpoch(daily["sunrise"][i]); day.sunset = readEpoch(daily["sunset"][i]);
    day.valid = day.epoch && numberIn(daily["temperature_2m_min"][i], -100, 70) && numberIn(daily["temperature_2m_max"][i], -100, 70);
    if (day.valid) {
      day.low = daily["temperature_2m_min"][i]; day.high = daily["temperature_2m_max"][i];
      day.valid = day.low <= day.high;
    }
    if (numberIn(daily["uv_index_max"][i], 0, 30)) day.uv = daily["uv_index_max"][i];
  }
  forecast = next;
  refreshSunTimes();
  weatherRetry.reset();
  weatherState = WEATHER_IDLE;
  lastError = "";
  invalidateDisplay();
}

void ICACHE_FLASH_ATTR fetchWeatherAsync() {
  if (!config.weather_enabled || WiFi.status() != WL_CONNECTED || weatherState != WEATHER_IDLE)
    return;

  char url[512];
  int length =
      snprintf_P(url, sizeof(url),
                 PSTR("http://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
                      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
                      "weather_code,wind_speed_10m,wind_direction_10m,is_day"
                      "&hourly=temperature_2m,precipitation_probability&daily=temperature_2m_min,"
                      "temperature_2m_max,sunrise,sunset,uv_index_max"
                      "&timezone=auto&timeformat=unixtime&forecast_days=2&forecast_hours=6"),
                 config.latitude, config.longitude);
  if (length < 0 || size_t(length) >= sizeof(url)) { weatherFailed(F("Weather URL too long")); return; }

  weatherStarted = true;
  lastWeatherUpdate = millis();
  weatherRetry.consume();
  weatherRequestStart = millis();
  // Set state before open/send: callbacks can run synchronously on failure.
  weatherState = WEATHER_REQUESTING;
  weatherRequest.onReadyStateChange(onWeatherResponse);
  weatherRequest.onData([](void*, AsyncHTTPRequest* request, size_t) {
    if (weatherState == WEATHER_REQUESTING) responseWithinBudget(request);
  });
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
  forecast = ForecastData();
  sunTimes = SunTimes();
  invalidateDisplay();
}

void ICACHE_FLASH_ATTR processWeather() {
  refreshSunTimes();
  if (weather.valid && !weather.stale &&
      (uint32_t(millis() - weather.lastUpdate) >= config.weather_interval * 1000UL ||
       weatherSourceAge() > 7200 || (timeIsSynced && weather.sourceEpoch > getAsyncEpoch() + 3600))) {
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
