#pragma once
#include "Arduino.h"
class AsyncHTTPRequest {
public:
  using Callback = void(*)(void*, AsyncHTTPRequest*, int);
  Callback callback = nullptr;
  bool openResult = true, sendResult = true, synchronous = false;
  int sends = 0, aborts = 0, code = 200;
  String payload;
  void onReadyStateChange(Callback cb) { callback = cb; }
  void setTimeout(int) {}
  bool open(const char*, const char*) { return openResult; }
  bool send() { ++sends; if (synchronous) complete(); return sendResult; }
  void abort() { ++aborts; if (callback) callback(nullptr, this, 4); }
  int responseHTTPcode() { return code; }
  String responseText() { return payload; }
  void complete() { callback(nullptr, this, 4); }
};
