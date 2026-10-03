/*
 * display.cpp - Display functions for OLED
 * TJ-56-654 Weather Clock
 */

#include "globals.h"
#include "weather_icons.h"
#include "status_icons.h"
#include "display_text.h"

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
  display.setTextWrap(false);
  display.cp437(true);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
  invalidateDisplay();
}

// All text uses explicit rectangles: GFX's implicit wrap must never move a
// heading into another screen region. Portrait has a 32px footer for long names.
void ICACHE_FLASH_ATTR updateDisplay() {
  display.clearDisplay();
  if (!timeIsSynced) {
    displayText("--:--", 0, footerTop(), 3);
    displayFooter(wifiConnState == WIFI_CONN_CONNECTED ? "Syncing NTP..." : "No WiFi");
  } else {
    unsigned long epoch = getAsyncEpoch();
    unsigned long local = epoch + getTotalOffset(epoch);
    int hours = (local / 3600) % 24;
    const bool pm = hours >= 12;
    if (!config.hour_format_24) hours = hours % 12 ? hours % 12 : 12;
    char value[32];
    snprintf(value, sizeof(value), "%02d%c%02d", hours, colonBlink ? ':' : ' ', int((local / 60) % 60));
    displayText(value, 0, footerTop(), 3);
    if (!config.hour_format_24) displayText(pm ? "PM" : "AM", footerTop() - 9, 8);
    time_t t = local;
    struct tm* date = gmtime(&t);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(value, sizeof(value), "%s%s %02d.%02d", wifiConnState == WIFI_CONN_CONNECTED ? "" : "!",
             days[date->tm_wday], date->tm_mday, date->tm_mon + 1);
    displayFooter(value, 2);
  }
  if (!inTransition) display.display();
}

void ICACHE_FLASH_ATTR displayWeather() {
  display.clearDisplay();
  if (!weather.valid) {
    displayText("No Data", 0, footerTop(), 2);
    displayFooter(display.width() < 100 ? "No reading" : "Weather unavailable");
  } else {
    if (weather.stale) {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print('*');
    }
    displayFooter(config.city_name, 2);
    char value[16];
    snprintf(value, sizeof(value), "%.1f", weather.temperature);
    if (display.width() < 100) {
      display.drawBitmap((display.width() - 16) / 2, 12, weatherIcon(weather.weathercode), 16, 16, SSD1306_WHITE);
      displayText(value, 36, 24, 2);
      displayText("°C", 64, 8);
    } else {
      display.drawBitmap(0, 16, weatherIcon(weather.weathercode), 16, 16, SSD1306_WHITE);
      uint8_t size = strlen(value) > 4 ? 2 : 3;
      int width = strlen(value) * 6 * size;
      // Reserve the 16px icon, a 6px gap, and 12px for the unit.
      int x = 22 + (display.width() - 22 - width - 12) / 2;
      int y = (footerTop() - 8 * size) / 2;
      display.setTextSize(size);
      display.setCursor(x, y);
      display.print(value);
      display.setTextSize(1);
      drawDisplayGlyph(0xB0, x + width, y, 1);
      drawDisplayGlyph('C', x + width + 6, y, 1);
    }
  }
  if (!inTransition) display.display();
}

