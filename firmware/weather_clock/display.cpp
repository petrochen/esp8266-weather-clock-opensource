/*
 * display.cpp - Display functions for OLED
 * TJ-56-654 Weather Clock
 */

#include "globals.h"
#include "weather_icons.h"

static bool displayDirty = true;
static bool displaySleeping = false;

void ICACHE_FLASH_ATTR wakeDisplay() {
  if (!displaySleeping) return;
  display.ssd1306_command(SSD1306_DISPLAYON);
  displaySleeping = false;
  displayDirty = true;
  lastModeSwitch = millis();
}

void ICACHE_FLASH_ATTR invalidateDisplay() { displayDirty = true; }

void ICACHE_FLASH_ATTR applyDisplaySettings() {
  // Legacy EEPROM may contain zero (previous firmware ignored brightness) or
  // values that bypassed form validation. Zero is a dim level, not screen-off.
  const int level = config.brightness < 0 ? 0 : (config.brightness > 7 ? 7 : config.brightness);
  const uint8_t contrast = 32 + level * (255 - 32) / 7;
  if (config.display_orientation > 3) config.display_orientation = 2;
  display.setRotation(config.display_orientation);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
  invalidateDisplay();
}

// Update main time display
void ICACHE_FLASH_ATTR updateDisplay() {
  display.clearDisplay();

  // Do not display an epoch until the shared clock has synchronized.
  bool hasTime = timeIsSynced;

  if (!hasTime) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println(F("--:--"));

    // Show WiFi status in yellow zone
    display.setTextSize(1);
    display.setCursor(25, 52);
    if (wifiConnState == WIFI_CONN_CONNECTED) {
      display.print(F("Syncing NTP..."));
    } else {
      display.print(F("No WiFi"));
    }
    if (!inTransition) {
      display.display();
    }
    return;
  }

  // Get epoch from best available source
  unsigned long epochTime = getAsyncEpoch();
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
  time_t t = localTime;
  struct tm *ptm = gmtime(&t);

  // Format: "Thu 02.01" or "! Thu 02.01" if no WiFi
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char dateStr[20];

  if (wifiConnState != WIFI_CONN_CONNECTED) {
    // Show "!" indicator when WiFi is down
    snprintf(dateStr, sizeof(dateStr), "!%s %02d.%02d", days[ptm->tm_wday], ptm->tm_mday, ptm->tm_mon + 1);
  } else {
    snprintf(dateStr, sizeof(dateStr), "%s %02d.%02d", days[ptm->tm_wday], ptm->tm_mday, ptm->tm_mon + 1);
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
  display.setCursor(10, 12);  // Y=12 centers in blue zone (0-47)
  display.printf("%02d", hours);

  // Blinking colon
  if (colonBlink) {
    display.print(F(":"));
  } else {
    display.print(F(" "));
  }

  display.printf("%02d", minutes);

  // Don't send to screen during transition - crossfade will do it
  if (!inTransition) {
    display.display();
  }
}

// Weather display
void ICACHE_FLASH_ATTR displayWeather() {
  display.clearDisplay();

  if (!weather.valid) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println(F("No Data"));
    if (!inTransition) {
      display.display();
    }
    return;
  }

  if (weather.stale) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(F("*"));
  }

  // === YELLOW ZONE (Y: 48-63): City name (size 2 = 16px) ===
  display.setTextSize(2);
  int cityWidth = strlen(config.city_name) * 12;
  int cityX = (128 - cityWidth) / 2;
  display.setCursor(cityX, 48);
  display.print(config.city_name);

  // Keep the 16px icon, stale marker and even -99.9 C inside the 128px row.
  display.drawBitmap(0, 19, weatherIcon(weather.weathercode), ICON_WIDTH, ICON_HEIGHT, SSD1306_WHITE);
  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.1f", weather.temperature);
  int textSize = strlen(tempStr) > 4 ? 2 : 3;
  int valueWidth = strlen(tempStr) * 6 * textSize;
  int startX = 22 + (106 - valueWidth - 12) / 2;
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(startX, 16);
  display.print(tempStr);
  display.setTextSize(1);
  display.setCursor(startX + valueWidth, 16);
  display.print("\xF8" "c");

  if (!inTransition) {
    display.display();
  }
}

