#pragma once
#include "Arduino.h"
#include <functional>
#include <vector>
#define PROGMEM
enum HTTPMethod { HTTP_GET, HTTP_POST };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String filename, name;
  size_t currentSize = 0;
  uint8_t buf[1024] = {};
};
class ESP8266WebServer;
class RequestHandlerMock {
  RequestHandlerMock* following = nullptr;
public:
  virtual ~RequestHandlerMock() = default;
  RequestHandlerMock* next(){return following;}
  void next(RequestHandlerMock* h){following=h;}
  virtual bool canHandle(HTTPMethod,const String&){return false;}
  virtual bool canUpload(const String&){return false;}
  virtual void upload(ESP8266WebServer&,const String&,HTTPUpload&){}
  virtual bool handle(ESP8266WebServer&,HTTPMethod,const String&){return false;}
};
class ESP8266WebServer {
public:
  using RequestHandlerType = RequestHandlerMock;
  int code = 0;
  bool authenticated = true;
  String suppliedPassword = "123456";
  std::function<void()> get;
  String body;
  std::vector<std::string> registration;
  RequestHandlerType* registeredGuard=nullptr;
  bool authenticate(const char* user,const char* password){return authenticated && String(user)=="admin" && suppliedPassword==password;}
  void send(int status,const char*,const String& text){code=status;body=text;}
  struct Client { void setNoDelay(bool){} void stop(){} } clientInstance;
  Client& client(){return clientInstance;}
  void send_P(int status,const char* type,const char* text){send(status,type,text);}
  void on(const char*,HTTPMethod,std::function<void()> handler){get=handler;registration.push_back("page");}
  void addHandler(RequestHandlerType* handler){registeredGuard=handler;registration.push_back("guard");}
} inline server;
struct ESPMock {
  size_t freeSketchSpace = 0x3000;
  int restarts = 0;
  size_t getFreeSketchSpace(){return freeSketchSpace;}
  void restart(){++restarts;}
} inline ESP;
inline void delay(unsigned long){}
inline const char* maintenancePassword(){return "123456";}
inline const char* authenticatedMaintenancePassword(){
  for(const char* spelling:{"123456","123-456"})if(server.authenticate("admin",spelling))return spelling;
  return nullptr;
}
inline bool requireMaintenanceAuth(){if(authenticatedMaintenancePassword())return true;server.code=401;return false;}

inline void serveWebUI(){server.send(200,"text/html","static UI");}
