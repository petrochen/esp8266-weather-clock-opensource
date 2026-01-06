/*
 * TJ-56-654 Weather Clock - Custom NTP Firmware with OTA v1.9.2
 *
 * Version 1.9.2 Changes (WIFI RESILIENCE):
 * - FIXED: No longer clears WiFi credentials on connection failure
 * - IMPROVED: Infinite retry with exponential backoff (up to 5 min between attempts)
 * - IMPROVED: Shows "No WiFi" status on display instead of cryptic numbers
 * - IMPROVED: AP mode only on first boot (empty credentials) or manual trigger
 * - IMPROVED: Clock continues running with last synced time during WiFi outage
 *
 * Version 1.9.1 Changes (HYBRID ASYNC FIX):
 * - FIXED STARTUP: WiFi now synchronous in setup() for proper initialization
 * - FIXED DNS: testInternetConnectivity() now runs AFTER WiFi is connected
 * - HYBRID MODEL: Sync WiFi in setup(), async reconnect in loop()
 * - RESULT: No more "DNS resolution failed" on startup, proper init order
 *
 * Version 1.9 Changes (ASYNC PERFORMANCE):
 * - ASYNC HTTP: Non-blocking weather API calls (was 1-10 sec freeze → 0ms)
 * - ASYNC NTP: Non-blocking time sync (was 5-20 sec freeze → 0ms)
 * - ASYNC WiFi RECONNECT: Non-blocking reconnection in loop()
 * - REMOVED DELAYS: All blocking delay() calls from loop() eliminated
 * - RETRY LOGIC: Exponential backoff for failed network operations
 * - RESULT: Device always responsive during operation
 *
 * Version 1.8 Changes (CRITICAL STABILITY FIXES):
 * - IRAM CRISIS FIX: Added ICACHE_FLASH_ATTR to 26 functions
 * - SECURITY FIX: WiFiManager integration (removed hardcoded credentials)
 * - BUG FIXES: NTP interval config, boolean parsing, infinite loop protection
 * - MEMORY FIX: Chunked HTTP responses, reduced String concatenation
 * - SAFETY: Input validation, buffer overflow prevention
 *
 * Version 1.7 Changes:
 * - FINALLY FIXED: Display is 0.96" OLED 128x64 with SSD1306/SH1106!
 * - GM009605v4.3 module (not TM1650, not TM1637)
 * - Using Adafruit_SSD1306 library
 * - Graphical display with large time digits
 *
 * Hardware:
 * - ESP-01S (ESP8266)
 * - GM009605v4.3 OLED 128x64 display (SSD1306 I2C)
 *
 * Connections:
 * - GPIO0 → OLED SDA (I2C Data) - SWAPPED!
 * - GPIO2 → OLED SCL (I2C Clock) - SWAPPED!
 *
 * OLED I2C Address: 0x3C
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>  // WiFiManager for captive portal configuration

// Async libraries for non-blocking operations
#include <ESPAsyncTCP.h>  // Async TCP for ESP8266
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN_TARGET      "AsyncHTTPRequest_Generic v1.13.0"
#define ASYNCHTTPREQUEST_GENERIC_VERSION_MIN             1013000
#include <AsyncHTTPRequest_Generic.h>  // Async HTTP requests

// OLED I2C Configuration - TRY SWAPPED!
#define I2C_SDA 0  // GPIO0 (I2C Data) - SWAPPED!
#define I2C_SCL 2  // GPIO2 (I2C Clock) - SWAPPED!
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // No reset pin
#define OLED_ADDRESS 0x3C

// Configuration structure with validation
#define CONFIG_MAGIC 0xC10CC10C  // Magic number to validate EEPROM data
#define FIRMWARE_VERSION "1.9.2"

struct Config {
  uint32_t magic = CONFIG_MAGIC;  // Magic number for validation
  char ssid[32] = "";  // Empty - configured via WiFiManager captive portal
  char password[64] = "";  // Empty - configured via WiFiManager captive portal
  long timezone_offset = 0; // Base UTC offset in seconds (0=Lisbon/London, 3600=Paris/Berlin)
  bool dst_enabled = true;  // Auto DST: +1 hour during summer (European rules: last Sun Mar-Oct)
  int brightness = 4; // 0-7
  char ntp_server[64] = "pool.ntp.org";
  unsigned long ntp_interval = 3600; // NTP update interval in seconds (default: 1 hour)
  bool hour_format_24 = true; // true=24h, false=12h
  char hostname[32] = "tj56654-clock";

  // Weather settings
  float latitude = 37.19;   // Portimao, Portugal
  float longitude = -8.54;
  char city_name[32] = "Portimao";
  bool weather_enabled = true;
  unsigned long weather_interval = 1800; // 30 minutes in seconds

  // Display settings
  uint8_t display_rotation_sec = 5;  // Seconds per screen
  bool show_weather = true;
  bool show_sunrise_sunset = true;
  uint8_t display_orientation = 2;  // 0=0°, 1=90°, 2=180°, 3=270°
};

Config config;

// OLED Display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Forward declarations
class NTPClient;
extern NTPClient timeClient;
void updateDisplayRotation();
void fetchWeatherAsync();
void onWeatherResponse(void* optParm, AsyncHTTPRequest* request, int readyState);
void sendNTPRequestAsync();
void processNTPResponse();
void processWiFiConnection();
void calculateSunTimes();
bool ICACHE_FLASH_ATTR isModeEnabled(uint8_t mode);

// Forward declarations for ICACHE_FLASH_ATTR functions
void ICACHE_FLASH_ATTR testInternetConnectivity();
void ICACHE_FLASH_ATTR updateNTPTime();
void ICACHE_FLASH_ATTR setupWiFi();
void ICACHE_FLASH_ATTR setupOTA();
void ICACHE_FLASH_ATTR setupWebServer();
void ICACHE_FLASH_ATTR clearDisplay();
void ICACHE_FLASH_ATTR showNumber(int num, bool leadingZeros);
void ICACHE_FLASH_ATTR showNoWiFi(unsigned long nextRetrySeconds);
void ICACHE_FLASH_ATTR showStartupAnimation();
void ICACHE_FLASH_ATTR showIP();
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
void ICACHE_FLASH_ATTR loadConfig();
void ICACHE_FLASH_ATTR saveConfig();

// DST calculation for European rules
// DST starts: last Sunday of March at 01:00 UTC
// DST ends: last Sunday of October at 01:00 UTC
bool isDST(unsigned long epochTime) {
  if (!config.dst_enabled) return false;

  time_t t = epochTime;
  struct tm *timeinfo = gmtime(&t);

  int month = timeinfo->tm_mon + 1; // 1-12
  int day = timeinfo->tm_mday;      // 1-31
  int weekday = timeinfo->tm_wday;  // 0=Sunday
  int hour = timeinfo->tm_hour;

  // Not DST: November - February
  if (month < 3 || month > 10) return false;

  // Always DST: April - September
  if (month > 3 && month < 10) return true;

  // March: DST starts last Sunday at 01:00 UTC
  if (month == 3) {
    // Find last Sunday of March
    int lastSunday = 31 - ((5 + timeinfo->tm_year) % 7);
    if (day < lastSunday) return false;
    if (day > lastSunday) return true;
    if (hour < 1) return false;
    return true;
  }

  // October: DST ends last Sunday at 01:00 UTC
  if (month == 10) {
    // Find last Sunday of October
    int lastSunday = 31 - ((1 + timeinfo->tm_year) % 7);
    if (day < lastSunday) return true;
    if (day > lastSunday) return false;
    if (hour < 1) return true;
    return false;
  }

  return false;
}

// Get total timezone offset including DST (pass epochTime to avoid dependency)
long getTotalOffset(unsigned long epochTime) {
  long offset = config.timezone_offset;
  if (isDST(epochTime)) {
    offset += 3600; // Add 1 hour for DST
  }
  return offset;
}

// NTP Client - will be reinitialized in setup() with config values
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Exponential backoff retry configuration
struct RetryConfig {
  uint8_t maxRetries = 3;          // For NTP/Weather: give up after 3 tries
  uint8_t currentRetry = 0;
  unsigned long nextRetryTime = 0;
  unsigned long maxBackoffMs = 8000;  // Max backoff 8 seconds for NTP/Weather

  unsigned long getBackoffDelay() {
    unsigned long delay = 1000UL * (1UL << currentRetry);  // 1s, 2s, 4s, 8s...
    return (delay > maxBackoffMs) ? maxBackoffMs : delay;
  }

  void scheduleRetry() {
    if (currentRetry < maxRetries) {
      nextRetryTime = millis() + getBackoffDelay();
      currentRetry++;
    } else {
      nextRetryTime = 0;  // Max retries reached, stop
    }
  }

  bool isRetryTime() {
    return nextRetryTime > 0 && millis() >= nextRetryTime;
  }

  void reset() {
    currentRetry = 0;
    nextRetryTime = 0;
  }

  bool maxRetriesReached() {
    return currentRetry >= maxRetries;
  }
};

// WiFi retry configuration - infinite retries with longer backoff
struct WiFiRetryConfig {
  uint8_t currentRetry = 0;
  unsigned long nextRetryTime = 0;
  static const unsigned long MAX_BACKOFF_MS = 300000;  // Max 5 minutes between retries

  unsigned long getBackoffDelay() {
    // 5s, 10s, 20s, 40s, 80s, 160s, 300s (max)
    unsigned long delay = 5000UL * (1UL << currentRetry);
    return (delay > MAX_BACKOFF_MS) ? MAX_BACKOFF_MS : delay;
  }

  void scheduleRetry() {
    nextRetryTime = millis() + getBackoffDelay();
    if (currentRetry < 10) currentRetry++;  // Cap at 10 to prevent overflow
  }

  bool isRetryTime() {
    return nextRetryTime > 0 && millis() >= nextRetryTime;
  }

  void reset() {
    currentRetry = 0;
    nextRetryTime = 0;
  }
};

// Async HTTP client for non-blocking weather fetch
AsyncHTTPRequest weatherRequest;

// Weather fetch state machine
enum WeatherState {
  WEATHER_IDLE,
  WEATHER_REQUESTING,
  WEATHER_SUCCESS,
  WEATHER_FAILED
};
WeatherState weatherState = WEATHER_IDLE;

// Async NTP state machine
enum NTPState {
  NTP_IDLE,
  NTP_REQUEST_SENT,
  NTP_WAITING,
  NTP_SUCCESS,
  NTP_FAILED
};
NTPState ntpState = NTP_IDLE;

// NTP packet buffer and timing
byte ntpPacketBuffer[48];
unsigned long ntpRequestTime = 0;
const unsigned long NTP_TIMEOUT_MS = 5000;  // 5 second timeout

// Independent epoch tracking for async NTP
unsigned long syncedEpoch = 0;
unsigned long syncedMillis = 0;
bool timeIsSynced = false;

// Async WiFi state machine
enum WiFiConnectionState {
  WIFI_CONN_IDLE,
  WIFI_CONN_CONNECTING,
  WIFI_CONN_CONNECTED,
  WIFI_CONN_FAILED,
  WIFI_CONN_SKIP_ASYNC  // Skip async, go straight to WiFiManager
};
WiFiConnectionState wifiConnState = WIFI_CONN_IDLE;
unsigned long wifiConnectStart = 0;
const unsigned long WIFI_TIMEOUT_MS = 10000;  // 10 second timeout for v1.7 migration

// Retry configurations with exponential backoff
RetryConfig ntpRetry;
RetryConfig weatherRetry;
WiFiRetryConfig wifiRetry;

// Web server
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// State variables
bool colonBlink = false;
unsigned long lastBlinkTime = 0;
unsigned long lastNTPUpdate = 0;
unsigned long ipDisplayUntil = 0;  // Non-blocking IP display timer
// NTP_UPDATE_INTERVAL removed - now using config.ntp_interval dynamically

// Debug variables
String lastError = "";
int ntpAttempts = 0;
int ntpSuccesses = 0;
bool internetConnected = false;

// Weather data cache
struct WeatherData {
  float temperature = 0.0;
  int weathercode = -1;  // WMO weather code
  int humidity = 0;
  float windspeed = 0.0;
  unsigned long lastUpdate = 0;
  bool valid = false;
};
WeatherData weather;

// Sunrise/Sunset cache
struct SunTimes {
  int sunriseMinutes = 0;  // Minutes since midnight
  int sunsetMinutes = 0;
  int lastDay = -1;        // Day of year
  char sunrise[6] = "--:--";  // HH:MM format
  char sunset[6] = "--:--";
};
SunTimes sunTimes;

// Display rotation state
uint8_t displayMode = 0;  // 0=time, 1=weather, 2=sunrise/sunset
unsigned long lastModeSwitch = 0;
unsigned long lastWeatherUpdate = 0;

// Helper function: Safe string copy with truncation warning
void ICACHE_FLASH_ATTR safeStringCopy(const String& src, char* dest, size_t maxLen) {
  if (src.length() >= maxLen) {
    Serial.printf("WARNING: String truncated from %d to %d chars\n", src.length(), maxLen - 1);
  }
  src.toCharArray(dest, maxLen);
  dest[maxLen - 1] = '\0';  // Ensure null termination
}

// Setup function
void setup() {
  Serial.begin(115200);
  delay(100);  // Wait for serial
  Serial.println("\n\nTJ-56-654 NTP Clock with OTA v" FIRMWARE_VERSION);
  Serial.println("==========================================");
  Serial.println("Display: GM009605v4.3 OLED 128x64 (SSD1306 I2C)");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize OLED display
  Serial.print("Initializing OLED at 0x");
  Serial.println(OLED_ADDRESS, HEX);

  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("✗ OLED initialization FAILED!");
    // Try alternate address 0x3D
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("✗ OLED not found at 0x3C or 0x3D!");
    } else {
      Serial.println("✓ OLED found at 0x3D");
    }
  } else {
    Serial.println("✓ OLED initialized successfully!");
  }

  // Set display rotation from config (0=0°, 1=90°, 2=180°, 3=270°)
  display.setRotation(config.display_orientation);
  Serial.printf("Display rotation: %d (180°)\n", config.display_orientation);

  // Show startup animation
  Serial.println("Showing startup animation...");
  showStartupAnimation();

  // Load configuration from EEPROM
  loadConfig();

  // Setup WiFi
  setupWiFi();

  // Setup OTA
  setupOTA();

  // Setup web server
  setupWebServer();

  // Setup NTP with config parameters
  // Note: NTP server and interval from config
  // Reinitialize timeClient with config values
  timeClient = NTPClient(ntpUDP, config.ntp_server, 0, config.ntp_interval * 1000);
  timeClient.begin();

  // Test internet connectivity
  testInternetConnectivity();

  Serial.println("Setup complete!");
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle web server
  server.handleClient();
  MDNS.update();

  // WiFi reconnection logic (async, non-blocking with exponential backoff)
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 1000) {  // Check every second
    lastWiFiCheck = millis();

    // If WiFi was connected but now disconnected
    if (WiFi.status() != WL_CONNECTED && wifiConnState == WIFI_CONN_CONNECTED) {
      Serial.println("⚠️ WiFi disconnected!");
      wifiConnState = WIFI_CONN_FAILED;
      internetConnected = false;
      wifiRetry.reset();  // Start fresh retry sequence
      wifiRetry.scheduleRetry();  // Schedule first retry
    }

    // If it's time to retry
    if (wifiConnState == WIFI_CONN_FAILED && wifiRetry.isRetryTime()) {
      Serial.printf("🔄 WiFi retry attempt (backoff level %d)...\n", wifiRetry.currentRetry);

      // After 5+ failed attempts (~5 min), enable fallback AP in dual mode
      if (wifiRetry.currentRetry >= 5 && WiFi.getMode() != WIFI_AP_STA) {
        Serial.println("📡 Enabling fallback AP (dual mode) for manual configuration");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("TJ56654-Setup", "12345678");
        Serial.print("Fallback AP IP: ");
        Serial.println(WiFi.softAPIP());
      }

      // Try SDK-stored credentials first, then EEPROM
      if (strlen(config.password) > 0) {
        WiFi.begin(config.ssid, config.password);
      } else {
        WiFi.begin();  // Use SDK-stored credentials from WiFiManager
      }
      wifiConnState = WIFI_CONN_CONNECTING;
      wifiConnectStart = millis();
    }
  }

  // Process async WiFi reconnection
  processWiFiConnection();

  // Process async NTP response (non-blocking check)
  processNTPResponse();

  // Check for NTP retry (exponential backoff)
  if (ntpRetry.isRetryTime() && ntpState == NTP_IDLE) {
    Serial.println("⏰ NTP retry time reached, attempting retry...");
    sendNTPRequestAsync();
  }

  // Trigger async NTP update periodically (using config.ntp_interval in seconds)
  unsigned long ntpInterval = config.ntp_interval * 1000UL; // Convert seconds to milliseconds
  if (millis() - lastNTPUpdate > ntpInterval || lastNTPUpdate == 0) {
    if (ntpState == NTP_IDLE && !ntpRetry.isRetryTime()) {  // Only if not already in progress or waiting for retry
      sendNTPRequestAsync();  // Non-blocking!
      lastNTPUpdate = millis();
    }

    // Also recalculate sun times when time is synced
    if (timeIsSynced || timeClient.isTimeSet()) {
      calculateSunTimes();
    }
  }

  // Check for weather retry (exponential backoff)
  if (weatherRetry.isRetryTime() && weatherState == WEATHER_IDLE) {
    Serial.println("⏰ Weather retry time reached, attempting retry...");
    fetchWeatherAsync();
  }

  // Update weather periodically (but wait at least 10 seconds after boot)
  // Async weather fetch - non-blocking!
  if (config.weather_enabled && millis() > 10000) {
    unsigned long weatherInterval = config.weather_interval * 1000UL;
    if (millis() - lastWeatherUpdate > weatherInterval || lastWeatherUpdate == 0) {
      if (timeClient.isTimeSet() && weatherState == WEATHER_IDLE && !weatherRetry.isRetryTime()) {
        fetchWeatherAsync();  // Non-blocking! Also updates sunrise/sunset from API
        lastWeatherUpdate = millis();
      }
    }
  }

  // Check if IP display should be cleared (non-blocking timer)
  if (ipDisplayUntil > 0 && millis() >= ipDisplayUntil) {
    clearDisplay();
    ipDisplayUntil = 0;  // Reset timer
  }

  // Update display with rotation (Time → Weather → Sunrise/Sunset)
  updateDisplayRotation();

  // Blink colon every second
  if (millis() - lastBlinkTime > 500) {
    colonBlink = !colonBlink;
    lastBlinkTime = millis();
  }

  // No delay needed - loop() runs as fast as possible for responsiveness
}

// Test internet connectivity
void ICACHE_FLASH_ATTR testInternetConnectivity() {
  Serial.println("\n=== Testing Internet Connectivity ===");

  // Test DNS resolution
  IPAddress ntpIP;
  if (WiFi.hostByName(config.ntp_server, ntpIP)) {
    Serial.print("✓ DNS works: ");
    Serial.print(config.ntp_server);
    Serial.print(" → ");
    Serial.println(ntpIP);
  } else {
    Serial.print("✗ DNS failed: cannot resolve ");
    Serial.println(config.ntp_server);
    lastError = "DNS resolution failed";
    return;
  }

  // Test ping to Google DNS
  if (WiFi.hostByName("google.com", ntpIP)) {
    Serial.println("✓ Can resolve google.com");
    internetConnected = true;
  } else {
    Serial.println("✗ Cannot resolve google.com - no internet?");
    lastError = "No internet connectivity";
    internetConnected = false;
  }
}

// Update NTP time with better error handling
void ICACHE_FLASH_ATTR updateNTPTime() {
  Serial.println("\n=== Updating NTP Time ===");
  ntpAttempts++;

  if (!internetConnected) {
    Serial.println("✗ Skipping NTP update - no internet");
    lastError = "No internet connection";
    return;
  }

  bool success = timeClient.update();

  if (success) {
    ntpSuccesses++;
    Serial.print("✓ NTP sync successful: ");
    Serial.println(timeClient.getFormattedTime());
    lastError = "";
  } else {
    Serial.println("✗ NTP sync failed");
    lastError = "NTP sync failed (timeout or no response)";

    // Try force update
    Serial.println("  Trying force update...");
    if (timeClient.forceUpdate()) {
      ntpSuccesses++;
      Serial.print("✓ Force update successful: ");
      Serial.println(timeClient.getFormattedTime());
      lastError = "";
    } else {
      Serial.println("✗ Force update also failed");

      // Test connectivity again
      testInternetConnectivity();
    }
  }

  Serial.print("NTP Stats: ");
  Serial.print(ntpSuccesses);
  Serial.print(" / ");
  Serial.print(ntpAttempts);
  Serial.println(" successful");
}

// Get current epoch (async NTP independent tracking)
unsigned long getAsyncEpoch() {
  if (!timeIsSynced) return timeClient.getEpochTime();
  unsigned long elapsed = (millis() - syncedMillis) / 1000;
  return syncedEpoch + elapsed;
}

// Async NTP - Send request (non-blocking)
void sendNTPRequestAsync() {
  if (ntpState != NTP_IDLE) return;

  if (!internetConnected) {
    Serial.println(F("✗ Skip NTP - no internet"));
    return;
  }

  Serial.println(F("⬇️ NTP request (async)..."));
  ntpAttempts++;

  memset(ntpPacketBuffer, 0, 48);
  ntpPacketBuffer[0] = 0b11100011;
  ntpPacketBuffer[1] = 0;
  ntpPacketBuffer[2] = 6;
  ntpPacketBuffer[3] = 0xEC;

  ntpUDP.beginPacket(config.ntp_server, 123);
  ntpUDP.write(ntpPacketBuffer, 48);
  ntpUDP.endPacket();

  ntpState = NTP_REQUEST_SENT;
  ntpRequestTime = millis();
  Serial.println("✓ NTP sent (non-blocking)");
}

// Async NTP - Process response (call in loop)
void processNTPResponse() {
  if (ntpState == NTP_IDLE) return;

  // Timeout check with exponential backoff retry
  if (millis() - ntpRequestTime > NTP_TIMEOUT_MS) {
    ntpState = NTP_IDLE;
    Serial.printf("✗ NTP timeout (attempt %d/%d)\n", ntpRetry.currentRetry + 1, ntpRetry.maxRetries);

    // Schedule retry with exponential backoff
    ntpRetry.scheduleRetry();
    if (ntpRetry.maxRetriesReached()) {
      Serial.println("✗ NTP max retries reached, will try again later");
      lastError = "NTP timeout - max retries";
    } else {
      unsigned long backoff = ntpRetry.getBackoffDelay() / 1000;
      Serial.printf("  Retry scheduled in %lu seconds\n", backoff);
    }
    return;
  }

  // Check for response packet
  if (ntpUDP.parsePacket() >= 48) {
    ntpUDP.read(ntpPacketBuffer, 48);

    unsigned long high = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
    unsigned long low = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
    unsigned long epoch = (high << 16 | low) - 2208988800UL;

    syncedEpoch = epoch;
    syncedMillis = millis();
    timeIsSynced = true;

    timeClient = NTPClient(ntpUDP, config.ntp_server, 0, config.ntp_interval * 1000);
    timeClient.begin();
    timeClient.update();

    ntpState = NTP_IDLE;
    ntpSuccesses++;
    ntpRetry.reset();  // Success! Reset retry counter
    lastError = "";

    unsigned long h = (epoch % 86400L) / 3600;
    unsigned long m = (epoch % 3600) / 60;
    Serial.printf("✓ NTP synced (async): %02lu:%02lu UTC\n", h, m);
  }
}

// Async WiFi - Process connection (call in loop)
void processWiFiConnection() {
  if (wifiConnState != WIFI_CONN_CONNECTING) return;

  // Check connection status
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnState = WIFI_CONN_CONNECTED;
    wifiRetry.reset();  // Success! Reset retry counter
    internetConnected = true;
    Serial.println("\n✅ WiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Disable fallback AP if it was enabled (switch back to STA only)
    if (WiFi.getMode() == WIFI_AP_STA) {
      Serial.println("📡 Disabling fallback AP (back to STA mode)");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }

    // Sync connected SSID to config for display purposes (don't clear on failure!)
    if (strlen(config.ssid) == 0) {
      safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
      saveConfig();
    }

    // Show IP on display
    showIP();
    return;
  }

  // Check timeout for this attempt
  if (millis() - wifiConnectStart > WIFI_TIMEOUT_MS) {
    // Connection attempt failed - schedule retry with backoff
    // DO NOT clear credentials!
    wifiRetry.scheduleRetry();
    unsigned long nextRetryMs = wifiRetry.getBackoffDelay();

    Serial.printf("\n⚠️ WiFi connection failed. Retry in %lu seconds\n", nextRetryMs / 1000);

    wifiConnState = WIFI_CONN_FAILED;
    internetConnected = false;

    // Show "No WiFi" status on display
    showNoWiFi(nextRetryMs / 1000);
    return;
  }

  // Still connecting, show progress dots in serial only (no display spam)
  static unsigned long lastDot = 0;
  if (millis() - lastDot > 500) {
    Serial.print(".");
    lastDot = millis();
  }
}


// WiFi setup (SYNCHRONOUS in setup(), async reconnect in loop())
void ICACHE_FLASH_ATTR setupWiFi() {
  Serial.println("WiFi Setup - Synchronous for initial connection");

  // Set hostname before connecting
  WiFi.hostname(config.hostname);
  WiFi.mode(WIFI_STA);

  // STRATEGY: First try SDK-stored credentials (from WiFiManager), then EEPROM config
  // WiFiManager stores credentials in ESP flash separately from our EEPROM

  // Try 1: Use WiFi.begin() without params - uses SDK stored credentials
  Serial.println("Trying SDK-stored credentials...");
  WiFi.begin();  // Uses credentials stored by WiFiManager/SDK

  // SYNCHRONOUS wait for connection (max 10 seconds)
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    showNumber(attempts, false);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());

    // Sync connected SSID to our config
    safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
    // Note: password stays in SDK storage, we don't have access to it
    saveConfig();

    // Show IP on display
    showIP();

    wifiConnState = WIFI_CONN_CONNECTED;
    return;  // Success!
  }

  // Try 2: If we have EEPROM credentials, try those
  if (strlen(config.ssid) > 0 && strlen(config.password) > 0) {
    Serial.println("\nTrying EEPROM credentials...");
    WiFi.begin(config.ssid, config.password);

    attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      showNumber(attempts, false);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi connected via EEPROM credentials!");
      showIP();
      wifiConnState = WIFI_CONN_CONNECTED;
      return;
    }
  }

  // Both attempts failed - check if we have ANY stored credentials
  // If not, use WiFiManager for initial setup
  if (strlen(config.ssid) == 0) {
    Serial.println("\nNo saved credentials, using WiFiManager...");
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);  // 3 minutes timeout

    // Display AP mode indication
    showNumber(0xAF, false);

    // Auto-connect: Portal SSID "TJ56654-Setup", Password "12345678"
    Serial.println("Attempting WiFiManager auto-connect...");
    if (!wifiManager.autoConnect("TJ56654-Setup", "12345678")) {
      // Connection failed after timeout
      Serial.println("WiFi connection failed. Starting fallback AP...");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("TJ56654-Clock", "12345678");
      Serial.print("Fallback AP IP: ");
      Serial.println(WiFi.softAPIP());
      wifiConnState = WIFI_CONN_CONNECTED;  // Mark as handled
      return;
    }

    // Connected successfully via WiFiManager!
    Serial.println("WiFi connected via WiFiManager!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Sync connected SSID to config for display purposes
    safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
    saveConfig();

    // Show IP on display
    showIP();
    wifiConnState = WIFI_CONN_CONNECTED;
    return;
  }

  // We have credentials but WiFi is not available - will retry in loop()
  Serial.println("\n⚠️ WiFi not available. Will retry in background.");
  wifiConnState = WIFI_CONN_FAILED;
  wifiRetry.scheduleRetry();
  showNoWiFi(wifiRetry.getBackoffDelay() / 1000);
}

// OTA setup
void ICACHE_FLASH_ATTR setupOTA() {
  ArduinoOTA.setHostname(config.hostname);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start OTA updating " + type);
    clearDisplay();
    showNumber(0, false);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete!");
    showNumber(100, false);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress / (total / 100));
    Serial.printf("Progress: %u%%\r", percent);
    showNumber(percent, false);
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

// Web server setup
void ICACHE_FLASH_ATTR setupWebServer() {
  // Setup web OTA updater at /update
  httpUpdater.setup(&server, "/update", "admin", "admin");

  // Root page
  server.on("/", HTTP_GET, handleRoot);

  // Config page
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfigSave);

  // Debug page
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/test-ntp", HTTP_GET, handleTestNTP);
  server.on("/test-display", HTTP_GET, handleTestDisplay);

  // API endpoints
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

  // Start mDNS
  if (MDNS.begin(config.hostname)) {
    Serial.printf("mDNS started: http://%s.local\n", config.hostname);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("arduino", "tcp", 8266);
  }
}

// Update OLED display with current time
// BLUE zone (top 48px): Large time
// YELLOW zone (bottom 16px): Date + WiFi status
void updateDisplay() {
  static unsigned long lastUpdate = 0;

  // Update display only every 500ms (not every loop cycle)
  if (millis() - lastUpdate < 500) return;
  lastUpdate = millis();

  display.clearDisplay();

  // Check if we have ANY time source (NTPClient or async sync)
  bool hasTime = timeClient.isTimeSet() || timeIsSynced;

  if (!hasTime) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println("--:--");

    // Show WiFi status in yellow zone
    display.setTextSize(1);
    display.setCursor(25, 52);
    if (wifiConnState == WIFI_CONN_CONNECTED) {
      display.print("Syncing NTP...");
    } else {
      display.print("No WiFi");
    }
    display.display();
    return;
  }

  // Get epoch from best available source
  unsigned long epochTime = timeIsSynced ? getAsyncEpoch() : timeClient.getEpochTime();
  unsigned long localTime = epochTime + getTotalOffset(epochTime);

  int hours = (localTime / 3600) % 24;
  int minutes = (localTime / 60) % 60;

  // Convert to 12h format if needed
  if (!config.hour_format_24) {
    if (hours == 0) hours = 12;
    else if (hours > 12) hours -= 12;
  }

  // === YELLOW ZONE (Y: 48-63): Date (size 2 = 16px height) ===
  display.setTextSize(2);
  time_t t = epochTime;
  struct tm *ptm = gmtime(&t);

  // Format: "Thu 02.01" or "! Thu 02.01" if no WiFi
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char dateStr[20];

  if (wifiConnState != WIFI_CONN_CONNECTED) {
    // Show "!" indicator when WiFi is down
    sprintf(dateStr, "!%s %02d.%02d", days[ptm->tm_wday], ptm->tm_mday, ptm->tm_mon + 1);
  } else {
    sprintf(dateStr, "%s %02d.%02d", days[ptm->tm_wday], ptm->tm_mday, ptm->tm_mon + 1);
  }

  // Center in yellow zone (Y: 48-63)
  int dateWidth = strlen(dateStr) * 12;  // Size 2 = ~12px per char
  int dateX = (128 - dateWidth) / 2;
  display.setCursor(dateX, 48);
  display.print(dateStr);

  // === BLUE ZONE (Y: 0-47): Large time (size 3 = 24px height) ===
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);

  // Calculate center position for HH:MM
  // Each char size 3 = 18px width
  display.setCursor(10, 12);  // Y=12 centers in blue zone (0-47)
  display.printf("%02d", hours);

  // Blinking colon
  if (colonBlink) {
    display.print(":");
  } else {
    display.print(" ");
  }

  display.printf("%02d", minutes);

  display.display();
}

// Weather display
// BLUE zone (top 48px): Large temperature
// YELLOW zone (bottom 16px): City name
void ICACHE_FLASH_ATTR displayWeather() {
  display.clearDisplay();

  if (!weather.valid) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println("No Data");
    display.display();
    return;
  }

  // === YELLOW ZONE (Y: 48-63): City name (size 2 = 16px) ===
  display.setTextSize(2);
  int cityWidth = strlen(config.city_name) * 12;
  int cityX = (128 - cityWidth) / 2;
  display.setCursor(cityX, 48);
  display.print(config.city_name);

  // === BLUE ZONE (Y: 0-47): Temperature (size 3 = 24px height) ===
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);

  // Format temperature value only (no degree symbol yet)
  char tempStr[16];
  sprintf(tempStr, "%.1f", weather.temperature);  // Just the number

  // Center temperature + degree symbol
  int tempValueWidth = strlen(tempStr) * 18;  // Size 3 = ~18px per char
  int degreeSymbolWidth = 6;  // Size 1 = 6px per char
  int totalWidth = tempValueWidth + degreeSymbolWidth + 6;  // +6 for small "c"
  int startX = (128 - totalWidth) / 2;

  // Print temperature value
  display.setCursor(startX, 12);  // Center in blue zone
  display.print(tempStr);

  // Print degree symbol and C (small, raised)
  display.setTextSize(1);
  int degreeX = startX + tempValueWidth;
  display.setCursor(degreeX, 12);  // Raised position (same Y as temp)
  display.print("\xF8" "c");  // °c (lowercase, smaller)

  display.display();
}

// Sunrise/Sunset display
// BLUE zone (top 48px): Times
// YELLOW zone (bottom 16px): Next event
void ICACHE_FLASH_ATTR displaySunTimes() {
  display.clearDisplay();

  if (sunTimes.lastDay == -1) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(30, 24);
    display.println("----");
    display.display();
    return;
  }

  // === YELLOW ZONE (Y: 48-63): Daylight duration ===
  // Calculate daylight duration
  int daylightMinutes = sunTimes.sunsetMinutes - sunTimes.sunriseMinutes;
  int daylightHours = daylightMinutes / 60;
  int daylightMins = daylightMinutes % 60;

  // Format: "Day 9h 41m" or "9h 41m"
  char daylightStr[16];
  sprintf(daylightStr, "Day %dh %dm", daylightHours, daylightMins);

  display.setTextSize(1);  // Size 1 to fit more text
  int textWidth = strlen(daylightStr) * 6;  // Size 1 = 6px per char
  int textX = (128 - textWidth) / 2;  // Center
  display.setCursor(textX, 52);  // Y=52 centers in yellow zone
  display.print(daylightStr);

  // === BLUE ZONE (Y: 0-47): Sunrise and Sunset times ===
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  // Line 1: Sunrise (arrow + space + time)
  display.setCursor(5, 4);
  display.print("\x18 ");  // Up arrow + SPACE
  display.print(sunTimes.sunrise);

  // Line 2: Sunset (arrow + space + time)
  display.setCursor(5, 28);
  display.print("\x19 ");  // Down arrow + SPACE
  display.print(sunTimes.sunset);

  // No labels needed - arrows are self-explanatory
  // ↑ = sunrise (sun going up)
  // ↓ = sunset (sun going down)

  display.display();
}

void ICACHE_FLASH_ATTR updateDisplayRotation() {
  unsigned long now = millis();
  unsigned long interval = config.display_rotation_sec * 1000UL;

  // Switch mode if interval elapsed
  if (now - lastModeSwitch > interval) {
    // Cycle through modes with protection against infinite loop
    uint8_t attempts = 0;
    do {
      displayMode = (displayMode + 1) % 3;
      attempts++;
      if (attempts >= 3) {
        // Safety: if no mode is enabled after 3 attempts, force time mode
        displayMode = 0;
        Serial.println("WARNING: No display mode enabled, forcing time mode");
        break;
      }
    } while (!isModeEnabled(displayMode));

    lastModeSwitch = now;
    Serial.printf("Display mode: %d\n", displayMode);
  }

  // Display based on current mode
  switch(displayMode) {
    case 0:
      updateDisplay();  // Show time
      break;
    case 1:
      displayWeather();  // Show weather
      break;
    case 2:
      displaySunTimes();  // Show sunrise/sunset
      break;
  }
}

bool ICACHE_FLASH_ATTR isModeEnabled(uint8_t mode) {
  switch(mode) {
    case 0:
      return true;  // Time always enabled
    case 1:
      return config.show_weather && weather.valid;
    case 2:
      return config.show_sunrise_sunset && sunTimes.lastDay != -1;
  }
  return false;
}

// Clear OLED display
void ICACHE_FLASH_ATTR clearDisplay() {
  display.clearDisplay();
  display.display();
}

// Show number on OLED (centered, large font)
void ICACHE_FLASH_ATTR showNumber(int num, bool leadingZeros) {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);

  // Center text
  display.setCursor(20, 20);

  if (leadingZeros) {
    display.printf("%04d", num);
  } else {
    display.print(num);
  }

  display.display();
}

// Show "No WiFi" status on OLED with retry countdown
void ICACHE_FLASH_ATTR showNoWiFi(unsigned long nextRetrySeconds) {
  display.clearDisplay();

  // === BLUE ZONE: "No WiFi" message ===
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 8);
  display.println("No WiFi");

  // === YELLOW ZONE: Retry info ===
  display.setTextSize(1);
  display.setCursor(10, 52);
  if (nextRetrySeconds < 60) {
    display.printf("Retry in %lu sec", nextRetrySeconds);
  } else {
    display.printf("Retry in %lu min", nextRetrySeconds / 60);
  }

  display.display();
}

// Dummy function for compatibility (not needed for OLED)
void displaySegments(const uint8_t segments[]) {
  // Not used with OLED - kept for compatibility
}

// Startup animation for OLED
void ICACHE_FLASH_ATTR showStartupAnimation() {
  Serial.println("  Animation: Show logo");

  // Show "TJ-56" text
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 10);
  display.println("TJ-56");
  display.setTextSize(1);
  display.setCursor(15, 35);
  display.println("Weather Clock");
  display.setCursor(30, 50);
  display.print("v");
  display.print(FIRMWARE_VERSION);
  display.display();
  delay(1000);

  // Blink
  display.clearDisplay();
  display.display();
  delay(200);

  // Show again
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(30, 20);
  display.println("READY");
  display.display();
  delay(500);

  clearDisplay();
  Serial.println("  Animation complete!");
}

// Show IP address on OLED (non-blocking)
void ICACHE_FLASH_ATTR showIP() {
  IPAddress ip = WiFi.localIP();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("IP: ");
  display.println(ip);
  display.display();

  // Schedule display clear after 3 seconds (non-blocking)
  ipDisplayUntil = millis() + 3000;
}

// PROGMEM templates for handleRoot() - chunked response
const char ROOT_HTML_HEADER[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<title>TJ-56-654 Clock v" FIRMWARE_VERSION "</title>"
  "<style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0;}"
  ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
  "h1{color:#333;}.time{font-size:48px;text-align:center;margin:20px 0;font-weight:bold;color:#0066cc;}"
  ".info{margin:10px 0;padding:10px;background:#f9f9f9;border-radius:5px;}"
  ".error{background:#ffebee;color:#c62828;}"
  "a.button{display:inline-block;background:#0066cc;color:white;padding:10px 20px;text-decoration:none;border-radius:5px;margin:10px 5px;}"
  "a.button:hover{background:#0052a3;}"
  ".debug{background:#fff3cd;color:#856404;padding:10px;border-radius:5px;margin:10px 0;}"
  "</style>"
  "<script>"
  "function updateTime(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('time').innerText=d.time;});}"
  "setInterval(updateTime,1000);updateTime();"
  "</script>"
  "</head><body>"
  "<div class='container'>"
  "<h1>TJ-56-654 NTP Clock v" FIRMWARE_VERSION "</h1>"
  "<div class='time' id='time'>--:--:--</div>";

const char ROOT_HTML_FOOTER[] PROGMEM =
  "<a href='/config' class='button'>Configuration</a>"
  "<a href='/debug' class='button'>Debug Info</a>"
  "<a href='/update' class='button'>Firmware Update</a>"
  "<a href='/api/status' class='button'>Status (JSON)</a>"
  "<button class='button' onclick=\"if(confirm('Reboot device?')) fetch('/api/reboot', {method:'POST'}).then(()=>alert('Rebooting...'))\">Reboot</button>"
  "</div></body></html>";

void ICACHE_FLASH_ATTR handleRoot() {
  char buf[150];

  // Start chunked transfer
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // Header
  server.sendContent_P(ROOT_HTML_HEADER);

  // Error message if present
  if (lastError != "") {
    snprintf_P(buf, sizeof(buf), PSTR("<div class='info error'><strong>⚠ Error:</strong> %s</div>"), lastError.c_str());
    server.sendContent(buf);
  }

  // WiFi info
  snprintf_P(buf, sizeof(buf), PSTR("<div class='info'><strong>WiFi:</strong> %s</div>"), WiFi.SSID().c_str());
  server.sendContent(buf);

  // IP info
  snprintf_P(buf, sizeof(buf), PSTR("<div class='info'><strong>IP:</strong> %s</div>"), WiFi.localIP().toString().c_str());
  server.sendContent(buf);

  // Hostname
  snprintf_P(buf, sizeof(buf), PSTR("<div class='info'><strong>Hostname:</strong> %s.local</div>"), config.hostname);
  server.sendContent(buf);

  // Uptime
  snprintf_P(buf, sizeof(buf), PSTR("<div class='info'><strong>Uptime:</strong> %lu seconds</div>"), millis()/1000);
  server.sendContent(buf);

  // NTP sync warning
  if (!timeClient.isTimeSet()) {
    snprintf_P(buf, sizeof(buf), PSTR("<div class='debug'><strong>⚠ NTP not synced yet</strong><br>Attempts: %d | Success: %d</div>"),
      ntpAttempts, ntpSuccesses);
    server.sendContent(buf);
  }

  // Footer
  server.sendContent_P(ROOT_HTML_FOOTER);

  // End chunked transfer
  server.sendContent("");
}

// PROGMEM templates for handleDebug() - chunked response to eliminate String concatenation
const char DEBUG_HTML_HEADER[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<title>Debug Info</title>"
  "<style>"
  "body{font-family:monospace;margin:20px;background:#f0f0f0;}"
  ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;}"
  ".ok{color:green;}.fail{color:red;}"
  "pre{background:#f5f5f5;padding:10px;border-radius:5px;overflow-x:auto;}"
  "button{background:#0066cc;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px;}"
  "</style>"
  "</head><body>"
  "<div class='container'>"
  "<h1>Debug Information</h1>"
  "<h2>Network</h2><pre>";

const char DEBUG_HTML_FOOTER[] PROGMEM =
  "<h2>Actions</h2>"
  "<button onclick=\"location.href='/test-ntp'\">Test NTP Now</button>"
  "<button onclick=\"location.href='/test-display'\">Test Display (8888)</button>"
  "<button onclick=\"location.reload()\">Refresh</button>"
  "<button onclick=\"location.href='/api/config'\">Download Config (JSON)</button>"
  "<button onclick=\"if(confirm('Clear EEPROM and reboot?')) fetch('/api/eeprom-clear', {method:'POST'}).then(()=>alert('Rebooting...'))\">Clear EEPROM</button>"
  "<button onclick=\"location.href='/'\">Back</button>"
  "</div></body></html>";

void ICACHE_FLASH_ATTR handleDebug() {
  char buf[200];  // Buffer for dynamic content

  // Start chunked transfer
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // Header
  server.sendContent_P(DEBUG_HTML_HEADER);

  // Network info
  snprintf_P(buf, sizeof(buf), PSTR("SSID: %s\nIP: %s\nGateway: %s\nDNS: %s\nRSSI: %d dBm\nHostname: %s\n</pre>"),
    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
    WiFi.dnsIP().toString().c_str(), WiFi.RSSI(), config.hostname);
  server.sendContent(buf);

  // Internet connectivity
  server.sendContent_P(PSTR("<h2>Internet Connectivity</h2><pre>Status: "));
  server.sendContent_P(internetConnected ? PSTR("<span class='ok'>✓ Connected</span>\n</pre>") : PSTR("<span class='fail'>✗ Not connected</span>\n</pre>"));

  // NTP info
  server.sendContent_P(PSTR("<h2>NTP</h2><pre>"));
  snprintf_P(buf, sizeof(buf), PSTR("Server: %s\nUpdate interval: %u seconds\nSynced: "), config.ntp_server, config.ntp_interval);
  server.sendContent(buf);
  server.sendContent_P(timeClient.isTimeSet() ? PSTR("<span class='ok'>✓ Yes</span>\n") : PSTR("<span class='fail'>✗ No</span>\n"));

  snprintf_P(buf, sizeof(buf), PSTR("UTC time: %s\n"), timeClient.getFormattedTime().c_str());
  server.sendContent(buf);

  if (timeClient.isTimeSet()) {
    unsigned long epochTime = timeClient.getEpochTime();
    unsigned long localTime = epochTime + getTotalOffset(epochTime);
    snprintf_P(buf, sizeof(buf), PSTR("Local time: %02d:%02d:%02d\n"), (int)((localTime/3600)%24), (int)((localTime/60)%60), (int)(localTime%60));
    server.sendContent(buf);
  }

  snprintf_P(buf, sizeof(buf), PSTR("Attempts: %d\nSuccesses: %d\nLast error: %s\n</pre>"), ntpAttempts, ntpSuccesses, lastError.c_str());
  server.sendContent(buf);

  // Timezone & DST
  server.sendContent_P(PSTR("<h2>Timezone & DST</h2><pre>"));
  snprintf_P(buf, sizeof(buf), PSTR("Base offset: %.1f hours (%ld seconds)\nDST enabled: %s\n"),
    config.timezone_offset/3600.0, config.timezone_offset, config.dst_enabled ? "Yes" : "No");
  server.sendContent(buf);

  if (config.dst_enabled && timeClient.isTimeSet()) {
    unsigned long epochTime = timeClient.getEpochTime();
    bool inDST = isDST(epochTime);
    snprintf_P(buf, sizeof(buf), PSTR("DST active now: %s\nTotal offset: %.1f hours\n"),
      inDST ? "<span class='ok'>✓ Yes (+1 hour)</span>" : "✗ No", getTotalOffset(epochTime)/3600.0);
    server.sendContent(buf);
  }

  snprintf_P(buf, sizeof(buf), PSTR("Time format: %s\n</pre>"), config.hour_format_24 ? "24-hour" : "12-hour (AM/PM)");
  server.sendContent(buf);

  // Weather
  server.sendContent_P(PSTR("<h2>Weather</h2><pre>"));
  snprintf_P(buf, sizeof(buf), PSTR("Enabled: %s\nValid data: %s\n"),
    config.weather_enabled ? "Yes" : "No",
    weather.valid ? "<span class='ok'>✓ Yes</span>" : "<span class='fail'>✗ No</span>");
  server.sendContent(buf);

  if (weather.valid) {
    snprintf_P(buf, sizeof(buf), PSTR("Temperature: %.1f°C\nWeather code: %d\nWind speed: %.1f km/h\nLast update: %lu sec ago\n"),
      weather.temperature, weather.weathercode, weather.windspeed, weather.lastUpdate/1000);
    server.sendContent(buf);
  }

  snprintf_P(buf, sizeof(buf), PSTR("City: %s\nLocation: %.6f, %.6f\nUpdate interval: %u seconds\n</pre>"),
    config.city_name, config.latitude, config.longitude, config.weather_interval);
  server.sendContent(buf);

  // Sun times
  server.sendContent_P(PSTR("<h2>Sunrise/Sunset</h2><pre>"));
  snprintf_P(buf, sizeof(buf), PSTR("Enabled: %s\n"), config.show_sunrise_sunset ? "Yes" : "No");
  server.sendContent(buf);

  if (sunTimes.lastDay != -1) {
    snprintf_P(buf, sizeof(buf), PSTR("<span class='ok'>✓ Data available</span>\nSunrise: %s (%d min)\nSunset: %s (%d min)\nLast update day: %d\n</pre>"),
      sunTimes.sunrise, sunTimes.sunriseMinutes, sunTimes.sunset, sunTimes.sunsetMinutes, sunTimes.lastDay);
  } else {
    snprintf_P(buf, sizeof(buf), PSTR("<span class='fail'>✗ No data</span>\n</pre>"));
  }
  server.sendContent(buf);

  // Display
  server.sendContent_P(PSTR("<h2>Display</h2><pre>"));
  const char* modeStr = (displayMode == 0) ? " (Time)" : (displayMode == 1) ? " (Weather)" : " (Sun times)";
  snprintf_P(buf, sizeof(buf), PSTR("Current mode: %d%s\nRotation interval: %u seconds\nBrightness: %d (0-7)\nShow weather: %s\nShow sun times: %s\nNTP synced: %s\n</pre>"),
    displayMode, modeStr, config.display_rotation_sec, config.brightness,
    config.show_weather ? "Yes" : "No", config.show_sunrise_sunset ? "Yes" : "No",
    timeClient.isTimeSet() ? "<span class='ok'>✓ Yes</span>" : "<span class='fail'>✗ No</span>");
  server.sendContent(buf);

  // System
  server.sendContent_P(PSTR("<h2>System</h2><pre>"));
  snprintf_P(buf, sizeof(buf), PSTR("Uptime: %lu seconds\nFree heap: %u bytes\nChip ID: %X\nFlash size: %u bytes\nSDK version: %s\n</pre>"),
    millis()/1000, ESP.getFreeHeap(), ESP.getChipId(), ESP.getFlashChipSize(), ESP.getSdkVersion());
  server.sendContent(buf);

  // Footer
  server.sendContent_P(DEBUG_HTML_FOOTER);

  // End chunked transfer
  server.sendContent("");
}

void ICACHE_FLASH_ATTR handleTestNTP() {
  // Force NTP update
  testInternetConnectivity();
  updateNTPTime();

  // Redirect back to debug page
  server.sendHeader("Location", "/debug");
  server.send(303);
}

void ICACHE_FLASH_ATTR handleTestDisplay() {
  // Test display by showing "8888" for 3 seconds
  uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};  // All segments on = "8888"
  displaySegments(data);
  delay(3000);

  // Redirect back to debug page
  server.sendHeader("Location", "/debug");
  server.send(303);
}

// PROGMEM templates for handleConfig() - chunked response
const char CONFIG_HTML_HEADER[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<title>Configuration</title>"
  "<style>"
  "body{font-family:Arial;margin:20px;background:#f0f0f0;}"
  ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
  "input,select{width:100%;padding:8px;margin:5px 0 15px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}"
  "button{background:#0066cc;color:white;padding:12px 20px;border:none;border-radius:5px;cursor:pointer;width:100%;}"
  "button:hover{background:#0052a3;}"
  "label{font-weight:bold;}"
  "</style>"
  "</head><body>"
  "<div class='container'>"
  "<h1>Configuration</h1>"
  "<form method='POST' action='/config'>";

const char CONFIG_HTML_FOOTER[] PROGMEM =
  "<button type='submit'>Save & Reboot</button>"
  "</form>"
  "<p><a href='/'>Back to Home</a></p>"
  "</div></body></html>";

void ICACHE_FLASH_ATTR handleConfig() {
  char buf[150];

  // Start chunked transfer
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // Header
  server.sendContent_P(CONFIG_HTML_HEADER);

  // WiFi settings
  snprintf_P(buf, sizeof(buf), PSTR("<label>WiFi SSID:</label><input type='text' name='ssid' value='%s' required>"), config.ssid);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>WiFi Password:</label><input type='password' name='password' value='%s'>"), config.password);
  server.sendContent(buf);

  // System settings
  snprintf_P(buf, sizeof(buf), PSTR("<label>Timezone Offset (seconds):</label><input type='number' name='timezone' value='%ld'>"), config.timezone_offset);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>Brightness (0-7):</label><input type='number' name='brightness' min='0' max='7' value='%d'>"), config.brightness);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>Hostname:</label><input type='text' name='hostname' value='%s'>"), config.hostname);
  server.sendContent(buf);

  // Weather settings
  server.sendContent_P(PSTR("<h2 style='margin-top:20px;'>Weather Settings</h2>"));
  snprintf_P(buf, sizeof(buf), PSTR("<label>City Name:</label><input type='text' name='city_name' value='%s'>"), config.city_name);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>Latitude:</label><input type='number' step='0.000001' name='latitude' value='%.6f'>"), config.latitude);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>Longitude:</label><input type='number' step='0.000001' name='longitude' value='%.6f'>"), config.longitude);
  server.sendContent(buf);
  snprintf_P(buf, sizeof(buf), PSTR("<label>Weather Update Interval (seconds):</label><input type='number' name='weather_interval' value='%u'>"), config.weather_interval);
  server.sendContent(buf);

  // Display settings
  server.sendContent_P(PSTR("<h2 style='margin-top:20px;'>Display Settings</h2>"));
  snprintf_P(buf, sizeof(buf), PSTR("<label>Screen Rotation Interval (seconds):</label><input type='number' name='display_rotation_sec' value='%u'>"), config.display_rotation_sec);
  server.sendContent(buf);

  // Footer
  server.sendContent_P(CONFIG_HTML_FOOTER);

  // End chunked transfer
  server.sendContent("");
}

void ICACHE_FLASH_ATTR handleConfigSave() {
  if (server.hasArg("ssid")) {
    safeStringCopy(server.arg("ssid"), config.ssid, sizeof(config.ssid));
  }
  if (server.hasArg("password")) {
    safeStringCopy(server.arg("password"), config.password, sizeof(config.password));
  }
  if (server.hasArg("timezone")) {
    config.timezone_offset = server.arg("timezone").toInt();
  }
  if (server.hasArg("brightness")) {
    config.brightness = server.arg("brightness").toInt();
    // OLED brightness controlled by hardware (no software control with Adafruit lib)
  }
  if (server.hasArg("hostname")) {
    safeStringCopy(server.arg("hostname"), config.hostname, sizeof(config.hostname));
  }
  if (server.hasArg("city_name")) {
    safeStringCopy(server.arg("city_name"), config.city_name, sizeof(config.city_name));
  }
  if (server.hasArg("latitude")) {
    config.latitude = server.arg("latitude").toFloat();
  }
  if (server.hasArg("longitude")) {
    config.longitude = server.arg("longitude").toFloat();
  }
  if (server.hasArg("weather_interval")) {
    config.weather_interval = server.arg("weather_interval").toInt();
  }
  if (server.hasArg("display_rotation_sec")) {
    config.display_rotation_sec = server.arg("display_rotation_sec").toInt();
  }
  if (server.hasArg("display_orientation")) {
    config.display_orientation = server.arg("display_orientation").toInt();
    display.setRotation(config.display_orientation);
  }

  saveConfig();

  String html = F("<!DOCTYPE html><html><head>");
  html += F("<meta charset='UTF-8'>");
  html += F("<meta http-equiv='refresh' content='5;url=/'>");
  html += F("<style>body{font-family:Arial;text-align:center;margin-top:50px;}</style>");
  html += F("</head><body>");
  html += F("<h1>Configuration Saved!</h1>");
  html += F("<p>Device will reboot in 5 seconds...</p>");
  html += F("</body></html>");

  server.send(200, "text/html", html);

  delay(1000);
  ESP.restart();
}

void ICACHE_FLASH_ATTR handleAPITime() {
  String json = "{";
  json += "\"time\":\"" + timeClient.getFormattedTime() + "\",";
  json += "\"hours\":" + String(timeClient.getHours()) + ",";
  json += "\"minutes\":" + String(timeClient.getMinutes()) + ",";
  json += "\"seconds\":" + String(timeClient.getSeconds()) + ",";
  json += "\"epoch\":" + String(timeClient.getEpochTime());
  json += "}";

  server.send(200, "application/json", json);
}

void ICACHE_FLASH_ATTR handleAPIStatus() {
  String json = "{";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"hostname\":\"" + String(config.hostname) + "\"";
  json += "},";
  json += "\"time\":{";
  json += "\"current\":\"" + timeClient.getFormattedTime() + "\",";
  json += "\"timezone_offset\":" + String(config.timezone_offset) + ",";
  json += "\"ntp_synced\":" + String(timeClient.isTimeSet() ? "true" : "false");
  json += "},";
  json += "\"system\":{";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\"";
  json += "}";
  json += "}";

  server.send(200, "application/json", json);
}

void ICACHE_FLASH_ATTR handleAPIDebug() {
  String json = "{";
  json += "\"internet_connected\":" + String(internetConnected ? "true" : "false") + ",";
  json += "\"ntp_attempts\":" + String(ntpAttempts) + ",";
  json += "\"ntp_successes\":" + String(ntpSuccesses) + ",";
  json += "\"last_error\":\"" + lastError + "\",";
  json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"dns\":\"" + WiFi.dnsIP().toString() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void ICACHE_FLASH_ATTR handleAPIWeather() {
  String json = "{";
  json += "\"enabled\":" + String(config.weather_enabled ? "true" : "false") + ",";
  json += "\"valid\":" + String(weather.valid ? "true" : "false") + ",";
  json += "\"temperature\":" + String(weather.temperature, 1) + ",";
  json += "\"weathercode\":" + String(weather.weathercode) + ",";
  json += "\"windspeed\":" + String(weather.windspeed, 1) + ",";
  json += "\"last_update\":" + String(weather.lastUpdate) + ",";
  json += "\"sunrise\":\"" + String(sunTimes.sunrise) + "\",";
  json += "\"sunset\":\"" + String(sunTimes.sunset) + "\",";
  json += "\"sunrise_minutes\":" + String(sunTimes.sunriseMinutes) + ",";
  json += "\"sunset_minutes\":" + String(sunTimes.sunsetMinutes);
  json += "}";

  server.send(200, "application/json", json);
}

void ICACHE_FLASH_ATTR handleAPIConfigExport() {
  String json = "{";
  json += "\"firmware_version\":\"" FIRMWARE_VERSION "\",";
  json += "\"magic\":\"0x" + String(config.magic, HEX) + "\",";
  json += "\"ssid\":\"" + String(config.ssid) + "\",";
  json += "\"password\":\"" + String(config.password) + "\",";
  json += "\"timezone_offset\":" + String(config.timezone_offset) + ",";
  json += "\"dst_enabled\":" + String(config.dst_enabled ? "true" : "false") + ",";
  json += "\"brightness\":" + String(config.brightness) + ",";
  json += "\"ntp_server\":\"" + String(config.ntp_server) + "\",";
  json += "\"ntp_interval\":" + String(config.ntp_interval) + ",";
  json += "\"hour_format_24\":" + String(config.hour_format_24 ? "true" : "false") + ",";
  json += "\"hostname\":\"" + String(config.hostname) + "\",";

  // Weather settings
  json += "\"latitude\":" + String(config.latitude, 6) + ",";
  json += "\"longitude\":" + String(config.longitude, 6) + ",";
  json += "\"city_name\":\"" + String(config.city_name) + "\",";
  json += "\"weather_enabled\":" + String(config.weather_enabled ? "true" : "false") + ",";
  json += "\"weather_interval\":" + String(config.weather_interval) + ",";

  // Display settings
  json += "\"display_rotation_sec\":" + String(config.display_rotation_sec) + ",";
  json += "\"show_weather\":" + String(config.show_weather ? "true" : "false") + ",";
  json += "\"show_sunrise_sunset\":" + String(config.show_sunrise_sunset ? "true" : "false");
  json += "}";

  server.sendHeader("Content-Disposition", "attachment; filename=clock-config.json");
  server.send(200, "application/json", json);
}

void ICACHE_FLASH_ATTR handleAPIConfigImport() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No config data received");
    return;
  }

  String body = server.arg("plain");
  Serial.println("Received config: " + body);

  // Simple JSON parsing (for production, consider ArduinoJson library)
  // This is basic parsing - just extracts values between quotes and colons
  int pos;

  // Parse SSID
  pos = body.indexOf("\"ssid\":\"");
  if (pos >= 0) {
    int start = pos + 8;
    int end = body.indexOf("\"", start);
    if (end > start) {
      String ssid = body.substring(start, end);
      safeStringCopy(ssid, config.ssid, sizeof(config.ssid));
    }
  }

  // Parse password
  pos = body.indexOf("\"password\":\"");
  if (pos >= 0) {
    int start = pos + 12;
    int end = body.indexOf("\"", start);
    if (end > start) {
      String password = body.substring(start, end);
      safeStringCopy(password, config.password, sizeof(config.password));
    }
  }

  // Parse timezone_offset
  pos = body.indexOf("\"timezone_offset\":");
  if (pos >= 0) {
    int start = pos + 18;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.timezone_offset = body.substring(start, end).toInt();
    }
  }

  // Parse brightness
  pos = body.indexOf("\"brightness\":");
  if (pos >= 0) {
    int start = pos + 13;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.brightness = body.substring(start, end).toInt();
    }
  }

  // Parse hostname
  pos = body.indexOf("\"hostname\":\"");
  if (pos >= 0) {
    int start = pos + 12;
    int end = body.indexOf("\"", start);
    if (end > start) {
      String hostname = body.substring(start, end);
      safeStringCopy(hostname, config.hostname, sizeof(config.hostname));
    }
  }

  // Parse dst_enabled
  pos = body.indexOf("\"dst_enabled\":");
  if (pos >= 0) {
    int start = pos + 14;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      String value = body.substring(start, end);
      value.trim();
      config.dst_enabled = (value == "true");
    }
  }

  // Parse ntp_server
  pos = body.indexOf("\"ntp_server\":\"");
  if (pos >= 0) {
    int start = pos + 14;
    int end = body.indexOf("\"", start);
    if (end > start) {
      String ntp_server = body.substring(start, end);
      safeStringCopy(ntp_server, config.ntp_server, sizeof(config.ntp_server));
    }
  }

  // Parse ntp_interval
  pos = body.indexOf("\"ntp_interval\":");
  if (pos >= 0) {
    int start = pos + 15;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.ntp_interval = body.substring(start, end).toInt();
    }
  }

  // Parse hour_format_24
  pos = body.indexOf("\"hour_format_24\":");
  if (pos >= 0) {
    int start = pos + 17;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      String value = body.substring(start, end);
      value.trim();
      config.hour_format_24 = (value == "true");
    }
  }

  // Parse weather settings
  pos = body.indexOf("\"latitude\":");
  if (pos >= 0) {
    int start = pos + 11;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.latitude = body.substring(start, end).toFloat();
    }
  }

  pos = body.indexOf("\"longitude\":");
  if (pos >= 0) {
    int start = pos + 12;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.longitude = body.substring(start, end).toFloat();
    }
  }

  pos = body.indexOf("\"city_name\":\"");
  if (pos >= 0) {
    int start = pos + 13;
    int end = body.indexOf("\"", start);
    if (end > start) {
      String city_name = body.substring(start, end);
      safeStringCopy(city_name, config.city_name, sizeof(config.city_name));
    }
  }

  pos = body.indexOf("\"weather_enabled\":");
  if (pos >= 0) {
    int start = pos + 18;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      String value = body.substring(start, end);
      value.trim();
      config.weather_enabled = (value == "true");
    }
  }

  pos = body.indexOf("\"weather_interval\":");
  if (pos >= 0) {
    int start = pos + 19;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.weather_interval = body.substring(start, end).toInt();
    }
  }

  // Parse display settings
  pos = body.indexOf("\"display_rotation_sec\":");
  if (pos >= 0) {
    int start = pos + 23;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      config.display_rotation_sec = body.substring(start, end).toInt();
    }
  }

  pos = body.indexOf("\"show_weather\":");
  if (pos >= 0) {
    int start = pos + 15;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      String value = body.substring(start, end);
      value.trim();
      config.show_weather = (value == "true");
    }
  }

  pos = body.indexOf("\"show_sunrise_sunset\":");
  if (pos >= 0) {
    int start = pos + 22;
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end > start) {
      String value = body.substring(start, end);
      value.trim();
      config.show_sunrise_sunset = (value == "true");
    }
  }

  config.magic = CONFIG_MAGIC;
  saveConfig();

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Config imported and saved. Reboot recommended.\"}");
}

void ICACHE_FLASH_ATTR handleEEPROMClear() {
  EEPROM.begin(512);
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  EEPROM.end();

  Serial.println("EEPROM cleared!");

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"EEPROM cleared, device will reboot\"}");

  delay(1000);
  ESP.restart();
}

void ICACHE_FLASH_ATTR handleReboot() {
  Serial.println("Reboot requested via web interface");

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Device rebooting...\"}");

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

  // Try OLED addresses specifically
  json += ",\"oled_test\":{";

  Wire.beginTransmission(0x3C);
  json += "\"0x3C\":\"" + String(Wire.endTransmission() == 0 ? "FOUND" : "not found") + "\",";

  Wire.beginTransmission(0x3D);
  json += "\"0x3D\":\"" + String(Wire.endTransmission() == 0 ? "FOUND" : "not found") + "\"";

  json += "}}}";

  Serial.println("I2C Scan results: " + json);

  server.send(200, "application/json", json);
}

// Weather and Sun Functions
// Async weather response callback
void onWeatherResponse(void* optParm, AsyncHTTPRequest* request, int readyState) {
  (void)optParm;  // Unused

  if (readyState == 4) {  // Request complete
    weatherState = WEATHER_IDLE;

    int httpCode = request->responseHTTPcode();
    if (httpCode == 200) {
      String payload = request->responseText();
      Serial.printf("✓ Weather response: %d bytes\n", payload.length());

      // Parse JSON response
      StaticJsonDocument<1536> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        // Extract current weather
        JsonObject current = doc["current_weather"];
        weather.temperature = current["temperature"] | 0.0f;
        weather.weathercode = current["weathercode"] | -1;
        weather.windspeed = current["windspeed"] | 0.0f;
        weather.lastUpdate = millis();
        weather.valid = true;

        // Extract sunrise/sunset
        JsonArray daily_sunrise = doc["daily"]["sunrise"];
        JsonArray daily_sunset = doc["daily"]["sunset"];

        if (daily_sunrise.size() > 0 && daily_sunset.size() > 0) {
          const char* sunrise_str = daily_sunrise[0];
          const char* sunset_str = daily_sunset[0];

          // Parse ISO time (2026-01-02T07:52) -> HH:MM
          if (sunrise_str && strlen(sunrise_str) >= 16) {
            sunTimes.sunrise[0] = sunrise_str[11];
            sunTimes.sunrise[1] = sunrise_str[12];
            sunTimes.sunrise[2] = ':';
            sunTimes.sunrise[3] = sunrise_str[14];
            sunTimes.sunrise[4] = sunrise_str[15];
            sunTimes.sunrise[5] = '\0';

            sunTimes.sunriseMinutes = (sunrise_str[11] - '0') * 600 +
                                      (sunrise_str[12] - '0') * 60 +
                                      (sunrise_str[14] - '0') * 10 +
                                      (sunrise_str[15] - '0');
          }

          if (sunset_str && strlen(sunset_str) >= 16) {
            sunTimes.sunset[0] = sunset_str[11];
            sunTimes.sunset[1] = sunset_str[12];
            sunTimes.sunset[2] = ':';
            sunTimes.sunset[3] = sunset_str[14];
            sunTimes.sunset[4] = sunset_str[15];
            sunTimes.sunset[5] = '\0';

            sunTimes.sunsetMinutes = (sunset_str[11] - '0') * 600 +
                                     (sunset_str[12] - '0') * 60 +
                                     (sunset_str[14] - '0') * 10 +
                                     (sunset_str[15] - '0');
          }

          // Update lastDay
          time_t epochTime = timeClient.getEpochTime();
          struct tm *ptm = gmtime(&epochTime);
          sunTimes.lastDay = ptm->tm_yday;
        }

        weatherState = WEATHER_SUCCESS;
        weatherRetry.reset();  // Success! Reset retry counter
        Serial.printf("✓ Weather: %.1f°C, code %d, wind %.1f km/h\n",
                      weather.temperature, weather.weathercode, weather.windspeed);
      } else {
        weatherState = WEATHER_FAILED;
        weather.valid = false;
        lastError = String("JSON: ") + error.c_str();
        Serial.printf("✗ JSON parse error (attempt %d/%d): %s\n",
                      weatherRetry.currentRetry + 1, weatherRetry.maxRetries, error.c_str());

        // Schedule retry with exponential backoff
        weatherRetry.scheduleRetry();
        if (weatherRetry.maxRetriesReached()) {
          Serial.println("✗ Weather max retries reached, will try again later");
        } else {
          unsigned long backoff = weatherRetry.getBackoffDelay() / 1000;
          Serial.printf("  Retry scheduled in %lu seconds\n", backoff);
        }
      }

      doc.clear();
    } else {
      weatherState = WEATHER_FAILED;
      weather.valid = false;
      lastError = "Weather API: " + String(httpCode);
      Serial.printf("✗ HTTP error %d (attempt %d/%d)\n",
                    httpCode, weatherRetry.currentRetry + 1, weatherRetry.maxRetries);

      // Schedule retry with exponential backoff
      weatherRetry.scheduleRetry();
      if (weatherRetry.maxRetriesReached()) {
        Serial.println("✗ Weather max retries reached, will try again later");
      } else {
        unsigned long backoff = weatherRetry.getBackoffDelay() / 1000;
        Serial.printf("  Retry scheduled in %lu seconds\n", backoff);
      }
    }
  }
}

// Async weather fetch - non-blocking!
void fetchWeatherAsync() {
  if (!config.weather_enabled) {
    Serial.println(F("Weather disabled"));
    return;
  }

  if (weatherState != WEATHER_IDLE) {
    Serial.println(F("Weather request already in progress"));
    return;
  }

  // Build URL
  String url = "http://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + String(config.latitude, 2);
  url += "&longitude=" + String(config.longitude, 2);
  url += "&current_weather=true";
  url += "&daily=sunrise,sunset";
  url += "&timezone=auto";
  url += "&forecast_days=1";

  Serial.println(F("⬇️ Fetching weather (async)..."));

  // Open async request
  if (weatherRequest.open("GET", url.c_str())) {
    weatherRequest.onReadyStateChange(onWeatherResponse);
    weatherRequest.setTimeout(10);  // 10 seconds
    weatherRequest.send();
    weatherState = WEATHER_REQUESTING;
    Serial.println("✓ Weather request sent (non-blocking)");
  } else {
    weatherState = WEATHER_FAILED;
    Serial.println("✗ Failed to open weather request");
  }
}

void ICACHE_FLASH_ATTR calculateSunTimes() {
  // Sunrise/sunset are fetched from Open-Meteo API in fetchWeather()
  // This function is kept as placeholder for future local calculation if needed

  if (!config.show_sunrise_sunset) return;

  // Data already provided by fetchWeather() API call
  if (sunTimes.lastDay != -1) {
    Serial.println(F("Sun times already available from API"));
    return;
  }

  Serial.println(F("⚠ Sun times not available yet - will be fetched with weather"));
}

// EEPROM functions
void ICACHE_FLASH_ATTR loadConfig() {
  EEPROM.begin(512);

  Config tempConfig;
  EEPROM.get(0, tempConfig);

  // Validate magic number
  if (tempConfig.magic == CONFIG_MAGIC) {
    config = tempConfig;
    Serial.println("✓ Valid configuration loaded from EEPROM");
  } else {
    Serial.println("⚠ Invalid EEPROM data detected, using defaults");
    config.magic = CONFIG_MAGIC;  // Set magic number
    saveConfig();  // Save defaults to EEPROM
  }

  EEPROM.end();

  Serial.println("Configuration:");
  Serial.printf("  Magic: 0x%08X %s\n", config.magic,
                config.magic == CONFIG_MAGIC ? "✓" : "✗");
  Serial.printf("  SSID: %s\n", config.ssid);
  Serial.printf("  Timezone: %ld\n", config.timezone_offset);
  Serial.printf("  Brightness: %d\n", config.brightness);
  Serial.printf("  Hostname: %s\n", config.hostname);
}

void ICACHE_FLASH_ATTR saveConfig() {
  EEPROM.begin(512);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();

  Serial.println("Configuration saved!");
}
