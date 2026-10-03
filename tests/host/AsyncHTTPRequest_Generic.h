#pragma once
#include "Arduino.h"
class AsyncHTTPRequest {
public:
  using Callback = void(*)(void*, AsyncHTTPRequest*, int);
  Callback callback = nullptr;
  bool openResult = true, sendResult = true, synchronous = false;
  int sends = 0, aborts = 0, code = 200;
  String payload;
  String url;
  void onReadyStateChange(Callback cb) { callback = cb; }
  void setTimeout(int) {}
  bool open(const char*, const char* target) { url = target; return openResult; }
  bool send() { ++sends; if (synchronous) complete(); return sendResult; }
  void abort() { ++aborts; if (callback) callback(nullptr, this, 4); }
  int responseHTTPcode() { return code; }
  String responseText() { return payload; }
  size_t available() { return payload.size(); }
  size_t responseLength() { return payload.size(); }
  void (*dataCallback)(void*, AsyncHTTPRequest*, size_t) = nullptr;
  void onData(void (*cb)(void*, AsyncHTTPRequest*, size_t)) { dataCallback = cb; }
  void complete() { callback(nullptr, this, 4); }
};
