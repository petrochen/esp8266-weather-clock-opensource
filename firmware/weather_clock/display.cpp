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
static int appliedBrightness = -1;

static float ICACHE_FLASH_ATTR displayTemperature(float value) {
  return featureSettings.temperature_unit ? value * 1.8f + 32 : value;
}

static const char* ICACHE_FLASH_ATTR temperatureUnit() { return featureSettings.temperature_unit ? "°F" : "°C"; }

static const uint8_t* ICACHE_FLASH_ATTR currentWeatherIcon() {
  return !weather.isDay && weather.weathercode <= 2 && weather.weathercode >= 0 ? weather_moon : weatherIcon(weather.weathercode);
}

static void ICACHE_FLASH_ATTR applyBrightness(int level) {
  level = level < 0 ? 0 : level > 7 ? 7 : level;
  if (appliedBrightness == level) return;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(32 + level * (255 - 32) / 7);
  appliedBrightness = level;
}

bool ICACHE_FLASH_ATTR externalCardActive() {
  return featureSettings.external_enabled && externalCard.ttl && uint32_t(millis() - externalCard.received) < externalCard.ttl;
}

// A missing/expired today entry must never be replaced with tomorrow's UV.
static float ICACHE_FLASH_ATTR forecastUV(uint8_t ahead) {
  if (!config.weather_enabled || !weather.valid || !timeIsSynced) return -1;
  uint32_t now = getAsyncEpoch();
  uint32_t target = (now + getTotalOffset(now)) / 86400 + ahead;
  for (const auto& day : forecast.days)
    if (day.epoch && (day.epoch + getTotalOffset(day.epoch)) / 86400 == target) return day.uv;
  return -1;
}

static const char* ICACHE_FLASH_ATTR uvLevel(int index) {
  return index < 3 ? "Low" : index < 6 ? "Moderate" : index < 8 ? "High" : index < 11 ? "Very high" : "Extreme";
}

static uint8_t ICACHE_FLASH_ATTR firstHour() {
  uint8_t i = 0;
  while (i < forecast.count && timeIsSynced && forecast.hours[i].epoch <= getAsyncEpoch()) ++i;
  return i;
}

static uint8_t ICACHE_FLASH_ATTR firstDay() {
  uint32_t now = getAsyncEpoch();
  uint32_t today = (now + getTotalOffset(now)) / 86400;
  for (uint8_t i = 0; i < 2; ++i) {
    const auto& day = forecast.days[i];
    if (day.valid && (!timeIsSynced || (day.epoch + getTotalOffset(day.epoch)) / 86400 >= today)) return i;
  }
  return 2;
}

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
  if (config.display_orientation > 3) config.display_orientation = 2;
  display.setRotation(config.display_orientation);
  display.setTextWrap(false);
  display.cp437(true);
  appliedBrightness = -1;
  applyBrightness(config.brightness);
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
    if (featureSettings.clock_weather && weather.valid) {
      const bool portrait = display.width() < 100;
      displayText(value, portrait ? 20 : 14, 24, portrait ? 2 : 3);
      char temperature[20];
      snprintf(temperature, sizeof(temperature), "%s%.0f%s", weather.stale ? "*" : "", displayTemperature(weather.temperature), temperatureUnit());
      displayText(temperature, portrait ? 76 : 48, portrait ? 24 : 16, 2);
      if (portrait) display.drawBitmap(24, 51, currentWeatherIcon(), 16, 16, SSD1306_WHITE);
      else display.drawBitmap(0, 48, currentWeatherIcon(), 16, 16, SSD1306_WHITE);
      if (!config.hour_format_24) displayText(pm ? "PM" : "AM", portrait ? 106 : 36, 8);
      time_t calendar = local; struct tm* date = gmtime(&calendar);
      snprintf(value, sizeof(value), "%02d.%02d", date->tm_mday, date->tm_mon + 1);
      displayText(value, 0, 8);
      if (!inTransition) display.display();
      return;
    }
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
    snprintf(value, sizeof(value), "%.1f", displayTemperature(weather.temperature));
    if (display.width() < 100) {
      display.drawBitmap((display.width() - 16) / 2, 12, currentWeatherIcon(), 16, 16, SSD1306_WHITE);
      displayText(value, 36, 24, 2);
      displayText(temperatureUnit(), 64, 8);
    } else {
      display.drawBitmap(0, 16, currentWeatherIcon(), 16, 16, SSD1306_WHITE);
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
      drawDisplayGlyph(featureSettings.temperature_unit ? 'F' : 'C', x + width + 6, y, 1);
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
    if (featureSettings.sun_countdown && timeIsSynced) {
      uint32_t now = getAsyncEpoch();
      uint32_t soonest = 0;
      bool rising = false;
      for (const auto& day : forecast.days) for (int event = 0; event < 2; ++event) {
        uint32_t epoch = event ? day.sunset : day.sunrise;
        if (epoch > now && (!soonest || epoch < soonest)) { soonest = epoch; rising = !event; }
      }
      if (soonest) snprintf(line, sizeof(line), "%s in %uh %um", rising ? "Rise" : "Set",
                            unsigned((soonest - now) / 3600), unsigned((soonest - now) / 60 % 60));
    }
    displayFooter(line);
  }
  if (!inTransition) display.display();
}

