#pragma once
#include <string>
#include <vector>
constexpr int U_FLASH=0, U_FS=100;
struct UpdateMock {
  bool running=false, error=false, failBegin=false, failWrite=false, failEnd=false;
  int begins=0, commits=0, cancels=0, command=-1;
  size_t maximum=0;
  std::vector<uint8_t> written;
  bool isRunning(){return running;}
  bool begin(size_t size,int type){
    ++begins; command=type; maximum=size; written.clear(); error=false;
    if (failBegin || !size) {error=true;return false;}
    running=true; return true;
  }
  size_t write(uint8_t* data,size_t count){
    if(failWrite || !running || error || written.size()+count>maximum){error=true;return 0;}
    written.insert(written.end(),data,data+count);return count;
  }
  bool end(bool partial=false){
    bool ok=running && !error && !failEnd && !written.empty() && (partial || written.size()==maximum);
    running=false;
    if(ok)++commits; else ++cancels;
    return ok;
  }
  String getErrorString() const{return "ERROR[10]: Invalid image";}
} inline Update;
