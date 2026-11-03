/*
 * TJ-56-654 Weather Clock - Custom NTP Firmware with OTA
 *
 * Hardware:
 * - ESP-01S (ESP8266)
 * - GM009605v4.3 OLED 128x64 display (SSD1306 I2C)
 *
 * Connections:
 * - GPIO0 -> OLED SDA (I2C Data) - SWAPPED!
 * - GPIO2 -> OLED SCL (I2C Clock) - SWAPPED!
 *
 * Features:
 * - Async NTP time sync
 * - Async weather from Open-Meteo API
 * - OTA updates (web + ArduinoOTA)
 * - WiFi resilience with exponential backoff
 * - "Thanos snap" dissolve transition effect
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>

#include "config.h"
#include "globals.h"
#include "settings.h"

// ============ Global variable definitions ============

// Configuration
Config config;

// OLED Display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// NTP Client
WiFiUDP ntpUDP;

// Web server
ESP8266WebServer server(80);

// State machines — volatile: written from ESPAsyncTCP callbacks, read in main loop
volatile WeatherState weatherState = WEATHER_IDLE;
volatile NTPState ntpState = NTP_IDLE;
WiFiConnectionState wifiConnState = WIFI_CONN_IDLE;

// Retry configurations
RetryConfig ntpRetry;
RetryConfig weatherRetry;
WiFiRetryConfig wifiRetry;

// NTP packet buffer and timing
byte ntpPacketBuffer[48];
unsigned long ntpRequestTime = 0;

// Independent epoch tracking
unsigned long syncedEpoch = 0;
unsigned long syncedMillis = 0;
bool timeIsSynced = false;

// WiFi connection timing
unsigned long wifiConnectStart = 0;

// Display state
bool colonBlink = false;
unsigned long lastBlinkTime = 0;
unsigned long lastNTPUpdate = 0;
unsigned long ipDisplayUntil = 0;

// Debug variables
String lastError = "";
int ntpAttempts = 0;
int ntpSuccesses = 0;
bool internetConnected = false;

// Weather and sun data
WeatherData weather;
SunTimes sunTimes;
FeatureSettings featureSettings;
ForecastData forecast;
ExternalCard externalCard;
bool displayPaused = false;

// Display rotation state
uint8_t displayMode = 0;
unsigned long lastModeSwitch = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long weatherRequestStart = 0;  // Tracks when WEATHER_REQUESTING began (TCP hang watchdog)

// Dissolve transition state
bool inTransition = false;
unsigned long transitionStart = 0;
unsigned long lastDissolveFrame = 0;
uint8_t nextDisplayMode = 0;

// ============ Helper functions ============

void ICACHE_FLASH_ATTR safeStringCopy(const String& src, char* dest, size_t maxLen) {
  if (src.length() >= maxLen) {
    Serial.printf("WARNING: String truncated from %d to %d chars\n", src.length(), maxLen - 1);
  }
  src.toCharArray(dest, maxLen);
  dest[maxLen - 1] = '\0';
}

// ============ Triple power-cycle factory reset ============
//
// How it works: on each boot we increment a counter in EEPROM.
// If the device runs for >10s the counter is cleared back to 0.
// 3 quick power cycles before the 10s window = factory reset:
//   clears WiFi credentials, reboots into WiFiManager AP mode.

void ICACHE_FLASH_ATTR checkFactoryReset() {
  EEPROM.begin(512);
  ResetCounter rc;
  EEPROM.get(RESET_COUNTER_ADDR, rc);

  if (rc.magic != RESET_COUNTER_MAGIC) {
    rc.magic = RESET_COUNTER_MAGIC;
    rc.count = 0;
  }

  rc.count++;
  Serial.printf("Boot counter: %d/%d (power-cycle %d more times within 10s to factory reset)\n",
                rc.count, RESET_COUNTER_TRIPS, RESET_COUNTER_TRIPS - rc.count);

  if (rc.count >= RESET_COUNTER_TRIPS) {
    Serial.println("!!! FACTORY RESET triggered !!!");

    // Atomicity: clear credentials FIRST (saveConfig commits), then zero counter.
    // If power fails between the two commits, the device boots into AP mode next
    // time (creds are already cleared) — instead of being in a "counter zeroed but
    // creds still valid" inconsistent state.
    memset(config.ssid, 0, sizeof(config.ssid));
    memset(config.password, 0, sizeof(config.password));
    EEPROM.end();          // close current handle before saveConfig opens its own
    saveConfig();          // commits cleared credentials to flash
    WiFi.persistent(true);
    WiFi.disconnect(true);
    WiFi.persistent(false);

    EEPROM.begin(512);
    rc.count = 0;
    EEPROM.put(RESET_COUNTER_ADDR, rc);
    EEPROM.commit();
    EEPROM.end();

    // Show reset screen
    showSetupScreen(true);

    delay(4000);
    ESP.restart();
    return;
  }

  EEPROM.put(RESET_COUNTER_ADDR, rc);
  EEPROM.commit();
  EEPROM.end();
}

// ============ EEPROM functions ============

void ICACHE_FLASH_ATTR loadConfig() {
  EEPROM.begin(512);

  Config tempConfig;
  EEPROM.get(0, tempConfig);

  if (tempConfig.magic == CONFIG_MAGIC) {
    config = tempConfig;
    Serial.println("Valid configuration loaded from EEPROM");
  } else {
    Serial.println("Invalid EEPROM data, using defaults");
    config.magic = CONFIG_MAGIC;
    saveConfig();
  }

  EEPROM.end();

  Serial.println("Configuration:");
  Serial.printf("  Magic: 0x%08X %s\n", config.magic, config.magic == CONFIG_MAGIC ? "OK" : "INVALID");
  Serial.printf("  SSID: %s\n", config.ssid);
  Serial.printf("  Timezone: %ld\n", config.timezone_offset);
  Serial.printf("  Hostname: %s\n", config.hostname);
}

void ICACHE_FLASH_ATTR saveConfig() {
  EEPROM.begin(512);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();

  Serial.println("Configuration saved!");
}

// ============ OTA setup ============

void ICACHE_FLASH_ATTR setupOTA() {
  if (!maintenancePassword()) return;
  ArduinoOTA.setHostname(config.hostname);
  ArduinoOTA.setPassword(maintenancePassword());

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start OTA updating " + type);
    clearDisplay();
    showUpdateProgress(0);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete!");
    showUpdateProgress(100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = total ? (uint64_t(progress) * 100 / total) : 0;
    Serial.printf("Progress: %u%%\r", percent);
    showUpdateProgress(percent);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

// ============ Setup ============

void setup() {
  Serial.begin(115200);
  delay(100);

  // Seed PRNG with hardware entropy: chip ID is unique per device,
  // micros() varies on each boot due to power-on timing jitter
  randomSeed(ESP.getChipId() ^ micros());

  Serial.println("\n\nTJ-56-654 NTP Clock with OTA v" FIRMWARE_VERSION);
  Serial.println("==========================================");
  Serial.println("Display: GM009605v4.3 OLED 128x64 (SSD1306 I2C)");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize OLED display
  Serial.print("Initializing OLED at 0x");
  Serial.println(OLED_ADDRESS, HEX);

  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization FAILED!");
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED not found at 0x3C or 0x3D!");
    } else {
      Serial.println("OLED found at 0x3D");
    }
  } else {
    Serial.println("OLED initialized successfully!");
  }

  // Apply persisted orientation/brightness before the first rendered frame.
  loadConfig();
  loadNightSettings();
  loadFeatureSettings();
  applyDisplaySettings();
  showStartupAnimation();

  // Check for triple power-cycle factory reset (must be after display+config init)
  checkFactoryReset();

  // Setup WiFi
  setupWiFi();

  // Maintenance code is independent of WiFi credentials and never exported.
  setupMaintenance();

  // Setup OTA
  setupOTA();

  // Setup web server
  setupWebServer();

  // Setup NTP
  ntpUDP.begin(2390);

  // Initialize timing to prevent immediate flicker/transition
  lastBlinkTime = millis();
  lastModeSwitch = millis();
  colonBlink = true;

  // Start normal display immediately; the PIN is shown only on user request.
  ipDisplayUntil = 0;
  invalidateDisplay();
  Serial.println("Setup complete!");
}

// ============ Loop ============

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle web server
  server.handleClient();
  MDNS.update();

  // WiFi reconnection logic
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 1000) {
    lastWiFiCheck = millis();

    if (WiFi.status() != WL_CONNECTED && wifiConnState == WIFI_CONN_CONNECTED) {
      Serial.println("WiFi disconnected!");
      wifiConnState = WIFI_CONN_FAILED;
      internetConnected = false;
      invalidateDisplay();
      wifiRetry.reset();
      wifiRetry.scheduleRetry();
    }

    if (wifiConnState == WIFI_CONN_FAILED && wifiRetry.isRetryTime()) {
      Serial.printf("WiFi retry attempt (backoff level %d)...\n", wifiRetry.currentRetry);

      if (wifiRetry.currentRetry >= 5 && WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("Enabling fallback AP (dual mode)");
        // Must set mode BEFORE WiFi.begin() — begin() resets mode to STA killing the AP
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("TJ56654-Setup", "12345678");
        Serial.print("Fallback AP IP: ");
        Serial.println(WiFi.softAPIP());
      }

      // Reconnect STA side without changing mode (preserves AP_STA if active)
      if (WiFi.getMode() == WIFI_AP_STA) {
        // Use low-level reconnect to keep AP alive
        WiFi.disconnect(false);
        if (strlen(config.password) > 0) {
          WiFi.begin(config.ssid, config.password);
        } else {
          WiFi.begin(config.ssid);
        }
        WiFi.mode(WIFI_AP_STA);  // Restore AP_STA after begin() may have reset it
      } else {
        if (strlen(config.password) > 0) {
          WiFi.begin(config.ssid, config.password);
        } else {
          WiFi.begin(config.ssid);
        }
      }
      wifiConnState = WIFI_CONN_CONNECTING;
      wifiConnectStart = millis();
    }
  }

  // Process async WiFi reconnection
  processWiFiConnection();

  // Independent non-blocking network state machines.
  processNTPResponse();
  processWeather();

  // Clear factory-reset boot counter after 10s of normal operation
  static bool resetCounterCleared = false;
  if (!resetCounterCleared && millis() > RESET_COUNTER_WINDOW) {
    EEPROM.begin(512);
    ResetCounter rc = { RESET_COUNTER_MAGIC, 0 };
    EEPROM.put(RESET_COUNTER_ADDR, rc);
    EEPROM.commit();
    EEPROM.end();
    resetCounterCleared = true;
    Serial.println("Boot counter cleared — stable operation confirmed");
  }

  // Blink colon every second
  if (millis() - lastBlinkTime > 500) {
    colonBlink = !colonBlink;
    lastBlinkTime = millis();
  }
  updateDisplayRotation();
}