static void ICACHE_FLASH_ATTR displayExtra(uint8_t mode) {
  display.clearDisplay();
  const bool portrait = display.width() < 100;
  char line[64];
  const int body = portrait ? 24 : 17;
  if (mode == SCREEN_COUNT) {
    displayText(externalCard.title, 0, portrait ? 24 : 16);
    displayText(externalCard.value, body + 4, portrait ? 56 : 24, 3);
    displayFooter(externalCard.unit, 2);
  } else if (mode == UV_SCREEN) {
    displayText(weather.stale ? "UV today max *" : "UV today max", 0, portrait ? 24 : 8);
    float uv = forecastUV(0);
    int index = int(uv + 0.5f); // international index categories use whole numbers
    if (uv >= 0) snprintf(line, sizeof(line), "%d", index);
    else strcpy(line, "--");
    displayText(line, portrait ? 32 : 12, 24, 3);
    displayText(uv >= 0 ? uvLevel(index) : "No forecast", portrait ? 68 : 38, portrait ? 24 : 8);
    float tomorrow = forecastUV(1);
    if (tomorrow >= 0) snprintf(line, sizeof(line), "Tomorrow %d", int(tomorrow + 0.5f));
    else strcpy(line, "Tomorrow --");
    displayFooter(line);
  } else if (mode == 3) {
    displayText(weather.stale ? "Outdoor *" : "Outdoor", 0, 8);
    snprintf(line, sizeof(line), "%.0f%s", displayTemperature(weather.temperature), temperatureUnit());
    displayText(line, body, 24, 3);
    if (weather.comfortValid) snprintf(line, sizeof(line), "Feels %.0f%s", displayTemperature(weather.feelsLike), temperatureUnit());
    else strcpy(line, "Feels --");
    displayText(line, portrait ? 60 : 44, portrait ? 24 : 8);
    if (weather.humidity >= 0) snprintf(line, sizeof(line), "Humidity %d%%", weather.humidity);
    else strcpy(line, "Humidity --");
    displayFooter(line);
  } else if (mode == 4) {
    displayText(weather.stale ? "Rain *" : "Rain / %", 0, 8);
    uint8_t start = firstHour();
    for (uint8_t row = 0; row < 3 && start + row < forecast.count; ++row) {
      const auto& hour = forecast.hours[start + row];
      unsigned end = (hour.epoch + getTotalOffset(hour.epoch)) / 3600 % 24;
      char chance[8] = "--";
      if (hour.rain >= 0) snprintf(chance, sizeof(chance), "%d%%", hour.rain);
      snprintf(line, sizeof(line), "%02u-%02u", (end + 23) % 24, end);
      if (portrait) {
        displayText(line, body + row * 28, 8);
        displayText(chance, body + row * 28 + 9, 16, 2);
      } else {
        int y = 14 + row * 17;
        display.setTextSize(1); display.setCursor(6, y + 4); display.print(line);
        display.setTextSize(2); display.setCursor(120 - strlen(chance) * 12, y); display.print(chance);
      }
    }
  } else if (mode == 5) {
    snprintf(line, sizeof(line), "Min/max %s%s", temperatureUnit(), weather.stale ? " *" : "");
    displayText(line, 0, portrait ? 16 : 8);
    uint8_t start = firstDay();
    for (uint8_t row = 0; row < 2 && start + row < 2; ++row) {
      const auto& day = forecast.days[start + row];
      if (!day.valid) continue;
      time_t local = day.epoch + getTotalOffset(day.epoch); struct tm* date = gmtime(&local);
      snprintf(line, sizeof(line), "%02d.%02d", date->tm_mday, date->tm_mon + 1);
      if (portrait) displayText(line, body + row * 38, 8);
      else { display.setTextSize(1); display.setCursor(2, 18 + row * 17); display.print(line); }
      snprintf(line, sizeof(line), "%.0f/%.0f", displayTemperature(day.low), displayTemperature(day.high));
      if (portrait) displayText(line, body + 10 + row * 38, 24, 2);
      else {
        uint8_t size = strlen(line) <= 7 ? 2 : 1;
        display.setTextSize(size); display.setCursor(40 + (88 - strlen(line) * 6 * size) / 2, 14 + row * 17);
        display.print(line);
      }
    }
    if (start < 2 && forecast.days[start].uv >= 0) {
      snprintf(line, sizeof(line), "UV max %.1f", forecast.days[start].uv);
      displayFooter(line);
    }
  } else if (mode == 6) {
    displayText(weather.stale ? "Wind *" : "Wind", 0, 8);
    float speed = weather.windspeed;
    const char* unit = "km/h";
    if (featureSettings.wind_unit == 1) { speed /= 3.6f; unit = "m/s"; }
    if (featureSettings.wind_unit == 2) { speed *= 0.621371f; unit = "mph"; }
    snprintf(line, sizeof(line), "%.0f", speed); displayText(line, body, 24, 3);
    const char* directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    snprintf(line, sizeof(line), "%s %s", weather.windDirection < 0 ? "--" : directions[((weather.windDirection + 22) / 45) % 8], unit);
    displayFooter(line, 2);
  }
  if (!inTransition) display.display();
}

