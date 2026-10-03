#include <cassert>
#include <iostream>
#include <vector>
#include "update_server.cpp"
struct StockUpdater : RequestHandlerMock {
  std::vector<HTTPUploadStatus> events;
  int finished = 0;
  bool canHandle(HTTPMethod method,const String& uri) override{return method==HTTP_POST && uri=="/update";}
  void upload(ESP8266WebServer& web,const String&,HTTPUpload& upload) override {
    (void)web;
    assert(authenticatedMaintenancePassword()); // outer guard must authorize every delegation
    events.push_back(upload.status);
  }
  bool handle(ESP8266WebServer&,HTTPMethod,const String&) override {++finished;return true;}
};
int main(){
  NonEmptyUpdateHandler guard;
  StockUpdater stock;guard.next(&stock);
  auto start=[&](){server.code=0;assert(guard.canHandle(HTTP_POST,"/update"));};
  auto finish=[&](){assert(guard.handle(server,HTTP_POST,"/update"));};
  HTTPUpload upload;
  start();finish();assert(server.code==400 && stock.events.empty() && stock.finished==0);
  for(const char* field:{"firmware","filesystem"}) {
    for(const char* spelling:{"123456","123-456"}) {
    server.suppliedPassword=spelling;
    for(const char* filename:{"","empty.bin"}) {
      start();upload.name=field;upload.filename=filename;
      upload.status=UPLOAD_FILE_START;guard.upload(server,"/update",upload);
      upload.status=UPLOAD_FILE_WRITE;upload.currentSize=0;guard.upload(server,"/update",upload);
      upload.status=UPLOAD_FILE_END;guard.upload(server,"/update",upload);finish();
      assert(server.code==400 && stock.events.empty() && stock.finished==0);
    }
    start();upload.name=field;upload.filename="valid.bin";
    upload.status=UPLOAD_FILE_START;guard.upload(server,"/update",upload);
    assert(stock.events.empty());
    upload.status=UPLOAD_FILE_WRITE;upload.currentSize=1024;guard.upload(server,"/update",upload);
    assert(upload.status==UPLOAD_FILE_WRITE);
    upload.status=UPLOAD_FILE_END;guard.upload(server,"/update",upload);finish();
    assert((stock.events==std::vector<HTTPUploadStatus>{UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END}));
    assert(stock.finished==1);stock.events.clear();stock.finished=0;
    // Missing multipart data following an earlier request must not reuse state.
    start();finish();assert(server.code==400 && stock.finished==0);
    }
  }
  start();upload.status=UPLOAD_FILE_START;guard.upload(server,"/update",upload);
  upload.status=UPLOAD_FILE_WRITE;guard.upload(server,"/update",upload);
  upload.status=UPLOAD_FILE_ABORTED;guard.upload(server,"/update",upload);finish();
  assert(server.code==400 && stock.finished==0 && stock.events.back()==UPLOAD_FILE_ABORTED);
  stock.events.clear();
  int changes=httpUpdater.credentialUpdates;
  for(const char* wrong:{"123-457","12345","123 456",""}){
    server.suppliedPassword=wrong;start();upload.status=UPLOAD_FILE_WRITE;
    guard.upload(server,"/update",upload);finish();
    assert(server.code==401 && stock.events.empty() && stock.finished==0);
    assert(httpUpdater.credentialUpdates==changes);
  }
  setupWebUpdate();
  assert((server.registration==std::vector<std::string>{"page","guard","core"}));
  assert(dynamic_cast<NonEmptyUpdateHandler*>(server.registeredGuard));
  assert(httpUpdater.username.empty() && httpUpdater.password.empty());
  server.registeredGuard->next(&stock);
  server.suppliedPassword="wrong";
  assert(server.registeredGuard->canHandle(HTTP_POST,"/update"));
  server.registeredGuard->upload(server,"/update",upload);
  server.registeredGuard->handle(server,HTTP_POST,"/update");
  assert(server.code==401 && stock.events.empty() && stock.finished==0);
  server.suppliedPassword="wrong";server.get();assert(server.code==200);
  server.suppliedPassword="123-456";server.get();assert(server.code==200);
  assert(!guard.canHandle(HTTP_GET,"/update") && !guard.canHandle(HTTP_POST,"/config"));
  delete server.registeredGuard;
  std::cout<<"PASS: missing/empty uploads, firmware/filesystem delegation with both PIN formats, outer authentication and registration order, aborts, per-request reset, unauthorized upload rejected, public PIN page\n";
}
