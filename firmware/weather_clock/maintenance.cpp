#include "globals.h"
#include <EEPROM.h>

// Separate record: upgrading keeps the legacy Config structure byte-for-byte.
static const uint16_t MAINTENANCE_ADDR = 384;
// New magic migrates the long codes in 1.9.11/12 to a PIN exactly once.
static const uint32_t MAINTENANCE_MAGIC = 0x50494E36;
struct MaintenanceCredentials {
  uint32_t magic;
  char password[13];  // Keep the record size; six digits and a terminator now.
};
static MaintenanceCredentials credentials;
static char formattedPIN[8];
static bool credentialsReady = false;
static bool showingCode = false;
static unsigned long codeShownAt = 0;
static const unsigned long PIN_DISPLAY_MS = 30000UL;
static_assert(sizeof(Config) <= MAINTENANCE_ADDR, "Config overlaps maintenance credentials");
static_assert(MAINTENANCE_ADDR + sizeof(credentials) <= RESET_COUNTER_ADDR, "Credentials overlap reset counter");
static_assert(MAINTENANCE_ADDR + sizeof(credentials) <= NIGHT_SETTINGS_ADDR, "Credentials overlap night settings");

bool ICACHE_FLASH_ATTR setupMaintenance() {
  credentialsReady = false;
  EEPROM.begin(512);
  EEPROM.get(MAINTENANCE_ADDR, credentials);
  bool valid = credentials.magic == MAINTENANCE_MAGIC && credentials.password[6] == '\0';
  for (size_t i = 0; valid && i < 6; ++i) {
    valid = credentials.password[i] >= '0' && credentials.password[i] <= '9';
  }
  if (!valid) {
    credentials = {};
    credentials.magic = MAINTENANCE_MAGIC;
    // Reject the incomplete final bucket for a uniform six-digit random PIN.
    // Do not derive it from a publicly observable MAC address or chip ID.
    uint32_t value;
    do { value = ESP.random(); } while (value >= 4294000000UL);
    snprintf(credentials.password, sizeof(credentials.password), "%06lu",
             static_cast<unsigned long>(value % 1000000UL));
    EEPROM.put(MAINTENANCE_ADDR, credentials);
    if (!EEPROM.commit()) {
      EEPROM.end();
      credentialsReady = false;
      return false;  // Do not enable OTA with an unpersisted/empty password.
    }
  }
  EEPROM.end();
  snprintf(formattedPIN, sizeof(formattedPIN), "%.3s-%.3s",
           credentials.password, credentials.password + 3);
  credentialsReady = true;
  return true;
}

const char* ICACHE_FLASH_ATTR maintenancePassword() {
  return credentialsReady ? credentials.password : nullptr;
}

const char* ICACHE_FLASH_ATTR authenticatedMaintenancePassword() {
  if (!credentialsReady) return nullptr;
  const String authorization = server.header("Authorization");
  const char* text = authorization.c_str();
  if (!strncmp(text, "Bearer ", 7)) {
    return !strcmp(text + 7, credentials.password) || !strcmp(text + 7, formattedPIN)
      ? credentials.password : nullptr;
  }
  // Compatibility with existing scripts; the web UI only uses a PIN.
  if (server.authenticate("admin", credentials.password)) return credentials.password;
  if (server.authenticate("admin", formattedPIN)) return formattedPIN;
  return nullptr;
}

bool ICACHE_FLASH_ATTR requireMaintenanceAuth() {
  if (!credentialsReady) {
    server.send(503, "text/plain", F("Maintenance credentials unavailable; restart the clock."));
    return false;
  }
  if (authenticatedMaintenancePassword()) return true;
  server.send(401, "application/json", F("{\"error\":\"Incorrect PIN. Read the six digits on your clock.\"}"));
  return false;
}

void ICACHE_FLASH_ATTR showMaintenanceCode() {
  if (!credentialsReady) return;
  wakeDisplay();
  // Keep the entire code readable even when normal screens use portrait rotation.
  display.setRotation(config.display_orientation & 2);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Update / reset PIN"));
  display.setTextSize(2);
  display.setCursor(22, 26);
  display.println(formattedPIN);
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.println(F("Shown for 30 seconds"));
  display.display();
  codeShownAt = millis();
  showingCode = true;
}

void ICACHE_FLASH_ATTR handleShowMaintenancePIN() {
  if (!credentialsReady) {
    server.send(503, "text/plain", F("Maintenance PIN unavailable; restart the clock."));
    return;
  }
  // No login is needed to show the PIN physically. Never include it in HTTP.
  showMaintenanceCode();
  server.send(200, "application/json", F("{\"status\":\"shown\",\"seconds\":30}"));
}

bool ICACHE_FLASH_ATTR maintenanceCodeVisible() {
  if (showingCode && uint32_t(millis() - codeShownAt) >= PIN_DISPLAY_MS) {
    showingCode = false;
    display.setRotation(config.display_orientation);
    lastModeSwitch = millis();
    invalidateDisplay();
  }
  return showingCode;
}
