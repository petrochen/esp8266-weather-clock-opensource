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
  const char* error = nullptr;
  if (!updateSettings(next, values, error) || !updateNightSettings(nextNight, values, error)) {
    server.send(400, "text/plain", error);
    return false;
  }
  restart = strcmp(next.ssid, config.ssid) || strcmp(next.password, config.password) ||
    strcmp(next.hostname, config.hostname) || strcmp(next.ntp_server, config.ntp_server);
  bool weatherChanged = next.latitude != config.latitude || next.longitude != config.longitude ||
    next.weather_enabled != config.weather_enabled;
  if (!saveSettings(next, nextNight)) {
    server.send(500, "text/plain", F("Could not save settings. Please try again."));
    return false;
  }
  config = next;
  nightSettings = nextNight;
  applyDisplaySettings();
  if (weatherChanged) resetWeather();
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
  char timeText[9];
  formatClockTime(timeText, sizeof(timeText), true);
  doc["wifi"]["ssid"] = WiFi.SSID();
  doc["wifi"]["ip"] = WiFi.localIP().toString();
  doc["wifi"]["rssi"] = WiFi.RSSI();
  doc["wifi"]["hostname"] = config.hostname;
  doc["time"]["current"] = timeText;
  doc["time"]["timezone_offset"] = config.timezone_offset;
  doc["time"]["ntp_synced"] = timeIsSynced;
  unsigned long epoch = getAsyncEpoch();
  doc["time"]["epoch"] = epoch;
  doc["time"]["offset"] = getTotalOffset(epoch);
  doc["time"]["hour_format_24"] = config.hour_format_24;
  doc["system"]["firmware_version"] = FIRMWARE_VERSION;
  doc["display"]["night_active"] = isNightModeActive();
  doc["weather"]["enabled"] = config.weather_enabled;
  doc["weather"]["valid"] = weather.valid;
  doc["weather"]["stale"] = weather.stale;
  doc["weather"]["temperature"] = weather.temperature;
  doc["weather"]["code"] = weather.weathercode;
  doc["weather"]["windspeed"] = weather.windspeed;
  doc["weather"]["age_seconds"] = weather.valid ? (millis() - weather.lastUpdate) / 1000 : 0;
  doc["weather"]["city"] = config.city_name;
  doc["weather"]["sunrise"] = sunTimes.sunrise;
  doc["weather"]["sunset"] = sunTimes.sunset;
  doc["system"]["uptime"] = millis() / 1000;
  doc["system"]["free_heap"] = ESP.getFreeHeap();
  doc["system"]["max_free_block"] = ESP.getMaxFreeBlockSize();
  doc["system"]["heap_fragmentation"] = ESP.getHeapFragmentation();
  doc["system"]["chip_id"] = String(ESP.getChipId(), HEX);
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
  server.sendHeader("Content-Disposition", "attachment; filename=clock-config.json");
  sendJSON(doc);
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
