#define HOST_DISPLAY_TEST
#include "globals.h"
#include <cassert>
#include <iostream>
#include <algorithm>
#include <climits>
constexpr int SSD1306_WHITE = 1, SSD1306_SETCONTRAST = 0x81, SSD1306_DISPLAYON=0xAF, SSD1306_DISPLAYOFF=0xAE;
struct DisplayMock {
  uint8_t buffer[1024]{};
  int transfers = 0;
  std::vector<int> commands;
  int rotation = 0;
  std::string text, lastText;
  void clearDisplay(){memset(buffer,0,sizeof(buffer));text.clear();}
  void setTextSize(int){}
  void setTextColor(int){}
  void setCursor(int,int){}
  const uint8_t* bitmap=nullptr;
  int bitmapX=0,bitmapY=0,bitmapWidth=0,bitmapHeight=0;
  void drawBitmap(int x,int y,const uint8_t* bits,int w,int h,int){
    bitmap=bits;bitmapX=x;bitmapY=y;bitmapWidth=w;bitmapHeight=h;
  }
  void setRotation(int value){rotation=value;}
  void ssd1306_command(int value){commands.push_back(value);}
  void print(const char* value){text+=value;}
  void print(const String& value){text+=value;}
  void print(int value){text+=std::to_string(value);}
  template<class T>void println(T value){print(value);text+='\n';}
  template<class...T>void printf(const char* format,T...values){
    char formatted[128];snprintf(formatted,sizeof(formatted),format,values...);text+=formatted;
  }
  void display(){++transfers;lastText=text;}
  uint8_t* getBuffer(){return buffer;}
} display;
uint8_t displayMode=0,nextDisplayMode=0;
bool inTransition=false,colonBlink=false;
unsigned long transitionStart=0,lastDissolveFrame=0,lastModeSwitch=0,ipDisplayUntil=0;
WiFiConnectionState wifiConnState=WIFI_CONN_CONNECTED;
void delay(unsigned long n){fakeMillis+=n;}
unsigned long testEpoch=1791028800UL;
unsigned long getAsyncEpoch(){return testEpoch;}
long getTotalOffset(unsigned long){return 0;}
bool isModeEnabled(uint8_t mode);
void showConnected();
#include "maintenance.cpp"
#include "display.cpp"
#include "night_mode.cpp"

static void finishTransition(uint8_t expectedMode){
  fakeMillis=lastModeSwitch+config.display_rotation_sec*1000UL+1;
  updateDisplayRotation();
  assert(inTransition && nextDisplayMode==expectedMode);
  // Exercise both halves, including the fully dissolved midpoint.
  for(unsigned elapsed=100;elapsed<=DISSOLVE_DURATION;elapsed+=100){
    fakeMillis=transitionStart+elapsed;updateDisplayRotation();
  }
  assert(!inTransition && displayMode==expectedMode);
  int transfers=display.transfers;
  updateDisplayRotation();
  assert(display.transfers==transfers+1 && !display.lastText.empty());
}