static void ICACHE_FLASH_ATTR drawMode(uint8_t mode) {
  switch (mode) {
    case 0: updateDisplay(); break;
    case 1: displayWeather(); break;
    case 2: displaySunTimes(); break;
    default: displayExtra(mode); break;
  }
}

bool ICACHE_FLASH_ATTR controlDisplay(const char* action, int screen) {
  if (!strcmp(action, "resume")) displayPaused = false;
  else if (!strcmp(action, "hold")) displayPaused = true;
  else if (!strcmp(action, "show")) {
    if (screen < 0 || screen >= DISPLAY_MODE_COUNT || !isModeEnabled(uint8_t(screen))) return false;
    displayMode = screen; displayPaused = true;
  } else if (!strcmp(action, "next")) {
    for (uint8_t i = 1; i <= DISPLAY_MODE_COUNT; ++i) {
      uint8_t next = (displayMode + i) % DISPLAY_MODE_COUNT;
      if (isModeEnabled(next)) { displayMode = next; break; }
    }
  } else return false;
  inTransition = false; lastModeSwitch = millis(); invalidateDisplay();
  return true;
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
  bool night = isNightModeActive();
  if (night && !featureSettings.night_action) {
    if (!displaySleeping) display.ssd1306_command(SSD1306_DISPLAYOFF);
    displaySleeping = true;
    inTransition = false;
    return;
  }
  wakeDisplay();
  applyBrightness(night ? featureSettings.night_brightness : config.brightness);
  if (!isModeEnabled(displayMode)) { displayMode = 0; inTransition = false; invalidateDisplay(); }
  if (inTransition && !isModeEnabled(nextDisplayMode)) { inTransition = false; invalidateDisplay(); }
  uint8_t duration = displayMode == UV_SCREEN ? featureSettings.screen_uv_sec :
    displayMode < SCREEN_COUNT ? featureSettings.seconds[displayMode] : 0;
  unsigned long interval = (duration ? duration : config.display_rotation_sec) * 1000UL;

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

    drawMode(currentMode);

    applyDissolveEffect(hidePercent, isDriftPhase);
    return;
  }

  // Check if time to switch modes
  if (!displayPaused && now - lastModeSwitch > interval) {
    uint8_t attempts = 0;
    nextDisplayMode = displayMode;
    do {
      nextDisplayMode = (nextDisplayMode + 1) % DISPLAY_MODE_COUNT;
      attempts++;
      if (attempts >= DISPLAY_MODE_COUNT) {
        nextDisplayMode = 0;
        Serial.println("WARNING: No display mode enabled, forcing time mode");
        break;
      }
    } while (!isModeEnabled(nextDisplayMode));

    if (nextDisplayMode == displayMode) {
      lastModeSwitch = now;
      return;
    }
    inTransition = featureSettings.dissolve;
    if (!inTransition) { displayMode = nextDisplayMode; invalidateDisplay(); }
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
  unsigned long refreshInterval = displayMode == 0 ? 500 : 60000;
  if (!displayDirty && uint32_t(now - lastClockFrame) < refreshInterval) return;
  displayDirty = false;
  lastClockFrame = now;
  drawMode(displayMode);
}

// Check if display mode is enabled
bool ICACHE_FLASH_ATTR isModeEnabled(uint8_t mode) {
  switch(mode) {
    case 0: return true;  // Time always enabled
    case 1: return config.show_weather && weather.valid;
    case 2: return config.show_sunrise_sunset && sunTimes.lastDay != -1;
    case 3: return featureSettings.show_comfort && weather.valid;
    case 4: return featureSettings.show_rain && firstHour() < forecast.count;
    case 5: return featureSettings.show_daily && firstDay() < 2;
    case 6: return featureSettings.show_wind && weather.valid;
    case SCREEN_COUNT: return externalCardActive();
    case UV_SCREEN: return featureSettings.show_uv && forecastUV(0) >= 0;
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
