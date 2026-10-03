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

bool ICACHE_FLASH_ATTR saveSettings(const Config& next, const NightSettings& night) {
  EEPROM.begin(512);
  EEPROM.put(0, next);
  EEPROM.put(NIGHT_SETTINGS_ADDR, night);
  // end() commits once, releases the buffer and reports the actual result.
  return EEPROM.end();
}

bool ICACHE_FLASH_ATTR isNightModeActive() {
  unsigned long epoch = getAsyncEpoch();
  return nightModeAt(nightSettings, epoch + getTotalOffset(epoch), timeIsSynced);
}
