#include <cassert>
#include <iostream>
#include <algorithm>
#include "maintenance.cpp"
int main(){
  EEPROM.bytes.fill(0xFF);
  ESP.randomValues={UINT32_MAX,1000007};
  assert(setupMaintenance());
  std::string code=maintenancePassword();
  assert(code=="000007" && ESP.randomCalls==2); // rejection sampling + leading zeros
  int writes=EEPROM.commits;
  assert(setupMaintenance());
  assert(code==maintenancePassword() && EEPROM.commits==writes);
  assert(ESP.randomCalls==2);
  assert(!requireMaintenanceAuth() && server.status==401);
  server.username="admin";
  for(const char* password:{"000007","000-007"}){
    server.password=password;assert(requireMaintenanceAuth());
    assert(std::string(authenticatedMaintenancePassword())==password);
  }
  for(const char* wrong:{"7","000-07","0000007","000 007","000-008"," 000007","000007 "}){
    server.password=wrong;assert(!requireMaintenanceAuth() && server.status==401);
    assert(authenticatedMaintenancePassword()==nullptr);
  }
  server.username.clear();server.password.clear();
  for(const char* token:{"000007","000-007"}) {
    server.authorization=String("Bearer ")+token;assert(requireMaintenanceAuth());
  }
  for(const char* bad:{"000008","000-07","000007extra","000007 "," 000007"}) {
    server.authorization=String("Bearer ")+bad;assert(!requireMaintenanceAuth());
  }
  server.authorization="";
  server.username="other";server.password=code;
  assert(!requireMaintenanceAuth());

  // Showing the PIN requires physical access to read it; HTTP never returns it.
  server.username.clear();server.password.clear();
  fakeMillis=UINT32_MAX-10000;
  handleShowMaintenancePIN();assert(server.status==200);
  assert(display.text.find("000-007")!=std::string::npos);
  assert(server.body.find(code)==std::string::npos && server.body.find("000-007")==std::string::npos);
  assert(maintenanceCodeVisible());fakeMillis+=29999;assert(maintenanceCodeVisible());
  ++fakeMillis;assert(!maintenanceCodeVisible());
  writes=EEPROM.commits;handleShowMaintenancePIN();assert(EEPROM.commits==writes);
  assert(std::string(maintenancePassword())==code);

  // Migrate a valid 1.9.11/12 record once without changing adjacent EEPROM data.
  EEPROM.bytes.fill(0x42);
  MaintenanceCredentials legacy={0x4D41494E,"234567ABCDEF"};
  EEPROM.put(MAINTENANCE_ADDR,legacy);
  auto before=EEPROM.bytes;
  writes=EEPROM.commits;
  ESP.randomValues={999999};assert(setupMaintenance());
  assert(std::string(maintenancePassword())=="999999" && EEPROM.commits==writes+1);
  for(size_t i=0;i<before.size();++i){
    if(i<MAINTENANCE_ADDR || i>=MAINTENANCE_ADDR+sizeof(MaintenanceCredentials))assert(EEPROM.bytes[i]==before[i]);
  }
  assert(setupMaintenance() && EEPROM.commits==writes+1);
  for(const char* invalid:{"12x456","12345","1234567","-12345"}){
    MaintenanceCredentials corrupt={};corrupt.magic=MAINTENANCE_MAGIC;
    strcpy(corrupt.password,invalid);EEPROM.put(MAINTENANCE_ADDR,corrupt);
    assert(setupMaintenance());
    std::string restored=maintenancePassword();
    assert(restored.size()==6 && std::all_of(restored.begin(),restored.end(),[](char c){return c>='0' && c<='9';}));
  }
  // Existing legacy config and reset counter survive credential initialization.
  EEPROM.bytes.fill(0xFF);EEPROM.bytes[0]=0x42;EEPROM.bytes[480]=0xA5;
  assert(setupMaintenance());assert(EEPROM.bytes[0]==0x42 && EEPROM.bytes[480]==0xA5);
  EEPROM.bytes.fill(0xFF);EEPROM.commitOK=false;
  assert(!setupMaintenance() && maintenancePassword()==nullptr);
  assert(!requireMaintenanceAuth() && server.status==503);
  handleShowMaintenancePIN();assert(server.status==503);
  std::cout<<"PASS: six-digit PIN, leading zeros, persistence/migration, both web formats, rejected credentials, physical-only display, expiry/rollover, EEPROM isolation, storage failure\n";
}
