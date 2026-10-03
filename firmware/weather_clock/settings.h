#ifndef SETTINGS_H
#define SETTINGS_H
#include "config.h"
#include <ArduinoJson.h>

// Apply a partial form/JSON update to a copy. Caller commits only on success.
bool updateSettings(Config& target, JsonObjectConst values, const char*& error);
void exportSettings(const Config& source, JsonDocument& doc);
bool updateNightSettings(NightSettings& target, JsonObjectConst values, const char*& error);
void exportNightSettings(const NightSettings& source, JsonDocument& doc);
void loadNightSettings();
bool saveSettings(const Config& next, const NightSettings& night, const FeatureSettings* features = nullptr);
bool nightModeAt(const NightSettings& night, unsigned long localEpoch, bool synced);
bool updateFeatureSettings(FeatureSettings& target, JsonObjectConst values, const char*& error);
void exportFeatureSettings(const FeatureSettings& source, JsonDocument& doc);
void loadFeatureSettings();
bool updateExternalCard(ExternalCard& target, JsonObjectConst values, uint32_t now, const char*& error);
#endif
