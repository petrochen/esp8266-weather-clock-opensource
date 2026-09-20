/*
 * config.h - Configuration structures and constants
 * TJ-56-654 Weather Clock v1.9.3
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Firmware version
#define FIRMWARE_VERSION "1.9.10"

// OLED I2C Configuration
#define I2C_SDA 0  // GPIO0 (I2C Data) - SWAPPED!
#define I2C_SCL 2  // GPIO2 (I2C Clock) - SWAPPED!
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // No reset pin
#define OLED_ADDRESS 0x3C

// Configuration structure with validation
#define CONFIG_MAGIC 0xC10CC10C  // Magic number to validate EEPROM data

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

// Exponential backoff retry configuration (for NTP/Weather)
struct RetryConfig {
  uint8_t maxRetries = 3;          // Give up after 3 tries
  uint8_t currentRetry = 0;
  unsigned long nextRetryTime = 0;
  unsigned long maxBackoffMs = 8000;  // Max backoff 8 seconds

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
    // Subtraction-safe: works correctly across millis() rollover at ~49.7 days
    return nextRetryTime > 0 && (millis() - nextRetryTime) < 0x80000000UL;
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
    // Subtraction-safe: works correctly across millis() rollover at ~49.7 days
    return nextRetryTime > 0 && (millis() - nextRetryTime) < 0x80000000UL;
  }

  void reset() {
    currentRetry = 0;
    nextRetryTime = 0;
  }
};

// Weather fetch state machine
enum WeatherState {
  WEATHER_IDLE,
  WEATHER_REQUESTING,
  WEATHER_SUCCESS,
  WEATHER_FAILED
};

// Async NTP state machine — only IDLE and REQUEST_SENT are used
// (response handler transitions back to IDLE directly on success or timeout)
enum NTPState {
  NTP_IDLE,
  NTP_REQUEST_SENT
};

// Async WiFi state machine
enum WiFiConnectionState {
  WIFI_CONN_IDLE,
  WIFI_CONN_CONNECTING,
  WIFI_CONN_CONNECTED,
  WIFI_CONN_FAILED,
  WIFI_CONN_SKIP_ASYNC  // Skip async, go straight to WiFiManager
};

// Weather data cache
struct WeatherData {
  float temperature = 0.0;
  int weathercode = -1;  // WMO weather code
  int humidity = 0;
  float windspeed = 0.0;
  unsigned long lastUpdate = 0;
  bool valid = false;
};

// Sunrise/Sunset cache
struct SunTimes {
  int sunriseMinutes = 0;  // Minutes since midnight
  int sunsetMinutes = 0;
  int lastDay = -1;        // Day of year
  char sunrise[6] = "--:--";  // HH:MM format
  char sunset[6] = "--:--";
};

// Dissolve transition constants
const unsigned long DISSOLVE_DURATION = 2000;  // 2 sec total (1s dissolve out + 1s dissolve in)
const unsigned long DISSOLVE_FRAME_INTERVAL = 100;  // 100ms per frame

// NTP timeout
const unsigned long NTP_TIMEOUT_MS = 5000;  // 5 second timeout

// WiFi timeout
const unsigned long WIFI_TIMEOUT_MS = 10000;  // 10 second timeout

// Triple power-cycle factory reset
// Counter stored at EEPROM offset 480, well past Config (~260 bytes)
#define RESET_COUNTER_ADDR   480
#define RESET_COUNTER_MAGIC  0xA5
#define RESET_COUNTER_WINDOW 10000UL  // 10s: if device runs longer, counter clears
#define RESET_COUNTER_TRIPS  3        // 3 quick power cycles = factory reset

struct ResetCounter {
  uint8_t magic;
  uint8_t count;
};

#endif // CONFIG_H
