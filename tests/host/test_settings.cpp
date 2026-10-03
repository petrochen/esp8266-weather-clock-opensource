#include "settings.h"
#include <cassert>
#include <iostream>
#include <string>

static bool apply(Config& config, const char* json) {
  JsonDocument doc;
  assert(!deserializeJson(doc, json));
  const char* error = nullptr;
  return updateSettings(config, doc.as<JsonObjectConst>(), error);
}
int main() {
  // HTTP endpoints share the C-string reader. Preserve String-reader semantics
  // for nesting limits, escaped text, Unicode and malformed request bodies.
  for (const std::string input : {R"({"action":"show","screen":8})", R"({"title":"Portim\u00e3o","value":"a\"b"})",
       R"({"a":{"b":{"c":1}}})", R"({"action":)", "[]", "", R"({"value":"a\u0000b"})"}) {
    JsonDocument previous, shared;
    auto before = deserializeJson(previous, input, DeserializationOption::NestingLimit(2));
    auto after = deserializeJson(shared, input.c_str(), DeserializationOption::NestingLimit(2));
    assert(before == after && previous.is<JsonObject>() == shared.is<JsonObject>());
    std::string left, right;
    serializeJson(previous, left); serializeJson(shared, right);
    assert(left == right);
  }
  Config c;
  strcpy(c.ssid, "home");
  strcpy(c.password, "test-only-password");
  // Rejected partial updates never change earlier fields.
  assert(!apply(c, "{\"ssid\":\"new\",\"hostname\":\"\"}"));
  assert(!strcmp(c.ssid, "home"));
  assert(!apply(c, "{\"latitude\":\"NaN\"}"));
  assert(!apply(c, "{\"latitude\":\"Infinity\"}"));
  assert(!apply(c, "{\"ntp_interval\":\"60garbage\"}"));
  assert(!apply(c, "{\"brightness\":null}"));
  assert(!apply(c, "{\"ssid\":\"a\\u0000b\"}"));
  assert(!apply(c, "[]"));
  assert(apply(c, "{ \"password\": \"\", \"ntp_interval\": 0, \"weather_interval\": 999999 }"));
  assert(!strcmp(c.password, "test-only-password"));
  assert(c.ntp_interval == 60 && c.weather_interval == 86400);
  assert(apply(c, "{\"ssid\":\"AAAA\",\"timezone_offset\":50400,\"dst_enabled\":false,\"display_orientation\":3}"));
  assert(c.timezone_offset == 50400 && !c.dst_enabled && c.display_orientation == 3);
  assert(apply(c, "{\"city_name\":\"a\\\"b\\\\c\"}"));
  // The legacy city field holds 31 UTF-8 bytes, not 31 Unicode characters.
  const std::string city31(31, 'W');
  assert(apply(c, ("{\"city_name\":\"" + city31 + "\"}").c_str()));
  for (size_t length : {32U, 55U}) {
    assert(!apply(c, ("{\"ssid\":\"changed\",\"city_name\":\"" + std::string(length, 'W') + "\"}").c_str()));
    assert(!strcmp(c.city_name, city31.c_str()) && !strcmp(c.ssid, "AAAA"));
  }
  assert(apply(c, "{\"city_name\":\"Санкт-Петербург\"}"));
  assert(!apply(c, "{\"city_name\":\"АБВГДЕЖЗИЙКЛМНОП\"}")); // 16 letters = 32 bytes
  assert(!strcmp(c.city_name, "Санкт-Петербург"));
  JsonDocument exported;
  exportSettings(c, exported);
  assert(exported["password"].isUnbound());
  std::string json;
  serializeJson(exported, json);
  Config restored;
  strcpy(restored.password, "other-device-secret");
  assert(apply(restored, json.c_str()));
  assert(!strcmp(restored.city_name, c.city_name));
  assert(restored.display_orientation == 3 && restored.timezone_offset == 50400);
  assert(!strcmp(restored.password, "other-device-secret"));
  assert(apply(c, "{\"clear_password\":true}"));
  assert(!*c.password);
  std::cout << "PASS: transactional settings, strict numbers, bounds, escaped JSON, secret-free round-trip\n";

  RetryConfig retry;
  assert(retry.scheduleRetry(UINT32_MAX - 999)); // deadline exactly zero
  assert(retry.pending && retry.nextRetryTime == 0);
  assert(!retry.isRetryTime(UINT32_MAX));
  assert(retry.isRetryTime(0));
  retry.consume();
  assert(!retry.isRetryTime(1));
  assert(retry.scheduleRetry(100));
  assert(!retry.isRetryTime(2099) && retry.isRetryTime(2100));
  retry.consume();
  assert(retry.scheduleRetry(2100));
  assert(retry.isRetryTime(6100));
  retry.consume();
  assert(!retry.scheduleRetry(6100));
  retry.reset();
  assert(!retry.pending && retry.currentRetry == 0);

  bool seen[8192] = {};
  unsigned hidden = 0;
  for (uint16_t i=0; i<8192; ++i) {
    auto rank = dissolveRank(i);
    assert(!seen[rank]); seen[rank] = true;
    if (rank < 4096) ++hidden;
  }
  assert(hidden == 4096);
  std::cout << "PASS: retry exhaustion/consumption/rollover and exact dissolve coverage\n";
}