// Sunrise/Sunset display
void ICACHE_FLASH_ATTR displaySunTimes() {
  display.clearDisplay();

  if (sunTimes.lastDay == -1) {
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(30, 24);
    display.println(F("----"));
    if (!inTransition) {
      display.display();
    }
    return;
  }

  // === YELLOW ZONE (Y: 48-63): Daylight duration ===
  int daylightMinutes = sunTimes.sunsetMinutes - sunTimes.sunriseMinutes;
  int daylightHours = daylightMinutes / 60;
  int daylightMins = daylightMinutes % 60;

  char daylightStr[32];
  snprintf(daylightStr, sizeof(daylightStr), "Day %dh %dm", daylightHours, daylightMins);

  display.setTextSize(1);
  int textWidth = strlen(daylightStr) * 6;
  int textX = (128 - textWidth) / 2;
  display.setCursor(textX, 52);
  display.print(daylightStr);

  // === BLUE ZONE (Y: 0-47): Sunrise and Sunset times ===
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  // Line 1: Sunrise
  display.setCursor(5, 4);
  display.print(F("\x18 "));  // Up arrow
  display.print(sunTimes.sunrise);

  // Line 2: Sunset
  display.setCursor(5, 28);
  display.print(F("\x19 "));  // Down arrow
  display.print(sunTimes.sunset);

  if (!inTransition) {
    display.display();
  }
}

// Apply dissolve effect with optional drift (Thanos-style)
void ICACHE_FLASH_ATTR applyDissolveEffect(uint8_t hidePercent, bool withDrift) {
  // Apply drift effect - shift buffer to the right
  if (withDrift && hidePercent > 10) {
    uint8_t* buffer = display.getBuffer();
    // SSD1306 buffer: 8 pages (8 rows each), 128 bytes per page
    for (int page = 0; page < 8; page++) {
      int pageOffset = page * SCREEN_WIDTH;
      // Shift right by 2 pixels per frame
      for (int x = SCREEN_WIDTH - 1; x > 1; x--) {
        buffer[pageOffset + x] = buffer[pageOffset + x - 2];
      }
      buffer[pageOffset] = 0;
      buffer[pageOffset + 1] = 0;
    }
  }

  // Direct byte masks: no random calls, duplicate work, or drawPixel overhead.
  const uint16_t threshold = uint32_t(hidePercent) * 8192U / 100U;
  uint8_t* buffer = display.getBuffer();
  for (uint16_t i = 0; i < 1024; ++i) {
    uint8_t mask = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (dissolveRank(i * 8 + bit) >= threshold) mask |= 1U << bit;
    }
    buffer[i] &= mask;
  }

  display.display();
}

