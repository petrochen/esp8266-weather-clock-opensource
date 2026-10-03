#include <cassert>
#include <iostream>
#include "update_server.cpp"
int main(){
  FirmwareUpdateHandler guard;
  HTTPUpload upload;
  auto start=[&](){server.code=0; assert(guard.canHandle(HTTP_POST,"/update"));};
  auto event=[&](HTTPUploadStatus status,size_t count=0){upload.status=status;upload.currentSize=count;guard.upload(server,"/update",upload);};
  auto finish=[&](){assert(guard.handle(server,HTTP_POST,"/update"));};
  auto reset=[&](){start();Update=UpdateMock();ESP.restarts=0;};
  upload.filename="image.bin"; upload.name="firmware";
  start(); finish();assert(server.code==400 && Update.begins==0);
  for(const char* field:{"firmware","filesystem"}) for(const char* pin:{"123456","123-456"}) {
    server.suppliedPassword=pin;upload.name=field;
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_END);finish();
    assert(server.code==400 && Update.begins==0);
    reset();event(UPLOAD_FILE_START);
    for(int chunk=0;chunk<3;++chunk){
      for(int i=0;i<1024;++i)upload.buf[i]=(chunk*17+i)&255;
      event(UPLOAD_FILE_WRITE,1024);
    }
    assert(Update.written.size()==3071 && Update.commits==0);
    event(UPLOAD_FILE_END);assert(Update.commits==0);finish();
    assert(server.code==200 && Update.commits==1 && ESP.restarts==1);
    assert(Update.command==(std::string(field)=="filesystem"?U_FS:U_FLASH));
    assert(Update.written.size()==3072);
    for(int i=0;i<3072;++i)assert(Update.written[i]==((i/1024*17+i%1024)&255));
    start();finish();assert(server.code==400 && Update.commits==1);
  }
  upload.name="firmware";server.suppliedPassword="123456";
  reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,1);event(UPLOAD_FILE_END);finish();
  assert(Update.written.size()==1 && Update.commits==1 && ESP.restarts==1);
  for(int bytes:{1,8192}) {
    reset();event(UPLOAD_FILE_START);
    for(int remaining=bytes;remaining>0;){int chunk=remaining>1024?1024:remaining;event(UPLOAD_FILE_WRITE,chunk);remaining-=chunk;}
    assert(Update.written.size()==size_t(bytes-1));
    event(UPLOAD_FILE_ABORTED);finish();assert(server.code==400 && Update.commits==0 && !Update.running);
  }
  for(int scenario=0;scenario<7;++scenario){
    reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);
    if(scenario==0)event(UPLOAD_FILE_START); // multiple files
    if(scenario==1)event(UPLOAD_FILE_ABORTED);
    if(scenario==2){server.suppliedPassword="wrong";event(UPLOAD_FILE_WRITE,10);}
    if(scenario==3){Update.failWrite=true;event(UPLOAD_FILE_WRITE,10);}
    if(scenario==4){Update.failEnd=true;event(UPLOAD_FILE_END);}
    if(scenario==5){event(UPLOAD_FILE_END);server.suppliedPassword="wrong";}
    if(scenario==6){} // no END callback / incomplete multipart
    finish();assert(Update.commits==0 && ESP.restarts==0 && !Update.running);
    server.suppliedPassword="123456";
  }
  for(const char* wrong:{"", "123-457", "12345", "123 456"}){
    reset();server.suppliedPassword=wrong;event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);event(UPLOAD_FILE_END);finish();
    assert(server.code==401 && Update.begins==0 && Update.commits==0);
  }
  server.suppliedPassword="123456";
  reset();Update.failBegin=true;event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);event(UPLOAD_FILE_END);finish();
  assert(Update.commits==0 && ESP.restarts==0);
  reset();Update.running=true;event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);finish();
  assert(Update.begins==0 && Update.running);Update.running=false; // do not abort another OTA transport
  reset();upload.name="unknown";event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);finish();assert(Update.begins==0);
  upload.name="firmware";
  reset();upload.filename="";event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);finish();assert(Update.begins==0);
  upload.filename="image.bin";
  reset();event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,10);start();finish();
  assert(Update.commits==0 && !Update.running && server.code==400);
  setupWebUpdate();assert((server.registration==std::vector<std::string>{"page","guard"}));
  server.suppliedPassword="wrong";server.get();assert(server.code==200);
  assert(!guard.canHandle(HTTP_GET,"/update") && !guard.canHandle(HTTP_POST,"/config"));
  delete server.registeredGuard;
  std::cout<<"PASS: authenticated firmware/filesystem streams, byte identity, missing/empty/multiple uploads, PIN errors, write/finalization failures, full-size abort, delayed commit and request reset\n";
}