void ICACHE_FLASH_ATTR displaySunTimes() {
  display.clearDisplay();
  if (sunTimes.lastDay == -1) {
    displayText("--:--", 0, footerTop(), 3);
    displayFooter(display.width() < 100 ? "No sun data" : "Sun times unavailable");
  } else {
    const bool portrait = display.width() < 100;
    // Small explicit labels stay unambiguous on both orientations.
    // Landscape uses native sun/horizon glyphs beside the large time digits.
    char line[32];
    if (portrait) {
      displayText("Sunrise", 4, 8);
      displayText(sunTimes.sunrise, 16, 24, 2);
      displayText("Sunset", 48, 8);
      displayText(sunTimes.sunset, 60, 24, 2);
    } else {
      display.setTextSize(2);
      display.drawBitmap(22, 4, sunriseIcon, 16, 16, SSD1306_WHITE);
      display.drawBitmap(22, 28, sunsetIcon, 16, 16, SSD1306_WHITE);
      display.setCursor(46, 4); display.print(sunTimes.sunrise);
      display.setCursor(46, 28); display.print(sunTimes.sunset);
    }
    int minutes = sunTimes.sunsetMinutes - sunTimes.sunriseMinutes;
    snprintf(line, sizeof(line), "Day %dh %dm", minutes / 60, minutes % 60);
    displayFooter(line);
  }
  if (!inTransition) display.display();
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

// Explicit test screen; OTA has its own label and percent unit.
void ICACHE_FLASH_ATTR showNumber(int num, bool leadingZeros) {
  wakeDisplay();
  display.clearDisplay();
  char value[16];
  snprintf(value, sizeof(value), leadingZeros ? "%04d" : "%d", num);
  displayText(value, 0, display.height(), 3);
  display.display();
}

void ICACHE_FLASH_ATTR showUpdateProgress(int percent) {
  wakeDisplay();
  display.clearDisplay();
  char value[8];
  snprintf(value, sizeof(value), "%d%%", percent < 0 ? 0 : percent > 100 ? 100 : percent);
  displayText(value, 0, footerTop(), 3);
  displayFooter("Updating");
  display.display();
}

void ICACHE_FLASH_ATTR showNoWiFi(unsigned long nextRetrySeconds) {
  if (maintenanceCodeVisible() || isNightModeActive()) return;
  display.clearDisplay();
  displayText("No WiFi", 0, footerTop(), 2);
  char retry[32];
  snprintf(retry, sizeof(retry), "Retry in %lu %s", nextRetrySeconds < 60 ? nextRetrySeconds : nextRetrySeconds / 60,
           nextRetrySeconds < 60 ? "sec" : "min");
  displayFooter(retry);
  display.display();
}

// Startup is a single frame: no delay or extra animation before initialization.
void ICACHE_FLASH_ATTR showStartupAnimation() {
  display.clearDisplay();
  displayText("TJ-56", 0, footerTop() / 2, 2);
  displayText("Weather Clock", footerTop() / 2, footerTop() / 2);
  displayFooter("v" FIRMWARE_VERSION);
  display.display();
}

void ICACHE_FLASH_ATTR showWiFiConnecting(int step) {
  display.clearDisplay();
  displayText("Wi-Fi", 0, footerTop() / 2, 2);
  const uint8_t phase = uint8_t(step < 0 ? 0 : step % 3);
  display.drawBitmap((display.width() - 24) / 2, (footerTop() * 3 / 4) - 8,
                     wifiConnectingFrames[phase], 24, 16, SSD1306_WHITE);
  displayFooter("Connecting");
  display.display();
}

void ICACHE_FLASH_ATTR showConnected() {
  if (maintenanceCodeVisible() || isNightModeActive()) return;
  display.clearDisplay();
  String ssid = WiFi.SSID();
  displayText(ssid.c_str(), 0, footerTop() / 2);
  IPAddress ip = WiFi.localIP();
  char address[16];
  snprintf(address, sizeof(address), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  displayText(address, footerTop() / 2, footerTop() / 2, 2);
  displayFooter("OK");
  display.display();
  ipDisplayUntil = millis() + 2000UL;
}

// The AP name and password must fit even in portrait; break only the visual
// representation. These strings are the actual setup network credentials.
void ICACHE_FLASH_ATTR showSetupScreen(bool afterReset) {
  display.clearDisplay();
  const bool portrait = display.width() < 100;
  displayText(afterReset ? "WiFi reset" : "Setup mode", 0, portrait ? 24 : 16, 2);
  displayText("TJ56654-Setup", portrait ? 32 : 20, portrait ? 24 : 12);
  displayText("Pass: 12345678", portrait ? 64 : 36, portrait ? 24 : 12);
  displayFooter("Connect to WiFi");
  display.display();
}

void ICACHE_FLASH_ATTR showIP() { showConnected(); }