// Display rotation with dissolve transition
void ICACHE_FLASH_ATTR updateDisplayRotation() {
  if (maintenanceCodeVisible()) return;
  unsigned long now = millis();
  if (ipDisplayUntil != 0) {
    if (uint32_t(now - ipDisplayUntil) >= 0x80000000UL) return;
    ipDisplayUntil = 0;
    invalidateDisplay();
  }
  if (isNightModeActive()) {
    if (!displaySleeping) display.ssd1306_command(SSD1306_DISPLAYOFF);
    displaySleeping = true;
    inTransition = false;
    return;
  }
  wakeDisplay();
  unsigned long interval = config.display_rotation_sec * 1000UL;

  // Handle active dissolve transition (two phases)
  if (inTransition) {
    unsigned long elapsed = now - transitionStart;

    if (elapsed >= DISSOLVE_DURATION) {
      displayMode = nextDisplayMode;
      inTransition = false;
      lastModeSwitch = now;
      invalidateDisplay();
      return;
    }

    if (now - lastDissolveFrame < DISSOLVE_FRAME_INTERVAL) {
      return;
    }
    lastDissolveFrame = now;

    uint8_t currentMode;
    uint8_t hidePercent;
    unsigned long halfDuration = DISSOLVE_DURATION / 2;
    bool isDriftPhase;

    if (elapsed < halfDuration) {
      // Phase 1: dissolve OUT old content
      currentMode = displayMode;
      hidePercent = (elapsed * 100) / halfDuration;
      isDriftPhase = true;
    } else {
      // Phase 2: dissolve IN new content
      currentMode = nextDisplayMode;
      unsigned long phase2Elapsed = elapsed - halfDuration;
      hidePercent = 100 - (phase2Elapsed * 100) / halfDuration;
      isDriftPhase = false;
    }

    switch(currentMode) {
      case 0: updateDisplay(); break;
      case 1: displayWeather(); break;
      case 2: displaySunTimes(); break;
    }

    applyDissolveEffect(hidePercent, isDriftPhase);
    return;
  }

  // Check if time to switch modes
  if (now - lastModeSwitch > interval) {
    uint8_t attempts = 0;
    nextDisplayMode = displayMode;
    do {
      nextDisplayMode = (nextDisplayMode + 1) % 3;
      attempts++;
      if (attempts >= 3) {
        nextDisplayMode = 0;
        Serial.println("WARNING: No display mode enabled, forcing time mode");
        break;
      }
    } while (!isModeEnabled(nextDisplayMode));

    if (nextDisplayMode == displayMode) {
      lastModeSwitch = now;
      return;
    }
    inTransition = true;
    transitionStart = now;
    lastModeSwitch = now;
    lastDissolveFrame = 0;
    return;
  }

  // Static screens only change on new data/settings. The clock blinks at 2 Hz.
  static unsigned long lastClockFrame = 0;
  if (!isModeEnabled(displayMode)) {
    displayMode = 0;
    displayDirty = true;
  }
  if (!displayDirty && (displayMode != 0 || uint32_t(now - lastClockFrame) < 500)) return;
  displayDirty = false;
  lastClockFrame = now;
  switch(displayMode) {
    case 0: updateDisplay(); break;
    case 1: displayWeather(); break;
    case 2: displaySunTimes(); break;
  }
}

// Check if display mode is enabled
bool ICACHE_FLASH_ATTR isModeEnabled(uint8_t mode) {
  switch(mode) {
    case 0: return true;  // Time always enabled
    case 1: return config.show_weather && weather.valid;
    case 2: return config.show_sunrise_sunset && sunTimes.lastDay != -1;
  }
  return false;
}

// Clear display
void ICACHE_FLASH_ATTR clearDisplay() {
  display.clearDisplay();
  display.display();
}

// Show number on OLED
void ICACHE_FLASH_ATTR showNumber(int num, bool leadingZeros) {
  wakeDisplay();
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20);

  if (leadingZeros) {
    display.printf("%04d", num);
  } else {
    display.print(num);
  }

  display.display();
}

// Show "No WiFi" status
void ICACHE_FLASH_ATTR showNoWiFi(unsigned long nextRetrySeconds) {
  if (maintenanceCodeVisible() || isNightModeActive()) return;
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 8);
  display.println(F("No WiFi"));

  display.setTextSize(1);
  display.setCursor(10, 52);
  if (nextRetrySeconds < 60) {
    display.printf("Retry in %lu sec", nextRetrySeconds);
  } else {
    display.printf("Retry in %lu min", nextRetrySeconds / 60);
  }

  display.display();
}

