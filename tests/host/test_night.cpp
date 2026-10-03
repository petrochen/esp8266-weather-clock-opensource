#include <cassert>
#include <iostream>
#include "night_mode.cpp"
unsigned long epoch = 0;
long offset = 0;
unsigned long getAsyncEpoch(){return epoch;}
long getTotalOffset(unsigned long){return offset;}
int main(){
  EEPROM.bytes.fill(0xFF);
  auto legacy=EEPROM.bytes;
  loadNightSettings();
  assert(!nightSettings.enabled && nightSettings.start_hour==23 && nightSettings.end_hour==7);
  assert(EEPROM.bytes==legacy && EEPROM.commits==0); // no migration write, no Config reinterpretation
  for(int invalid=0;invalid<6;++invalid){
    NightSettings corrupt;corrupt.enabled=1;
    switch(invalid){
      case 0:corrupt.magic=0;break;case 1:corrupt.enabled=255;break;
      case 2:corrupt.start_hour=24;break;case 3:corrupt.end_hour=255;break;
      case 4:corrupt.start_minute=60;break;case 5:corrupt.end_minute=255;break;
    }
    EEPROM.put(NIGHT_SETTINGS_ADDR,corrupt);loadNightSettings();assert(!nightSettings.enabled);
  }
  NightSettings night;night.enabled=1;
  for(unsigned minute=0;minute<1440;++minute){
    bool expected=minute>=1380 || minute<420;
    assert(nightModeAt(night,minute*60,true)==expected);
    assert(!nightModeAt(night,minute*60,false));
  }
  night.start_hour=12;night.start_minute=30;night.end_hour=13;night.end_minute=15;
  for(unsigned minute=0;minute<1440;++minute)assert(nightModeAt(night,minute*60,true)==(minute>=750 && minute<795));
  night.end_hour=12;night.end_minute=30;assert(!nightModeAt(night,750*60,true));
  const char* error=nullptr;JsonDocument doc;
  for(const char* invalid:{"{\"night_enabled\":255}","{\"night_start_hour\":24}","{\"night_end_minute\":-1}","{\"night_end_hour\":1.5}","{\"night_start_minute\":\"2x\"}"}){
    deserializeJson(doc,invalid);NightSettings before=night;
    assert(!updateNightSettings(night,doc.as<JsonObjectConst>(),error));
    assert(memcmp(&before,&night,sizeof(night))==0);
  }
  deserializeJson(doc,"{\"night_enabled\":true,\"night_start_hour\":23,\"night_end_hour\":7,\"night_start_minute\":0,\"night_end_minute\":0}");
  assert(updateNightSettings(night,doc.as<JsonObjectConst>(),error));
  EEPROM.bytes.fill(0x42);legacy=EEPROM.bytes;assert(saveSettings(config,night));
  for(size_t i=sizeof(Config);i<512;++i)if(i<NIGHT_SETTINGS_ADDR || i>=NIGHT_SETTINGS_ADDR+sizeof(NightSettings))assert(EEPROM.bytes[i]==legacy[i]);
  loadNightSettings();assert(nightSettings.enabled==1 && nightSettings.start_hour==23);
  timeIsSynced=true;epoch=22*3600;offset=3600;assert(isNightModeActive());
  offset=0;assert(!isNightModeActive());
  epoch=6*3600;offset=3600;assert(!isNightModeActive());
  timeIsSynced=false;offset=0;assert(!isNightModeActive());
  EEPROM.commitOK=false;night.start_minute=1;assert(!saveSettings(config,night));
  doc.clear();exportNightSettings(night,doc);NightSettings restored;
  assert(updateNightSettings(restored,doc.as<JsonObjectConst>(),error) && restored.enabled && restored.start_hour==23);
  std::cout<<"PASS: night EEPROM migration/isolation/failure, disabled and unsynced defaults, midnight/day/equal windows, local offset, transactional validation, export round-trip\n";
}
