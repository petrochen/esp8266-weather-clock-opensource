#include <cassert>
#include <iostream>
#include "night_mode.cpp"
unsigned long getAsyncEpoch(){return 1791028800;}
long getTotalOffset(unsigned long){return 0;}

int main() {
  EEPROM.bytes.fill(0xff);
  const auto original = EEPROM.bytes;
  loadFeatureSettings();
  assert(!featureSettings.clock_weather && featureSettings.dissolve && !featureSettings.external_enabled);
  assert(EEPROM.bytes == original && EEPROM.commits == 0);
  const char* error = nullptr;
  JsonDocument doc;
  for (const char* input : {"{\"temperature_unit\":2}", "{\"wind_unit\":3}",
       "{\"screen_clock_sec\":121}", "{\"screen_rain_sec\":-1}", "{\"night_action\":1.5}",
       "{\"night_brightness\":8}", "{\"clock_weather\":1}", "{\"show_rain\":null}",
       "{\"external_enabled\":\"yes\"}", "{\"screen_clock_sec\":\"10junk\"}"}) {
    deserializeJson(doc, input); auto before = featureSettings;
    assert(!updateFeatureSettings(featureSettings, doc.as<JsonObjectConst>(), error));
    assert(!memcmp(&before, &featureSettings, sizeof(before)));
  }
  deserializeJson(doc, R"({"clock_weather":true,"show_rain":true,"dissolve":false,"screen_clock_sec":120,"temperature_unit":1,"wind_unit":2,"night_action":1,"night_brightness":2})");
  assert(updateFeatureSettings(featureSettings, doc.as<JsonObjectConst>(), error));
  doc.clear(); exportFeatureSettings(featureSettings, doc);
  FeatureSettings roundtrip;
  assert(updateFeatureSettings(roundtrip, doc.as<JsonObjectConst>(), error));
  assert(!memcmp(&roundtrip, &featureSettings, sizeof(roundtrip)));
  assert(doc["clock_weather"].is<bool>() && doc["temperature_unit"].is<int>());
  EEPROM.bytes.fill(0x42); const auto beforeSave = EEPROM.bytes;
  assert(saveSettings(config, nightSettings, &featureSettings));
  for (size_t i = sizeof(Config); i < EEPROM.bytes.size(); ++i)
    if (!(i >= NIGHT_SETTINGS_ADDR && i < NIGHT_SETTINGS_ADDR + sizeof(NightSettings)) &&
        !(i >= FEATURE_SETTINGS_ADDR && i < FEATURE_SETTINGS_ADDR + sizeof(FeatureSettings)))
      assert(EEPROM.bytes[i] == beforeSave[i]);
  featureSettings = FeatureSettings(); loadFeatureSettings();
  assert(featureSettings.clock_weather && featureSettings.seconds[0] == 120 && !featureSettings.dissolve);
  FeatureSettings broken; broken.night_brightness = 8;
  EEPROM.put(FEATURE_SETTINGS_ADDR, broken); loadFeatureSettings(); assert(featureSettings.night_brightness == 0);
  broken = FeatureSettings(); broken.seconds[6] = 255;
  EEPROM.put(FEATURE_SETTINGS_ADDR, broken); loadFeatureSettings(); assert(featureSettings.seconds[6] == 0);

  ExternalCard card;
  deserializeJson(doc, R"({"title":"Living room","value":"920","unit":"ppm","ttl":60})");
  assert(updateExternalCard(card, doc.as<JsonObjectConst>(), UINT32_MAX - 1000, error));
  assert(card.ttl == 60000 && !strcmp(card.value, "920"));
  for (const char* bad : {R"({"ttl":-1})", R"({"ttl":3601})", R"({"ttl":1})",
       R"({"title":"Room","value":"1234567890123456","ttl":60})",
       R"({"title":"Room","value":"42\n","ttl":60})", R"({"title":"Room","value":42,"ttl":60})"}) {
    deserializeJson(doc, bad); auto before = card;
    assert(!updateExternalCard(card, doc.as<JsonObjectConst>(), 200, error));
    assert(!memcmp(&before, &card, sizeof(card)));
  }
  deserializeJson(doc, R"({"ttl":0})");
  assert(updateExternalCard(card, doc.as<JsonObjectConst>(), 200, error) && !card.ttl);
  std::cout << "PASS: feature defaults/migration/isolation/strict transactional validation/round-trip and bounded card fields/TTL\n";
}