// Startup animation - just logo and version
// Layout: Blue zone (Y 0-47), Yellow zone (Y 48-63)
void ICACHE_FLASH_ATTR showStartupAnimation() {
  Serial.println("  Boot: Show logo");

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // "TJ-56" centered in BLUE zone
  // 5 chars × 10px + 4 gaps × 2px = 58px → X = (128-58)/2 = 35
  display.setTextSize(2);
  display.setCursor(35, 10);
  display.print(F("TJ-56"));

  // "Weather Clock" centered in BLUE zone
  // 13 chars × 5px + 12 gaps × 1px = 77px → X = (128-77)/2 = 26
  display.setTextSize(1);
  display.setCursor(26, 30);
  display.print(F("Weather Clock"));

  // Version in YELLOW zone (centered)
  // 6 chars × 5px + 5 gaps × 1px = 35px → X = (128-35)/2 = 47
  display.setTextSize(1);
  display.setCursor(47, 52);
  display.print(F("v"));
  display.print(FIRMWARE_VERSION);

  display.display();

  Serial.println("  Boot: Logo done");
}

// Show WiFi connecting animation with dots
// Layout: Blue zone (Y 0-47), Yellow zone (Y 48-63)
void ICACHE_FLASH_ATTR showWiFiConnecting(int step) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // "WiFi..." in BLUE zone (centered)
  // 7 chars × 10px + 6 gaps × 2px = 82px → X = 23
  display.setTextSize(2);
  display.setCursor(23, 10);
  display.print(F("WiFi..."));

  // Animated dots in BLUE zone (centered)
  // "* * * * * * " = 12 chars × 5px + 11 gaps × 1px = 71px → X = 29
  display.setTextSize(1);
  display.setCursor(29, 32);

  int dots = (step % 6) + 1;
  for (int i = 0; i < 6; i++) {
    if (i < dots) {
      display.print(F("* "));
    } else {
      display.print(F("  "));
    }
  }

  // "Connecting" in YELLOW zone (centered)
  // 10 chars × 5px + 9 gaps × 1px = 59px → X = 35
  display.setTextSize(1);
  display.setCursor(35, 52);
  display.print(F("Connecting"));

  display.display();
}

// Show connected status with SSID and IP
// Layout: Blue zone (Y 0-47), Yellow zone (Y 48-63)
void ICACHE_FLASH_ATTR showConnected() {
  if (maintenanceCodeVisible() || isNightModeActive()) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // SSID in BLUE zone (centered)
  // Formula: n chars × 5px + (n-1) gaps × 1px
  display.setTextSize(1);
  String ssid = WiFi.SSID();
  int ssidLen = ssid.length();
  int ssidWidth = ssidLen * 5 + (ssidLen - 1) * 1;
  int ssidX = (128 - ssidWidth) / 2;
  display.setCursor(ssidX, 8);
  display.print(ssid);

  // IP address in BLUE zone
  IPAddress ip = WiFi.localIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  int ipLen = strlen(ipStr);

  // Try size 2 first: n chars × 10px + (n-1) gaps × 2px
  int ipWidth2 = ipLen * 10 + (ipLen - 1) * 2;

  if (ipWidth2 <= 128) {
    display.setTextSize(2);
    int ipX = (128 - ipWidth2) / 2;
    display.setCursor(ipX, 24);
  } else {
    // Fallback to size 1: n chars × 5px + (n-1) gaps × 1px
    display.setTextSize(1);
    int ipWidth1 = ipLen * 5 + (ipLen - 1) * 1;
    int ipX = (128 - ipWidth1) / 2;
    display.setCursor(ipX, 24);
  }
  display.print(ipStr);

  // "OK" in YELLOW zone (centered)
  // 2 chars × 5px + 1 gap × 1px = 11px → X = 59
  display.setTextSize(1);
  display.setCursor(59, 52);
  display.print(F("OK"));

  display.display();
  ipDisplayUntil = millis() + 2000UL;
}

// Show IP address (legacy, for reconnection)
void ICACHE_FLASH_ATTR showIP() {
  showConnected();  // Use new unified function
}
