/*
 * web_server.cpp - Web server handlers
 * TJ-56-654 Weather Clock
 */

#include "globals.h"
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include "settings.h"

static void sendJSON(JsonDocument& doc);

#include "web_assets.h"

void ICACHE_FLASH_ATTR serveWebUI() {
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("ETag", WEB_ETAG);
  if (server.header("If-None-Match") == WEB_ETAG) { server.send(304); return; }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.send_P(200, PSTR("text/html; charset=utf-8"),
                reinterpret_cast<const char*>(WEB_PAGE), sizeof(WEB_PAGE));
}

void ICACHE_FLASH_ATTR handleRoot() { serveWebUI(); }
void ICACHE_FLASH_ATTR handleConfig() { serveWebUI(); }
void ICACHE_FLASH_ATTR handleDebug() { serveWebUI(); }

void ICACHE_FLASH_ATTR handleTestNTP() {
  ntpRetry.reset();
  sendNTPRequestAsync();

  server.sendHeader("Location", "/debug");
  server.send(303);
}

void ICACHE_FLASH_ATTR handleTestDisplay() {
  showNumber(8888, false);
  ipDisplayUntil = millis() + 3000UL;

  server.sendHeader("Location", "/debug");
  server.send(303);
}

static bool ICACHE_FLASH_ATTR applySettings(JsonObjectConst values, bool& restart) {
  Config next = config;
  NightSettings nextNight = nightSettings;
  FeatureSettings nextFeatures = featureSettings;
  const char* error = nullptr;
  if (!updateSettings(next, values, error) || !updateNightSettings(nextNight, values, error) ||
      !updateFeatureSettings(nextFeatures, values, error)) {
    server.send(400, "text/plain", error);
    return false;
  }
  restart = strcmp(next.ssid, config.ssid) || strcmp(next.password, config.password) ||
    strcmp(next.hostname, config.hostname) || strcmp(next.ntp_server, config.ntp_server);
  bool weatherChanged = next.latitude != config.latitude || next.longitude != config.longitude ||
    next.weather_enabled != config.weather_enabled;
  if (!saveSettings(next, nextNight, &nextFeatures)) {
    server.send(500, "text/plain", F("Could not save settings. Please try again."));
    return false;
  }
  config = next;
  nightSettings = nextNight;
  featureSettings = nextFeatures;
  if (!featureSettings.external_enabled) externalCard = ExternalCard();
  applyDisplaySettings();
  if (weatherChanged) resetWeather();
  else refreshSunTimes();
  return true;
}

void ICACHE_FLASH_ATTR handleConfigSave() {
  JsonDocument doc;
  for (int i = 0; i < server.args(); ++i) doc[server.argName(i)] = server.arg(i);
  if (doc.overflowed()) { server.send(413, "text/plain", F("Settings too large")); return; }
  bool restart;
  if (!applySettings(doc.as<JsonObjectConst>(), restart)) return;
  server.send(200, "text/html", restart
    ? F("<!DOCTYPE html><meta charset='UTF-8'><h1>Saved</h1><p>Restarting with the new network settings.</p>")
    : F("<!DOCTYPE html><meta charset='UTF-8'><meta http-equiv='refresh' content='2;url=/'><h1>Saved</h1><p>Settings applied.</p>"));
  if (restart) { delay(1000); ESP.restart(); }
}

void ICACHE_FLASH_ATTR handleAPITime() {
  char buf[256], timeText[9];
  unsigned long epoch = getAsyncEpoch();
  unsigned long local = timeIsSynced ? epoch + getTotalOffset(epoch) : 0;
  formatClockTime(timeText, sizeof(timeText), true);
  snprintf_P(buf, sizeof(buf),
    PSTR("{\"time\":\"%s\",\"hours\":%u,\"minutes\":%u,\"seconds\":%u,\"epoch\":%lu,\"synced\":%s}"),
    timeText, unsigned(local / 3600 % 24), unsigned(local / 60 % 60), unsigned(local % 60),
    epoch, timeIsSynced ? "true" : "false");
  server.send(200, "application/json", buf);
}

