#include "settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool readNumber(JsonVariantConst value, double& out) {
  if (value.is<bool>() || value.isNull()) return false;
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    char* end;
    out = strtod(text, &end);
    return end != text && *end == '\0' && isfinite(out);
  }
  if (!value.is<double>()) return false;
  out = value.as<double>();
  return isfinite(out);
}

static bool readBool(JsonVariantConst value, bool& out) {
  if (value.is<bool>()) { out = value.as<bool>(); return true; }
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    if (!strcmp(text, "true") || !strcmp(text, "1")) { out = true; return true; }
    if (!strcmp(text, "false") || !strcmp(text, "0")) { out = false; return true; }
  }
  return false;
}

static bool copyField(JsonObjectConst values, const char* key, char* out, size_t size, bool empty) {
  if (!values[key].is<const char*>()) return false;
  JsonString text = values[key].as<JsonString>();
  if ((!empty && !text.size()) || text.size() >= size) return false;
  // Embedded NUL/control bytes cannot round-trip through a C string or HTML form.
  for (size_t i = 0; i < text.size(); ++i) if (uint8_t(text.c_str()[i]) < 32 || text.c_str()[i] == 127) return false;
  memcpy(out, text.c_str(), text.size());
  out[text.size()] = '\0';
  return true;
}

bool updateSettings(Config& target, JsonObjectConst values, const char*& error) {
  if (values.isNull()) { error = "Expected a JSON object"; return false; }
  Config next = target;
#define TEXT_FIELD(key, field, empty) \
  if (!values[key].isUnbound() && !copyField(values, key, next.field, sizeof(next.field), empty)) { \
    error = "Invalid " key; return false; }
  TEXT_FIELD("ssid", ssid, false)
  TEXT_FIELD("hostname", hostname, false)
  TEXT_FIELD("ntp_server", ntp_server, false)
  TEXT_FIELD("city_name", city_name, true)
#undef TEXT_FIELD
  if (!values["password"].isUnbound()) {
    if (!values["password"].is<const char*>()) { error = "Invalid password"; return false; }
    // A blank field or omitted field keeps the saved password. Explicitly clear
    // it with clear_password=true when changing to an open WiFi network.
    if (values["password"].as<JsonString>().size() &&
        !copyField(values, "password", next.password, sizeof(next.password), true)) {
      error = "Invalid password"; return false;
    }
  }
  if (!values["clear_password"].isUnbound()) {
    bool clear;
    if (!readBool(values["clear_password"], clear)) { error = "Invalid clear_password"; return false; }
    if (clear) next.password[0] = '\0';
  }
#define NUMBER_FIELD(key, field, low, high, integer) \
  if (!values[key].isUnbound()) { \
    double number; \
    if (!readNumber(values[key], number) || (integer && floor(number) != number)) { \
      error = "Invalid " key; return false; } \
    next.field = number < low ? low : (number > high ? high : number); \
  }
  NUMBER_FIELD("timezone", timezone_offset, -43200, 50400, true)
  NUMBER_FIELD("timezone_offset", timezone_offset, -43200, 50400, true)
  NUMBER_FIELD("brightness", brightness, 0, 7, true)
  NUMBER_FIELD("latitude", latitude, -90, 90, false)
  NUMBER_FIELD("longitude", longitude, -180, 180, false)
  NUMBER_FIELD("ntp_interval", ntp_interval, 60, 86400, true)
  NUMBER_FIELD("weather_interval", weather_interval, 60, 86400, true)
  NUMBER_FIELD("display_rotation_sec", display_rotation_sec, 1, 60, true)
  NUMBER_FIELD("display_orientation", display_orientation, 0, 3, true)
#undef NUMBER_FIELD
#define BOOL_FIELD(key, field) \
  if (!values[key].isUnbound() && !readBool(values[key], next.field)) { error = "Invalid " key; return false; }
  BOOL_FIELD("dst_enabled", dst_enabled)
  BOOL_FIELD("hour_format_24", hour_format_24)
  BOOL_FIELD("weather_enabled", weather_enabled)
  BOOL_FIELD("show_weather", show_weather)
  BOOL_FIELD("show_sunrise_sunset", show_sunrise_sunset)
#undef BOOL_FIELD
  target = next;
  return true;
}

void exportSettings(const Config& source, JsonDocument& doc) {
  doc["firmware_version"] = FIRMWARE_VERSION;
  // Passwords are intentionally never exported. Imports that omit them retain
  // the credentials already saved on this device.
#define FIELD(name) doc[#name] = source.name;
  FIELD(ssid) FIELD(timezone_offset) FIELD(dst_enabled) FIELD(brightness)
  FIELD(ntp_server) FIELD(ntp_interval) FIELD(hour_format_24) FIELD(hostname)
  FIELD(latitude) FIELD(longitude) FIELD(city_name) FIELD(weather_enabled)
  FIELD(weather_interval) FIELD(display_rotation_sec) FIELD(display_orientation)
  FIELD(show_weather) FIELD(show_sunrise_sunset)
#undef FIELD
}

