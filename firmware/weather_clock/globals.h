/*
 * globals.h - Global variables and extern declarations
 * TJ-56-654 Weather Clock
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// Configuration
extern Config config;
extern NightSettings nightSettings;

// OLED Display
extern Adafruit_SSD1306 display;

// NTP Client
extern WiFiUDP ntpUDP;

// Web server
extern ESP8266WebServer server;
extern ESP8266HTTPUpdateServer httpUpdater;

// State machines — volatile: written from ESPAsyncTCP callbacks, read in main loop
extern volatile WeatherState weatherState;
extern volatile NTPState ntpState;
extern WiFiConnectionState wifiConnState;

// Retry configurations
extern RetryConfig ntpRetry;
extern RetryConfig weatherRetry;
extern WiFiRetryConfig wifiRetry;

// NTP packet buffer and timing
extern byte ntpPacketBuffer[48];
extern unsigned long ntpRequestTime;

// Independent epoch tracking for async NTP
extern unsigned long syncedEpoch;
extern unsigned long syncedMillis;
extern bool timeIsSynced;

// WiFi connection timing
extern unsigned long wifiConnectStart;

// Display state
extern bool colonBlink;
extern unsigned long lastBlinkTime;
extern unsigned long lastNTPUpdate;
extern unsigned long ipDisplayUntil;

// Debug variables
extern String lastError;
extern int ntpAttempts;
extern int ntpSuccesses;
extern bool internetConnected;

// Weather and sun data
extern WeatherData weather;
extern SunTimes sunTimes;

// Display rotation state
extern uint8_t displayMode;
extern unsigned long lastModeSwitch;
extern unsigned long lastWeatherUpdate;
extern unsigned long weatherRequestStart;

// Dissolve transition state
extern bool inTransition;
extern unsigned long transitionStart;
extern unsigned long lastDissolveFrame;
extern uint8_t nextDisplayMode;

// ============ Function declarations ============

// Helper functions
void ICACHE_FLASH_ATTR safeStringCopy(const String& src, char* dest, size_t maxLen);

// Time functions (ntp_client.cpp)
bool isDST(unsigned long epochTime);
long getTotalOffset(unsigned long epochTime);
unsigned long getAsyncEpoch();
void sendNTPRequestAsync();
void processNTPResponse();
void formatClockTime(char* out, size_t size, bool local);

// WiFi functions (wifi_manager.cpp)
void ICACHE_FLASH_ATTR setupWiFi();
void processWiFiConnection();

// Display functions (display.cpp)
void ICACHE_FLASH_ATTR clearDisplay();
void ICACHE_FLASH_ATTR showNumber(int num, bool leadingZeros);
void ICACHE_FLASH_ATTR showNoWiFi(unsigned long nextRetrySeconds);
void ICACHE_FLASH_ATTR showStartupAnimation();
void ICACHE_FLASH_ATTR showWiFiConnecting(int step);
void ICACHE_FLASH_ATTR showConnected();
void ICACHE_FLASH_ATTR showIP();
void updateDisplay();
void invalidateDisplay();
void applyDisplaySettings();
void wakeDisplay();
bool isNightModeActive();
void ICACHE_FLASH_ATTR displayWeather();
void ICACHE_FLASH_ATTR displaySunTimes();
void ICACHE_FLASH_ATTR applyDissolveEffect(uint8_t hidePercent, bool withDrift);
void ICACHE_FLASH_ATTR updateDisplayRotation();
bool ICACHE_FLASH_ATTR isModeEnabled(uint8_t mode);

// Weather functions (weather.cpp)
void fetchWeatherAsync();
void processWeather();
void resetWeather();

// Web server functions (web_server.cpp)
void ICACHE_FLASH_ATTR setupWebServer();
void setupWebUpdate();
void serveWebUI();
void ICACHE_FLASH_ATTR handleRoot();
void ICACHE_FLASH_ATTR handleDebug();
void ICACHE_FLASH_ATTR handleTestNTP();
void ICACHE_FLASH_ATTR handleTestDisplay();
void ICACHE_FLASH_ATTR handleConfig();
void ICACHE_FLASH_ATTR handleConfigSave();
void ICACHE_FLASH_ATTR handleAPITime();
void ICACHE_FLASH_ATTR handleAPIStatus();
void ICACHE_FLASH_ATTR handleAPIDebug();
void ICACHE_FLASH_ATTR handleAPIWeather();
void ICACHE_FLASH_ATTR handleAPIConfigExport();
void ICACHE_FLASH_ATTR handleAPIConfigImport();
void ICACHE_FLASH_ATTR handleEEPROMClear();
void ICACHE_FLASH_ATTR handleReboot();
void ICACHE_FLASH_ATTR handleI2CScan();

// Config functions (in main .ino)
void ICACHE_FLASH_ATTR loadConfig();
void ICACHE_FLASH_ATTR saveConfig();

bool setupMaintenance();
const char* maintenancePassword();
const char* authenticatedMaintenancePassword();
bool requireMaintenanceAuth();
void showMaintenanceCode();
void handleShowMaintenancePIN();
bool maintenanceCodeVisible();

// OTA functions (in main .ino)
void ICACHE_FLASH_ATTR setupOTA();

#endif // GLOBALS_H
