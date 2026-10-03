#include "globals.h"

// Intercept only POST /update, before the core updater's handlers. Actual flash
// validation/writing stays in ESP8266HTTPUpdateServer. Delay its START callback
// until the first non-empty chunk so missing/zero-byte files cannot begin OTA.
class NonEmptyUpdateHandler : public ESP8266WebServer::RequestHandlerType {
  bool started = false;
  bool received = false;
  bool aborted = false;

  ESP8266WebServer::RequestHandlerType* updater(const String& uri) {
    for (auto* handler = next(); handler; handler = handler->next()) {
      if (handler->canHandle(HTTP_POST, uri)) return handler;
    }
    return nullptr;
  }
public:
  bool canHandle(HTTPMethod method, const String& uri) override {
    if (method != HTTP_POST || uri != "/update") return false;
    // The web server selects one handler at the start of each request, before
    // parsing multipart data. Reset even if this request has no file parts.
    started = received = aborted = false;
    return true;
  }
  bool canUpload(const String& uri) override { return uri == "/update"; }

  void upload(ESP8266WebServer& web, const String& uri, HTTPUpload& upload) override {
    if (!authenticatedMaintenancePassword()) return;
    auto* handler = updater(uri);
    if (!handler) return;
    if (upload.status == UPLOAD_FILE_START) {
      started = received = aborted = false;
    } else if (upload.status == UPLOAD_FILE_WRITE && upload.currentSize && upload.filename.length()) {
      if (!started) {
        upload.status = UPLOAD_FILE_START;
        handler->upload(web, uri, upload);
        upload.status = UPLOAD_FILE_WRITE;
        started = true;
      }
      received = true;
      handler->upload(web, uri, upload);
    } else if (started && (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED)) {
      aborted = upload.status == UPLOAD_FILE_ABORTED;
      handler->upload(web, uri, upload);
    }
  }

  bool handle(ESP8266WebServer& web, HTTPMethod method, const String& uri) override {
    if (!requireMaintenanceAuth()) return true;
    if (!received || aborted) {
      web.send(400, "text/html",
        F("<!DOCTYPE html><meta charset='utf-8'><h1>Update not started</h1>"
          "<p>Choose a non-empty firmware or filesystem file and try again.</p>"
          "<p><a href='/update'>Back to update</a></p>"));
      return true;
    }
    auto* handler = updater(uri);
    if (handler) return handler->handle(web, method, uri);
    web.send(503, "text/plain", "Updater unavailable");
    return true;
  }
};

void ICACHE_FLASH_ATTR setupWebUpdate() {
  if (!maintenancePassword()) return;
  // Registration order matters: custom page/guard first, stock updater second.
  server.on("/update", HTTP_GET, []() {
    serveWebUI();
  });
  server.addHandler(new NonEmptyUpdateHandler());
  // The outer handler authenticates every chunk and final POST. The core owns
  // flash validation/writing only; it does not implement PIN Bearer auth.
  httpUpdater.setup(&server, "/update");
}