void ICACHE_FLASH_ATTR handleAPIStatus() {
  JsonDocument doc;
  JsonObject wifiInfo = doc["wifi"].to<JsonObject>();
  JsonObject timeInfo = doc["time"].to<JsonObject>();
  JsonObject systemInfo = doc["system"].to<JsonObject>();
  JsonObject displayInfo = doc["display"].to<JsonObject>();
  JsonObject unitsInfo = doc["units"].to<JsonObject>();
  JsonObject weatherInfo = doc["weather"].to<JsonObject>();
  JsonObject forecastInfo = doc["forecast"].to<JsonObject>();
  JsonObject cardInfo = doc["card"].to<JsonObject>();

  char timeText[9];
  formatClockTime(timeText, sizeof(timeText), true);
  wifiInfo["ssid"] = WiFi.SSID();
  wifiInfo["ip"] = WiFi.localIP().toString();
  wifiInfo["rssi"] = WiFi.RSSI();
  wifiInfo["hostname"] = config.hostname;
  timeInfo["current"] = timeText;
  timeInfo["timezone_offset"] = config.timezone_offset;
  timeInfo["ntp_synced"] = timeIsSynced;
  unsigned long epoch = getAsyncEpoch();
  timeInfo["epoch"] = epoch;
  timeInfo["offset"] = getTotalOffset(epoch);
  timeInfo["hour_format_24"] = config.hour_format_24;
  timeInfo["dst_enabled"] = config.dst_enabled;
  systemInfo["firmware_version"] = FIRMWARE_VERSION;
  displayInfo["night_active"] = isNightModeActive();
  displayInfo["night_dim"] = bool(featureSettings.night_action);
  displayInfo["screen"] = displayMode;
  displayInfo["paused"] = displayPaused;
  for (uint8_t i = 0; i <= SCREEN_COUNT; ++i) displayInfo["available"].add(isModeEnabled(i));
  unitsInfo["temperature"] = featureSettings.temperature_unit;
  unitsInfo["wind"] = featureSettings.wind_unit;
  weatherInfo["enabled"] = config.weather_enabled;
  weatherInfo["valid"] = weather.valid;
  weatherInfo["stale"] = weather.stale;
  weatherInfo["temperature"] = weather.temperature;
  weatherInfo["code"] = weather.weathercode;
  weatherInfo["windspeed"] = weather.windspeed;
  weatherInfo["age_seconds"] = weather.valid ? (millis() - weather.lastUpdate) / 1000 : 0;
  weatherInfo["city"] = config.city_name;
  weatherInfo["sunrise"] = sunTimes.sunrise;
  weatherInfo["sunset"] = sunTimes.sunset;
  weatherInfo["source_epoch"] = weather.sourceEpoch;
  weatherInfo["source_age_seconds"] = weather.valid ? weatherSourceAge() : 0;
  weatherInfo["is_day"] = weather.isDay;
  weatherInfo["comfort_valid"] = weather.comfortValid;
  weatherInfo["feels_like"] = weather.feelsLike;
  weatherInfo["humidity"] = weather.humidity;
  weatherInfo["wind_direction"] = weather.windDirection;
  for (uint8_t i = 0; i < forecast.count; ++i) {
    JsonObject hour = forecastInfo["hours"].add<JsonObject>();
    hour["epoch"] = forecast.hours[i].epoch;
    hour["temperature"] = forecast.hours[i].temperature;
    hour["rain"] = forecast.hours[i].rain;
  }
  for (const auto& item : forecast.days) {
    JsonObject day = forecastInfo["days"].add<JsonObject>();
    day["epoch"] = item.epoch; day["valid"] = item.valid;
    day["low"] = item.low; day["high"] = item.high; day["uv"] = item.uv;
  }
  cardInfo["enabled"] = bool(featureSettings.external_enabled);
  cardInfo["active"] = externalCardActive();
  if (externalCardActive()) {
    cardInfo["title"] = externalCard.title; cardInfo["value"] = externalCard.value;
    cardInfo["unit"] = externalCard.unit;
    cardInfo["remaining_seconds"] = (externalCard.ttl - uint32_t(millis() - externalCard.received)) / 1000;
  }
  systemInfo["uptime"] = millis() / 1000;
  systemInfo["free_heap"] = ESP.getFreeHeap();
  systemInfo["max_free_block"] = ESP.getMaxFreeBlockSize();
  systemInfo["heap_fragmentation"] = ESP.getHeapFragmentation();
  systemInfo["chip_id"] = String(ESP.getChipId(), HEX);
  sendJSON(doc);
}

