#include "globals.h"
#include <Updater.h>
#include <FS.h>
#include <flash_hal.h>

// Keep the core flash writer/validation, without its duplicate HTML form,
// Basic-auth UI, debug paths and cross-origin handlers. The PIN is checked on
// every chunk and again before committing. Commit only after the complete POST.
class FirmwareUpdateHandler : public ESP8266WebServer::RequestHandlerType {
  bool started = false, received = false, ended = false, part = false;
  uint8_t tail = 0;
  String failure;

  void stop() {
    // One byte is withheld until handle(): even a full-size interrupted image
    // is incomplete, so end(false) cannot arm the bootloader to install it.
    if (started && Update.isRunning()) Update.end(false);
    started = false;
  }
  void fail(const String& reason) {
    if (!failure.length()) failure = reason;
    stop();
  }
public:
  bool canHandle(HTTPMethod method, const String& uri) override {
    if (method != HTTP_POST || uri != "/update") return false;
    stop(); started = received = ended = part = false; failure = "";
    return true;
  }
  bool canUpload(const String& uri) override { return uri == "/update"; }
  void upload(ESP8266WebServer&, const String&, HTTPUpload& upload) override {
    if (!authenticatedMaintenancePassword()) { fail(F("Incorrect PIN")); return; }
    if (failure.length()) return;
    if (upload.status == UPLOAD_FILE_START) {
      if (part || !upload.filename.length() || (upload.name != "firmware" && upload.name != "filesystem")) {
        fail(F("Choose one firmware or filesystem file")); return;
      }
      part = true;
    } else if (upload.status == UPLOAD_FILE_WRITE && upload.currentSize) {
      if (!part || ended) { fail(F("Invalid upload")); return; }
      if (!started) {
        if (Update.isRunning()) { fail(F("Another update is running")); return; }
        const bool filesystem = upload.name == "filesystem";
        const uint32_t freeSpace = ESP.getFreeSketchSpace();
        const size_t maximum = filesystem ? size_t(FS_end) - size_t(FS_start)
          : freeSpace > 0x1000 ? (freeSpace - 0x1000) & 0xFFFFF000 : 0;
        if (filesystem) close_all_fs();
        if (!Update.begin(maximum, filesystem ? U_FS : U_FLASH)) { fail(Update.getErrorString()); return; }
        started = true;
      }
      if ((received && Update.write(&tail, 1) != 1) ||
          (upload.currentSize > 1 && Update.write(upload.buf, upload.currentSize - 1) != upload.currentSize - 1)) {
        fail(Update.getErrorString()); return;
      }
      tail = upload.buf[upload.currentSize - 1]; received = true;
    } else if (upload.status == UPLOAD_FILE_END) {
      ended = true;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      fail(F("Upload interrupted"));
    }
  }
  bool handle(ESP8266WebServer& web, HTTPMethod, const String&) override {
    if (!requireMaintenanceAuth()) { stop(); return true; }
    if (!received || !ended || failure.length()) {
      stop();
      web.send(400, "text/plain", String(F("Update error: ")) + (failure.length() ? failure : String(F("Choose a non-empty file"))));
      return true;
    }
    if (Update.write(&tail, 1) != 1 || !Update.end(true)) {
      const String error = Update.getErrorString(); stop();
      web.send(200, "text/plain", String(F("Update error: ")) + error); return true;
    }
    started = false;
    web.client().setNoDelay(true);
    web.send(200, "text/plain", F("Update Success! Rebooting..."));
    delay(100); web.client().stop(); ESP.restart();
    return true;
  }
};

void ICACHE_FLASH_ATTR setupWebUpdate() {
  if (!maintenancePassword()) return;
  server.on("/update", HTTP_GET, []() { serveWebUI(); });
  server.addHandler(new FirmwareUpdateHandler());
}
