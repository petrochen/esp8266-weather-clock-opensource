#include "globals.h"
#include "settings.h"
#include <EEPROM.h>

NightSettings nightSettings;
static_assert(NIGHT_SETTINGS_ADDR + sizeof(NightSettings) <= RESET_COUNTER_ADDR,
              "Night settings overlap reset counter");

void ICACHE_FLASH_ATTR loadNightSettings() {
  NightSettings saved;
  EEPROM.begin(512);
  EEPROM.get(NIGHT_SETTINGS_ADDR, saved);
  EEPROM.end();
  if (saved.magic != NIGHT_SETTINGS_MAGIC || saved.enabled > 1 ||
      saved.start_hour > 23 || saved.end_hour > 23 ||
      saved.start_minute > 59 || saved.end_minute > 59) saved = NightSettings();
  nightSettings = saved;
}

bool ICACHE_FLASH_ATTR saveSettings(const Config& next, const NightSettings& night, const FeatureSettings* features) {
  EEPROM.begin(512);
  EEPROM.put(0, next);
  EEPROM.put(NIGHT_SETTINGS_ADDR, night);
  if (features) EEPROM.put(FEATURE_SETTINGS_ADDR, *features);
  // end() commits once, releases the buffer and reports the actual result.
  return EEPROM.end();
}

void ICACHE_FLASH_ATTR loadFeatureSettings() {
  static_assert(NIGHT_SETTINGS_ADDR + sizeof(NightSettings) <= FEATURE_SETTINGS_ADDR, "Feature/night overlap");
  static_assert(FEATURE_SETTINGS_ADDR + sizeof(FeatureSettings) <= RESET_COUNTER_ADDR, "Feature/reset overlap");
  FeatureSettings saved;
  EEPROM.begin(512); EEPROM.get(FEATURE_SETTINGS_ADDR, saved); EEPROM.end();
  bool valid = saved.magic == FEATURE_SETTINGS_MAGIC;
  for (uint8_t duration : saved.seconds) valid = valid && duration <= 120;
  valid = valid && saved.clock_weather <= 1 && saved.dissolve <= 1 && saved.temperature_unit <= 1 &&
    saved.wind_unit <= 2 && saved.night_action <= 1 && saved.night_brightness <= 7 &&
    saved.external_enabled <= 1 && saved.sun_countdown <= 1 && saved.show_comfort <= 1 &&
    saved.show_rain <= 1 && saved.show_daily <= 1 && saved.show_wind <= 1 &&
    saved.show_uv <= 1 && saved.screen_uv_sec <= 120;
  featureSettings = valid ? saved : FeatureSettings();
}

bool ICACHE_FLASH_ATTR isNightModeActive() {
  unsigned long epoch = getAsyncEpoch();
  return nightModeAt(nightSettings, epoch + getTotalOffset(epoch), timeIsSynced);
}