void ICACHE_FLASH_ATTR handleAPIDebug() {
  JsonDocument doc;
  doc["internet_connected"] = internetConnected;
  doc["ntp_attempts"] = ntpAttempts;
  doc["ntp_successes"] = ntpSuccesses;
  doc["last_error"] = lastError;
  doc["gateway"] = WiFi.gatewayIP().toString();
  doc["dns"] = WiFi.dnsIP().toString();
  sendJSON(doc);
}

void ICACHE_FLASH_ATTR handleAPIWeather() {
  char buf[256];

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", F(""));

  snprintf_P(buf, sizeof(buf),
    PSTR("{\"enabled\":%s,\"valid\":%s,\"temperature\":%.1f,\"weathercode\":%d,\"windspeed\":%.1f,\"last_update\":%lu,\"stale\":%s,\"age_seconds\":%lu,"),
    config.weather_enabled ? "true" : "false",
    weather.valid ? "true" : "false",
    weather.temperature,
    weather.weathercode,
    weather.windspeed,
    weather.lastUpdate,
    weather.stale ? "true" : "false",
    weather.valid ? (millis() - weather.lastUpdate) / 1000 : 0);
  server.sendContent(buf);

  snprintf_P(buf, sizeof(buf),
    PSTR("\"sunrise\":\"%s\",\"sunset\":\"%s\",\"sunrise_minutes\":%d,\"sunset_minutes\":%d}"),
    sunTimes.sunrise,
    sunTimes.sunset,
    sunTimes.sunriseMinutes,
    sunTimes.sunsetMinutes);
  server.sendContent(buf);

  server.sendContent("");
}

// Buffered writer keeps JSON escaped correctly without assembling another large String.
class ResponseWriter {
  char buffer[128];
  size_t used = 0;
public:
  size_t write(uint8_t byte) {
    buffer[used++] = char(byte);
    if (used == sizeof(buffer)) flush();
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) write(data[i]);
    return size;
  }
  void flush() {
    if (used) server.sendContent(buffer, used);
    used = 0;
  }
};

static void ICACHE_FLASH_ATTR sendJSON(JsonDocument& doc) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", F(""));
  ResponseWriter writer;
  serializeJson(doc, writer);
  writer.flush();
  server.sendContent("");
}

void ICACHE_FLASH_ATTR handleAPIConfigExport() {
  JsonDocument doc;
  exportSettings(config, doc);
  exportNightSettings(nightSettings, doc);
  exportFeatureSettings(featureSettings, doc);
  server.sendHeader("Content-Disposition", "attachment; filename=clock-config.json");
  sendJSON(doc);
}

static bool ICACHE_FLASH_ATTR readObject(JsonDocument& doc, size_t limit) {
  if (!server.hasArg("plain") || server.arg("plain").length() > limit ||
      deserializeJson(doc, server.arg("plain"), DeserializationOption::NestingLimit(2)) || !doc.is<JsonObject>()) {
    server.send(400, "text/plain", F("Invalid or oversized JSON object")); return false;
  }
  return true;
}

static void ICACHE_FLASH_ATTR handleDisplayControl() {
  JsonDocument doc;
  if (!readObject(doc, 128)) return;
  const char* action = doc["action"].as<const char*>();
  int screen = doc["screen"].is<int>() ? doc["screen"].as<int>() : -1;
  if (!action || !controlDisplay(action, screen)) {
    server.send(400, "text/plain", F("Choose next, hold, resume or show an available screen")); return;
  }
  server.send(200, "application/json", F("{\"status\":\"ok\"}"));
}

static void ICACHE_FLASH_ATTR handleExternalCard() {
  if (!featureSettings.external_enabled) { server.send(403, "text/plain", F("Enable external cards in Settings first")); return; }
  JsonDocument doc;
  if (!readObject(doc, 512)) return;
  static bool accepted = false;
  static uint32_t lastAccepted = 0;
  if (accepted && uint32_t(millis() - lastAccepted) < 1000) {
    server.send(429, "text/plain", F("Send at most one card per second")); return;
  }
  const char* error = nullptr;
  if (!updateExternalCard(externalCard, doc.as<JsonObjectConst>(), millis(), error)) {
    server.send(400, "text/plain", error); return;
  }
  accepted = true; lastAccepted = millis(); invalidateDisplay();
  server.send(200, "application/json", F("{\"status\":\"ok\"}"));
}

