/*
 * wifi_manager.cpp - WiFi connection management
 * TJ-56-654 Weather Clock
 */

#include "globals.h"
#include <WiFiManager.h>

// Async WiFi - Process connection (call in loop)
void ICACHE_FLASH_ATTR processWiFiConnection() {
  if (wifiConnState != WIFI_CONN_CONNECTING) return;

  // Check connection status
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnState = WIFI_CONN_CONNECTED;
    invalidateDisplay();
    wifiRetry.reset();
    internetConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Disable fallback AP if it was enabled
    if (WiFi.getMode() == WIFI_AP_STA) {
      Serial.println("Disabling fallback AP (back to STA mode)");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
    }

    // Sync connected SSID + password to config
    if (strlen(config.ssid) == 0) {
      safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
      safeStringCopy(WiFi.psk(), config.password, sizeof(config.password));
      saveConfig();
    }

    showIP();
    return;
  }

  // Check timeout for this attempt
  if (millis() - wifiConnectStart > WIFI_TIMEOUT_MS) {
    wifiRetry.scheduleRetry();
    unsigned long nextRetryMs = wifiRetry.getBackoffDelay();

    Serial.printf("\nWiFi connection failed. Retry in %lu seconds\n", nextRetryMs / 1000);

    wifiConnState = WIFI_CONN_FAILED;
    invalidateDisplay();
    internetConnected = false;

    showNoWiFi(nextRetryMs / 1000);
    return;
  }

  // Still connecting
  static unsigned long lastDot = 0;
  if (millis() - lastDot > 500) {
    Serial.print(".");
    lastDot = millis();
  }
}

// WiFi setup (SYNCHRONOUS in setup(), async reconnect in loop())
void ICACHE_FLASH_ATTR setupWiFi() {
  Serial.println("WiFi Setup - Synchronous for initial connection");

  WiFi.hostname(config.hostname);
  WiFi.mode(WIFI_STA);

  // Try 1: Use WiFi.begin() without params - only when no SSID is configured.
  // Skipped if user has a saved SSID: SDK-cached credentials may include open
  // networks (e.g. public hotspots) that would be preferred over the user's
  // network, and a successful connect would overwrite config.ssid (issue #3).
  if (strlen(config.ssid) == 0) {
    Serial.println("No SSID configured, trying SDK-stored credentials...");
    WiFi.begin();

    // SYNCHRONOUS wait for connection (max 10 seconds)
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      showWiFiConnecting(attempts);
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("Gateway: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: ");
      Serial.println(WiFi.dnsIP());

      // Persist the password too, not just the SSID. The SDK keeps the
      // password in its own flash area, but every later boot reads
      // config.password: an empty one is treated as an open network
      // (WiFi.begin(ssid)) and fails with WL_WRONG_PASSWORD (issue #12).
      safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
      safeStringCopy(WiFi.psk(), config.password, sizeof(config.password));
      saveConfig();

      showIP();
      wifiConnState = WIFI_CONN_CONNECTED;
      invalidateDisplay();
      return;
    }
  }

  // Try 2: If we have EEPROM credentials, try those (M2: support open networks)
  if (strlen(config.ssid) > 0) {
    Serial.println("\nTrying EEPROM credentials...");
    if (strlen(config.password) > 0) {
      WiFi.begin(config.ssid, config.password);
    } else {
      WiFi.begin(config.ssid);  // open network — no password
    }

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      showWiFiConnecting(attempts);
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected via EEPROM credentials!");
      showIP();
      wifiConnState = WIFI_CONN_CONNECTED;
      invalidateDisplay();
      return;
    }
  }

  // No stored credentials - use WiFiManager
  if (strlen(config.ssid) == 0) {
    Serial.println("\nNo saved credentials, using WiFiManager...");
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);

    // Show AP mode indicator
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(25, 15);
    display.print("Setup Mode");
    display.setTextSize(1);
    display.setCursor(10, 35);
    display.print("Connect to WiFi:");
    display.setCursor(10, 48);
    display.print("TJ56654-Setup");
    display.display();

    Serial.println("Attempting WiFiManager auto-connect...");
    if (!wifiManager.autoConnect("TJ56654-Setup", "12345678")) {
      Serial.println("WiFi connection failed. Starting fallback AP...");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("TJ56654-Clock", "12345678");
      Serial.print("Fallback AP IP: ");
      Serial.println(WiFi.softAPIP());
      wifiConnState = WIFI_CONN_CONNECTED;
      invalidateDisplay();
      return;
    }

    Serial.println("WiFi connected via WiFiManager!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Save both SSID and password (issue #12): WiFiManager stores them in the
    // SDK flash, but the next boot connects from config.* only.
    safeStringCopy(WiFi.SSID(), config.ssid, sizeof(config.ssid));
    safeStringCopy(WiFi.psk(), config.password, sizeof(config.password));
    saveConfig();

    showIP();
    wifiConnState = WIFI_CONN_CONNECTED;
    invalidateDisplay();
    return;
  }

  // We have credentials but WiFi is not available
  Serial.println("\nWiFi not available. Will retry in background.");
  wifiConnState = WIFI_CONN_FAILED;
  wifiRetry.scheduleRetry();
  showNoWiFi(wifiRetry.getBackoffDelay() / 1000);
}