int main(){
  // EEPROM settings bypass the form validator. Every supported brightness,
  // including the legacy zero value, must send a nonzero contrast to the OLED.
  int previousContrast = 0;
  for(int level=0;level<=7;++level){
    config.brightness=level;
    config.display_orientation=level%4;
    display.commands.clear();applyDisplaySettings();
    assert(display.commands.size()==2 && display.commands[0]==SSD1306_SETCONTRAST);
    int contrast=display.commands[1];
    assert(contrast>=32 && contrast<=255 && contrast>previousContrast);
    assert(display.rotation==config.display_orientation);
    previousContrast=contrast;
  }
  assert(previousContrast==255);
  for(int invalid:{INT_MIN,-1,8,INT_MAX}){
    config.brightness=invalid;
    display.commands.clear();applyDisplaySettings();
    assert(display.commands[1]==(invalid<0 ? 32 : 255));
  }
  config=Config();
  // Reproduce the startup call sequence with persisted brightness zero.
  // Text capture checks rendering dispatch; it does not emulate font pixels/I2C.
  fakeMillis=1000;showStartupAnimation();
  assert(fakeMillis==1000); // startup logo must not delay WiFi/NTP setup
  config.brightness=0;applyDisplaySettings();
  assert(display.commands.back()>=32);
  showWiFiConnecting(0);showConnected();
  EEPROM.bytes.fill(0xff);assert(setupMaintenance());
  // setup() dismisses the startup connection overlay without showing the PIN.
  ipDisplayUntil=0;lastModeSwitch=millis();invalidateDisplay();updateDisplayRotation();
  assert(!maintenanceCodeVisible() && display.lastText.find("--:--")!=std::string::npos);
  handleShowMaintenancePIN();
  assert(display.lastText.find("Update / reset PIN")!=std::string::npos);
  int transfers=display.transfers;
  updateDisplayRotation();assert(display.transfers==transfers);
  fakeMillis+=29999;updateDisplayRotation();assert(display.transfers==transfers);
  ++fakeMillis;updateDisplayRotation();
  assert(display.transfers==transfers+1 && !maintenanceCodeVisible());
  assert(display.lastText.find("--:--")!=std::string::npos);
  assert(display.lastText.find("Syncing NTP")!=std::string::npos);

  // Missing network/time/weather must still render a useful clock placeholder.
  wifiConnState=WIFI_CONN_FAILED;invalidateDisplay();updateDisplayRotation();
  assert(display.lastText.find("No WiFi")!=std::string::npos);
  fakeMillis=lastModeSwitch+config.display_rotation_sec*1000UL+1;
  updateDisplayRotation();assert(!inTransition && displayMode==0);

  timeIsSynced=true;wifiConnState=WIFI_CONN_CONNECTED;
  weather.valid=true;weather.temperature=22.2f;
  sunTimes.lastDay=1;strcpy(sunTimes.sunrise,"07:31");strcpy(sunTimes.sunset,"19:14");
  finishTransition(1);assert(display.lastText.find("22.2")!=std::string::npos);
  finishTransition(2);assert(display.lastText.find("07:31")!=std::string::npos);
  finishTransition(0);assert(display.lastText.find("--:--")==std::string::npos);

  // On-demand PIN is not overwritten by a WiFi reconnect/failure overlay.
  handleShowMaintenancePIN();transfers=display.transfers;
  showConnected();showNoWiFi(10);updateDisplayRotation();
  assert(display.transfers==transfers && display.lastText.find("Update / reset PIN")!=std::string::npos);
  fakeMillis+=30000;updateDisplayRotation();
  assert(display.transfers==transfers+1 && !maintenanceCodeVisible());

  showConnected();transfers=display.transfers;
  fakeMillis+=1999;updateDisplayRotation();assert(display.transfers==transfers);
  ++fakeMillis;updateDisplayRotation();assert(display.transfers==transfers+1);
  assert(display.lastText.find("OK")==std::string::npos);
  showNumber(8888,false);ipDisplayUntil=millis()+3000UL;
  transfers=display.transfers;
  fakeMillis+=2999;updateDisplayRotation();assert(display.transfers==transfers);
  ++fakeMillis;updateDisplayRotation();assert(display.transfers==transfers+1);
  assert(display.lastText.find("8888")==std::string::npos);

  config=Config();lastModeSwitch=fakeMillis;
  config.display_rotation_sec=60;
  displayMode=1;weather.valid=true;
  invalidateDisplay();updateDisplayRotation();
  transfers=display.transfers;
  for(int i=0;i<100;++i){++fakeMillis;updateDisplayRotation();}
  assert(display.transfers==transfers);
  invalidateDisplay();updateDisplayRotation();assert(display.transfers==transfers+1);
  displayMode=2;sunTimes.lastDay=1;invalidateDisplay();updateDisplayRotation();
  transfers=display.transfers;
  for(int i=0;i<100;++i){++fakeMillis;updateDisplayRotation();}
  assert(display.transfers==transfers);
  displayMode=0;config.show_weather=false;config.show_sunrise_sunset=false;
  fakeMillis=lastModeSwitch+70000;updateDisplayRotation();assert(!inTransition);
  memset(display.buffer,0xff,1024);applyDissolveEffect(0,false);
  assert(std::all_of(display.buffer,display.buffer+1024,[](uint8_t x){return x==255;}));
  applyDissolveEffect(50,false);
  int count=0;for(uint8_t x:display.buffer)count+=__builtin_popcount(x);
  assert(count==4096);
  applyDissolveEffect(100,false);
  assert(std::all_of(display.buffer,display.buffer+1024,[](uint8_t x){return x==0;}));
  // Night window blanks once, halts transitions, and wakes for explicit PIN.
  nightSettings.enabled=1;nightSettings.start_hour=23;nightSettings.end_hour=7;
  testEpoch=23*3600;timeIsSynced=true;ipDisplayUntil=0;
  fakeMillis+=30000;maintenanceCodeVisible();
  updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYOFF);
  size_t commands=display.commands.size();int beforeNight=display.transfers;
  updateDisplayRotation();showNoWiFi(5);showConnected();
  assert(display.commands.size()==commands && display.transfers==beforeNight);
  handleShowMaintenancePIN();assert(display.commands.back()==SSD1306_DISPLAYON);
  updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYON);
  fakeMillis+=30000;updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYOFF);
  showNumber(8888,false);ipDisplayUntil=millis()+3000;
  updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYON);
  fakeMillis+=3000;updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYOFF);
  testEpoch=7*3600;updateDisplayRotation();assert(display.commands.back()==SSD1306_DISPLAYON);
  // WMO partly cloudy has its own silhouette; other categories keep their mappings.
  for(int code:{0,1})assert(weatherIcon(code)==weather_sun);
  assert(weatherIcon(2)==weather_partly_cloudy && weatherIcon(3)==weather_cloud);
  for(int code:{45,48})assert(weatherIcon(code)==weather_fog);
  for(int code:{51,53,55,56,57,61,63,65,66,67,80,81,82})assert(weatherIcon(code)==weather_rain);
  for(int code:{71,73,75,77,85,86})assert(weatherIcon(code)==weather_snow);
  for(int code:{95,96,97,99})assert(weatherIcon(code)==weather_thunder);
  for(int code:{-1,4,100})assert(weatherIcon(code)==weather_unknown);
  for(int code:{0,2,3,45,61,71,95,-1}){
    weather.weathercode=code;weather.temperature=-99.9;displayWeather();
    assert(display.bitmap==weatherIcon(code));
    assert(display.bitmapWidth==16 && display.bitmapHeight==16); // native pixels, no resizing
    assert(display.bitmapX>=0 && display.bitmapX+display.bitmapWidth<=22); // before temperature area
    assert(display.bitmapY>=8 && display.bitmapY+display.bitmapHeight<=48); // below stale mark, above city
  }
  config.display_orientation=255;applyDisplaySettings();assert(display.rotation==2);
  std::cout<<"PASS: contrast bounds, startup without pause, on-demand PIN expiry, offline placeholder, screen transitions, overlay expiry, static frames, dissolve endpoints\n";
}