void ICACHE_FLASH_ATTR handleAPIConfigImport() {
  if (!server.hasArg("plain") || server.arg("plain").length() > 2048) {
    server.send(400, "text/plain", F("Expected a settings object (max 2048 bytes)"));
    return;
  }
  JsonDocument doc;
  String body = server.arg("plain");
  if (deserializeJson(doc, body.c_str(), DeserializationOption::NestingLimit(3)) || !doc.is<JsonObject>()) {
    server.send(400, "text/plain", F("Invalid settings JSON"));
    return;
  }
  bool restart;
  if (!applySettings(doc.as<JsonObjectConst>(), restart)) return;
  server.send(200, "application/json", restart
    ? "{\"status\":\"ok\",\"restart\":true}"
    : "{\"status\":\"ok\",\"restart\":false}");
  if (restart) { delay(1000); ESP.restart(); }
}

void ICACHE_FLASH_ATTR handleEEPROMClear() {
  if (!requireMaintenanceAuth()) return;
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  EEPROM.end();

  Serial.println("EEPROM cleared!");

  server.send(200, "application/json", F("{\"status\":\"ok\",\"message\":\"EEPROM cleared, device will reboot\"}"));

  delay(1000);
  WiFi.persistent(true);
  WiFi.disconnect(true);
  WiFi.persistent(false);
  ESP.restart();
}

void ICACHE_FLASH_ATTR handleReboot() {
  if (!requireMaintenanceAuth()) return;
  Serial.println("Reboot requested via web interface");

  server.send(200, "application/json", F("{\"status\":\"ok\",\"message\":\"Device rebooting...\"}"));

  delay(1000);
  ESP.restart();
}

void ICACHE_FLASH_ATTR handleI2CScan() {
  String json = "{\"i2c_scan\":{\"devices\":[";

  int deviceCount = 0;

  for (uint8_t address = 0x08; address <= 0x77; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      if (deviceCount > 0) json += ",";
      json += "{\"address\":\"0x";
      if (address < 16) json += "0";
      json += String(address, HEX);
      json += "\",\"decimal\":" + String(address) + "}";
      deviceCount++;
    }
    delay(1);
  }

  json += "],\"count\":" + String(deviceCount);

  json += ",\"oled_test\":{";
  Wire.beginTransmission(0x3C);
  json += "\"0x3C\":\"" + String(Wire.endTransmission() == 0 ? "FOUND" : "not found") + "\",";
  Wire.beginTransmission(0x3D);
  json += "\"0x3D\":\"" + String(Wire.endTransmission() == 0 ? "FOUND" : "not found") + "\"";
  json += "}}}";

  Serial.println("I2C Scan results: " + json);

  server.send(200, "application/json", json);
}

// Setup web server
void ICACHE_FLASH_ATTR setupWebServer() {
  setupWebUpdate();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfigSave);
  server.on("/maintenance/pin", HTTP_POST, handleShowMaintenancePIN);
  server.on("/api/maintenance/verify", HTTP_POST, []() {
    if (requireMaintenanceAuth()) server.send(200, "application/json", F("{\"status\":\"ok\"}"));
  });
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/test-ntp", HTTP_GET, handleTestNTP);
  server.on("/test-display", HTTP_GET, handleTestDisplay);
  server.on("/api/time", HTTP_GET, handleAPITime);
  server.on("/api/status", HTTP_GET, handleAPIStatus);
  server.on("/api/display", HTTP_POST, handleDisplayControl);
  server.on("/api/card", HTTP_POST, handleExternalCard);
  server.on("/api/debug", HTTP_GET, handleAPIDebug);
  server.on("/api/weather", HTTP_GET, handleAPIWeather);
  server.on("/api/config", HTTP_GET, handleAPIConfigExport);
  server.on("/api/config", HTTP_POST, handleAPIConfigImport);
  server.on("/api/eeprom-clear", HTTP_POST, handleEEPROMClear);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/i2c-scan", HTTP_GET, handleI2CScan);

  server.begin();
  Serial.println("Web server started");

  if (MDNS.begin(config.hostname)) {
    Serial.printf("mDNS responder started: %s.local\n", config.hostname);
    MDNS.addService("http", "tcp", 80);
  }
}