bool updateNightSettings(NightSettings& target, JsonObjectConst values, const char*& error) {
  NightSettings next = target;
  if (!values["night_enabled"].isUnbound()) {
    bool enabled;
    if (!readBool(values["night_enabled"], enabled)) { error = "Invalid night_enabled"; return false; }
    next.enabled = enabled;
  }
#define NIGHT_FIELD(key, field, maximum) \
  if (!values[key].isUnbound()) { \
    double number; \
    if (!readNumber(values[key], number) || floor(number) != number || number < 0 || number > maximum) { \
      error = "Invalid " key; return false; } \
    next.field = number; \
  }
  NIGHT_FIELD("night_start_hour", start_hour, 23)
  NIGHT_FIELD("night_start_minute", start_minute, 59)
  NIGHT_FIELD("night_end_hour", end_hour, 23)
  NIGHT_FIELD("night_end_minute", end_minute, 59)
#undef NIGHT_FIELD
  target = next;
  return true;
}

void exportNightSettings(const NightSettings& source, JsonDocument& doc) {
  doc["night_enabled"] = bool(source.enabled);
  doc["night_start_hour"] = source.start_hour;
  doc["night_start_minute"] = source.start_minute;
  doc["night_end_hour"] = source.end_hour;
  doc["night_end_minute"] = source.end_minute;
}

bool nightModeAt(const NightSettings& night, unsigned long localEpoch, bool synced) {
  if (!synced || !night.enabled) return false;
  unsigned minute = localEpoch / 60 % 1440;
  unsigned start = night.start_hour * 60 + night.start_minute;
  unsigned end = night.end_hour * 60 + night.end_minute;
  // Equal endpoints disable the window; an unsynced clock always stays visible.
  return start > end ? minute >= start || minute < end : minute >= start && minute < end;
}

static const char* const screenKeys[SCREEN_COUNT] = {
  "screen_clock_sec", "screen_weather_sec", "screen_sun_sec", "screen_comfort_sec",
  "screen_rain_sec", "screen_daily_sec", "screen_wind_sec"
};

struct FeatureField {
  const char* name;
  uint8_t FeatureSettings::* member;
  uint8_t maximum; // 0 denotes a boolean
};
static const FeatureField featureFields[] = {
  {"temperature_unit", &FeatureSettings::temperature_unit, 1},
  {"wind_unit", &FeatureSettings::wind_unit, 2},
  {"night_action", &FeatureSettings::night_action, 1},
  {"night_brightness", &FeatureSettings::night_brightness, 7},
  {"clock_weather", &FeatureSettings::clock_weather, 0},
  {"dissolve", &FeatureSettings::dissolve, 0},
  {"external_enabled", &FeatureSettings::external_enabled, 0},
  {"sun_countdown", &FeatureSettings::sun_countdown, 0},
  {"show_comfort", &FeatureSettings::show_comfort, 0},
  {"show_rain", &FeatureSettings::show_rain, 0},
  {"show_daily", &FeatureSettings::show_daily, 0},
  {"show_uv", &FeatureSettings::show_uv, 0},
  {"screen_uv_sec", &FeatureSettings::screen_uv_sec, 120},
  {"show_wind", &FeatureSettings::show_wind, 0}
};

bool ICACHE_FLASH_ATTR updateFeatureSettings(FeatureSettings& target, JsonObjectConst values, const char*& error) {
  FeatureSettings next = target;
  for (uint8_t i = 0; i < SCREEN_COUNT; ++i) {
    if (values[screenKeys[i]].isUnbound()) continue;
    double number;
    if (!readNumber(values[screenKeys[i]], number) || floor(number) != number || number < 0 || number > 120) {
      error = "Screen duration must be 0..120 seconds"; return false;
    }
    next.seconds[i] = number;
  }
  for (const auto& field : featureFields) {
    JsonVariantConst value = values[field.name];
    if (value.isUnbound()) continue;
    if (field.maximum) {
      double number;
      if (!readNumber(value, number) || floor(number) != number || number < 0 || number > field.maximum) {
        error = "Invalid display option"; return false;
      }
      next.*(field.member) = number;
    } else {
      bool flag;
      if (!readBool(value, flag)) { error = "Expected a display option boolean"; return false; }
      next.*(field.member) = flag;
    }
  }
  target = next;
  return true;
}

void ICACHE_FLASH_ATTR exportFeatureSettings(const FeatureSettings& source, JsonDocument& doc) {
  for (uint8_t i = 0; i < SCREEN_COUNT; ++i) doc[screenKeys[i]] = source.seconds[i];
  for (const auto& field : featureFields) {
    if (field.maximum) doc[field.name] = source.*(field.member);
    else doc[field.name] = bool(source.*(field.member));
  }
}

bool ICACHE_FLASH_ATTR updateExternalCard(ExternalCard& target, JsonObjectConst values, uint32_t now, const char*& error) {
  ExternalCard next;
  double ttl;
  if (!readNumber(values["ttl"], ttl) || floor(ttl) != ttl || ttl < 0 || ttl > 3600) {
    error = "TTL must be 0..3600 seconds"; return false;
  }
  if (ttl == 0) { target = next; return true; }
  if (ttl < 5 || !copyField(values, "title", next.title, sizeof(next.title), false) ||
      !copyField(values, "value", next.value, sizeof(next.value), false) ||
      (!values["unit"].isUnbound() && !copyField(values, "unit", next.unit, sizeof(next.unit), true))) {
    error = "Use title <=31 bytes, value <=15, unit <=11 and TTL >=5"; return false;
  }
  next.received = now;
  next.ttl = uint32_t(ttl) * 1000;
  target = next;
  return true;
}
