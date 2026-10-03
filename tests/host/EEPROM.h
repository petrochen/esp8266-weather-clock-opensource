#pragma once
#include <array>
#include <cstring>
struct EEPROMMock {
 std::array<uint8_t,512> bytes{};
 bool commitOK = true;
 bool dirty = false;
 int commits = 0;
 void begin(int){}
 bool end(){bool saved=commit();dirty=false;return saved;}
 template<class T>void get(int offset,T& value){memcpy(&value,bytes.data()+offset,sizeof(T));}
 template<class T>void put(int offset,const T& value){if(memcmp(bytes.data()+offset,&value,sizeof(T))){memcpy(bytes.data()+offset,&value,sizeof(T));dirty=true;}}
 bool commit(){if(!dirty)return true;++commits;if(commitOK)dirty=false;return commitOK;}
} inline EEPROM;
