#define HOST_DISPLAY_TEST
#include "globals.h"
#include <Adafruit_GFX.h>
#include <fstream>
#include <functional>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <algorithm>
constexpr int SSD1306_WHITE=1,SSD1306_SETCONTRAST=0x81,SSD1306_DISPLAYON=0xAF,SSD1306_DISPLAYOFF=0xAE;
struct RasterDisplay:Adafruit_GFX {
 uint8_t buffer[1024]{};bool powered=true;int wraps=0,clippedGlyphs=0,clippedInk=0;
 std::string text;
 RasterDisplay():Adafruit_GFX(128,64){}
 void clearDisplay(){memset(buffer,0,sizeof(buffer));wraps=clippedGlyphs=clippedInk=0;text.clear();}
 void drawPixel(int16_t x,int16_t y,uint16_t color)override {
  if(x<0||y<0||x>=width()||y>=height()){if(color)++clippedInk;return;}
  auto p=physical(x,y);auto& b=buffer[p.first+128*(p.second/8)];uint8_t mask=1<<(p.second%8);
  if(color==1)b|=mask;else if(color==0)b&=~mask;else b^=mask;
 }
 std::pair<int,int> physical(int x,int y)const {
  switch(getRotation()){case 1:return {127-y,x};case 2:return {127-x,63-y};case 3:return {y,63-x};default:return {x,y};}
 }
 size_t write(uint8_t c)override {
  if(c!='\n'&&c!='\r'){
   int x=cursor_x,y=cursor_y,w=6*textsize_x,h=8*textsize_y;
   if(wrap&&x+w>width()){++wraps;x=0;y+=h;}
   if(x<0||y<0||x+w>width()||y+h>height())++clippedGlyphs;
  }
  text+=char(c);return Adafruit_GFX::write(c);
 }
 void display(){}
 uint8_t* getBuffer(){return buffer;}
 void ssd1306_command(uint8_t c){if(c==SSD1306_DISPLAYOFF)powered=false;if(c==SSD1306_DISPLAYON)powered=true;}
 void save(const std::string& path){
  std::ofstream f(path,std::ios::binary);f<<"P6\n"<<width()<<' '<<height()<<"\n255\n";
  for(int y=0;y<height();++y)for(int x=0;x<width();++x){auto p=physical(x,y);bool on=powered&&(buffer[p.first+128*(p.second/8)]&(1<<(p.second%8)));
   unsigned char rgb[3]={0,0,0};if(on){rgb[0]=p.second<16?255:139;rgb[1]=p.second<16?207:217;rgb[2]=p.second<16?94:255;}f.write((char*)rgb,3);
  }
 }
} display;
uint8_t displayMode=0,nextDisplayMode=0;
bool inTransition=false,colonBlink=true;
unsigned long transitionStart=0,lastDissolveFrame=0,lastModeSwitch=0,ipDisplayUntil=0;
WiFiConnectionState wifiConnState=WIFI_CONN_CONNECTED;
void delay(unsigned long n){fakeMillis+=n;}
unsigned long testEpoch=1791071940UL;
unsigned long getAsyncEpoch(){return testEpoch;}
long getTotalOffset(unsigned long){return 0;}
bool isModeEnabled(uint8_t);
void showConnected();
#include "maintenance.cpp"
#include "display.cpp"
#include "night_mode.cpp"

static void setCity(const char* name) {
 assert(strlen(name) < sizeof(config.city_name));
 strcpy(config.city_name, name);
}
static void sampleForecast() {
 for (int i=0;i<6;++i) { forecast.hours[i].epoch=(testEpoch/3600+1+i)*3600;forecast.hours[i].rain=i*20;forecast.hours[i].temperature=22-i; }
 forecast.count=6;
 for (int i=0;i<2;++i) { auto& d=forecast.days[i];d.epoch=(testEpoch/86400+i)*86400;d.low=16-i;d.high=23-i;d.uv=4.2f;d.valid=true;d.sunrise=d.epoch+7*3600;d.sunset=d.epoch+19*3600; }
}
int main(int argc,char** argv){
 int failures=0;
 if(argc!=2)return 2;std::filesystem::create_directories(argv[1]);
 ESP.nextValue=123456;setupMaintenance();
 struct Case{const char* name;std::function<void()> draw;};
 std::vector<Case> cases={
  {"combined",[]{featureSettings.clock_weather=1;updateDisplay();}},
  {"combined-12h",[]{featureSettings.clock_weather=1;config.hour_format_24=false;updateDisplay();}},
  {"combined-fahrenheit",[]{featureSettings.clock_weather=1;featureSettings.temperature_unit=1;weather.temperature=-100;weather.stale=true;updateDisplay();}},
  {"combined-portrait-night",[]{featureSettings.clock_weather=1;weather.isDay=false;weather.weathercode=0;updateDisplay();}},
  {"comfort",[]{weather.comfortValid=true;weather.feelsLike=19;weather.humidity=68;displayExtra(3);}},
  {"comfort-missing",[]{weather.comfortValid=false;weather.humidity=-1;displayExtra(3);}},
  {"comfort-extreme",[]{weather.comfortValid=true;weather.feelsLike=-120;featureSettings.temperature_unit=1;weather.temperature=-100;weather.humidity=100;displayExtra(3);}},
  {"rain",[]{sampleForecast();displayExtra(4);}},
  {"rain-100",[]{sampleForecast();for(auto& hour:forecast.hours)hour.rain=100;displayExtra(4);}},
  {"rain-missing",[]{sampleForecast();forecast.hours[0].rain=-1;displayExtra(4);}},
  {"rain-midnight",[]{testEpoch=(testEpoch/86400)*86400+23*3600;sampleForecast();displayExtra(4);}},
  {"daily",[]{sampleForecast();displayExtra(5);}},
  {"daily-extreme",[]{sampleForecast();featureSettings.temperature_unit=1;forecast.days[0].low=-100;forecast.days[0].high=70;displayExtra(5);}},
  {"wind",[]{weather.windspeed=26;weather.windDirection=315;displayExtra(6);}},
  {"wind-ms",[]{weather.windspeed=26;weather.windDirection=270;featureSettings.wind_unit=1;displayExtra(6);}},
  {"wind-mph",[]{weather.windspeed=500;weather.windDirection=-1;featureSettings.wind_unit=2;displayExtra(6);}},
  {"moon",[]{weather.weathercode=0;weather.isDay=false;displayWeather();}},
  {"sun-countdown",[]{sampleForecast();featureSettings.sun_countdown=1;displaySunTimes();}},
  {"external-card",[]{strcpy(externalCard.title,"Living room CO2");strcpy(externalCard.value,"920");strcpy(externalCard.unit,"ppm");displayExtra(SCREEN_COUNT);}},
  {"external-card-long",[]{strcpy(externalCard.title,"A very long name with 31 chars!");strcpy(externalCard.value,"123456789012345");strcpy(externalCard.unit,"12345678901");displayExtra(SCREEN_COUNT);}},
  {"night-dim",[]{nightSettings.enabled=1;featureSettings.night_action=1;testEpoch=23*3600;updateDisplayRotation();}},
  {"startup",[]{showStartupAnimation();}},
  {"connecting",[]{showWiFiConnecting(2);}},
  {"connecting-start",[]{showWiFiConnecting(0);}},
  {"connecting-middle",[]{showWiFiConnecting(1);}},
  {"connected",[]{showConnected();}},
  {"long-ip",[]{WiFi.ip.value=0xffffffff;showConnected();}},
  {"long-ssid",[]{WiFi.ssid="Very-Long-Home-Network-Name-1234";showConnected();}},
  {"clock-24h",[]{updateDisplay();}},
  {"clock-12h",[]{config.hour_format_24=false;updateDisplay();}},
  {"clock-midnight",[]{config.hour_format_24=false;testEpoch=1790985600UL;updateDisplay();}},
  {"clock-noon",[]{config.hour_format_24=false;testEpoch=1791028800UL;updateDisplay();}},
  {"clock-colon-off",[]{colonBlink=false;updateDisplay();}},
  {"clock-offline",[]{wifiConnState=WIFI_CONN_FAILED;updateDisplay();}},
  {"clock-unsynced",[]{timeIsSynced=false;updateDisplay();}},
  {"clock-unsynced-offline",[]{timeIsSynced=false;wifiConnState=WIFI_CONN_FAILED;updateDisplay();}},
  {"weather",[]{displayWeather();}},
  {"weather-accent",[]{setCity("Portim\xc3\xa3o");displayWeather();}},
  {"weather-city-21",[]{setCity("Newcastle upon Tyne!!");displayWeather();}},
  {"weather-city-31",[]{setCity("Llanfairpwllgwyngyllgogerychwyr");displayWeather();}},
  {"weather-word-wrap",[]{setCity("A very long name with 31 chars!");displayWeather();}},
  {"weather-empty-city",[]{config.city_name[0]=0;displayWeather();}},
  {"weather-emoji",[]{setCity("Town ☀️");displayWeather();}},
  {"weather-invalid-utf8",[]{setCity("Town \xc3");displayWeather();}},
  {"weather-cyrillic",[]{setCity("\xd0\xa1\xd0\xb0\xd0\xbd\xd0\xba\xd1\x82-\xd0\x9f\xd0\xb5\xd1\x82\xd0\xb5\xd1\x80\xd0\xb1\xd1\x83\xd1\x80\xd0\xb3");displayWeather();}},
  {"weather-minimum",[]{weather.temperature=-100.0;displayWeather();}},
  {"weather-maximum",[]{weather.temperature=70.0;displayWeather();}},
  {"weather-negative",[]{weather.temperature=-99.9;displayWeather();}},
  {"weather-stale",[]{weather.stale=true;displayWeather();}},
  {"weather-no-data",[]{weather.valid=false;displayWeather();}},
  {"weather-sun",[]{weather.weathercode=0;displayWeather();}},
  {"weather-partly-cloudy",[]{weather.weathercode=2;displayWeather();}},
  {"weather-fog",[]{weather.weathercode=45;displayWeather();}},
  {"weather-rain",[]{weather.weathercode=61;displayWeather();}},
  {"weather-snow",[]{weather.weathercode=71;displayWeather();}},
  {"weather-thunder",[]{weather.weathercode=95;displayWeather();}},
  {"weather-unknown",[]{weather.weathercode=-1;displayWeather();}},
  {"sun-times",[]{displaySunTimes();}},
  {"sun-longest-day",[]{sunTimes.sunriseMinutes=0;sunTimes.sunsetMinutes=1439;strcpy(sunTimes.sunrise,"00:00");strcpy(sunTimes.sunset,"23:59");displaySunTimes();}},
  {"sun-unavailable",[]{sunTimes.lastDay=-1;displaySunTimes();}},
  {"no-wifi",[]{showNoWiFi(15);}},
  {"no-wifi-retry-min",[]{showNoWiFi(300);}},
  {"pin",[]{showMaintenanceCode();}},
  {"setup",[]{showSetupScreen(false);}},
  {"recovery",[]{showSetupScreen(true);}},
  {"test-display",[]{showNumber(8888,false);}},
  {"ota-0",[]{showUpdateProgress(0);}},
  {"ota-50",[]{showUpdateProgress(50);}},
  {"ota-100",[]{showUpdateProgress(100);}},
  {"transition-clock-out",[]{inTransition=true;updateDisplay();applyDissolveEffect(50,true);}},
  {"transition-weather-in",[]{inTransition=true;displayWeather();applyDissolveEffect(50,false);}},
  {"night",[]{nightSettings.enabled=1;testEpoch=23*3600;updateDisplayRotation();}}
 };
 for(int rotation:{0,1,2,3})for(auto& item:cases){
  config=Config{};config.display_orientation=rotation;nightSettings=NightSettings{};
  featureSettings=FeatureSettings();forecast=ForecastData();externalCard=ExternalCard();displayPaused=false;weather=WeatherData();
  weather.valid=true;weather.stale=false;weather.temperature=22.4;weather.weathercode=3;
  sunTimes.lastDay=1;strcpy(sunTimes.sunrise,"07:31");strcpy(sunTimes.sunset,"19:14");sunTimes.sunriseMinutes=451;sunTimes.sunsetMinutes=1154;
  wifiConnState=WIFI_CONN_CONNECTED;WiFi.ssid="Home WiFi";WiFi.ip.value=0x7b01a8c0;
  timeIsSynced=true;testEpoch=1791071940UL;colonBlink=true;inTransition=false;ipDisplayUntil=0;showingCode=false;displaySleeping=false;
  fakeMillis=20000;lastModeSwitch=fakeMillis;displayMode=0;display.powered=true;display.setTextWrap(true);display.cp437(false);display.setFont();applyDisplaySettings();
  display.clearDisplay();item.draw();std::string name=std::string(item.name)+"-r"+std::to_string(rotation);
  display.save(std::string(argv[1])+"/"+name+".ppm");
  if(display.wraps || display.clippedGlyphs || display.clippedInk) ++failures;
  std::cout<<name<<'\t'<<display.width()<<'\t'<<display.height()<<'\t'<<display.wraps<<'\t'<<display.clippedGlyphs<<'\t'<<display.clippedInk<<'\n';
 }
 // Stress every accepted ASCII city length, not just the gallery examples.
 for(int rotation:{0,1,2,3})for(size_t length=0;length<=31;++length){
  config.display_orientation=rotation;applyDisplaySettings();
  setCity(std::string(length,'W').c_str());weather.valid=true;
  displayWeather();
  assert(!display.wraps && !display.clippedGlyphs && !display.clippedInk);
  // A short first word followed by a long word must not waste a line and
  // truncate an otherwise representable city. Exercise every split position.
  for(size_t split=0;split<length;++split){
   std::string name(length,'W');name[split]=' ';
   display.clearDisplay();displayFooter(name.c_str(),2);
   std::string rendered=display.text;
   name.erase(std::remove(name.begin(),name.end(),' '),name.end());
   rendered.erase(std::remove(rendered.begin(),rendered.end(),' '),rendered.end());
   assert(rendered==name);
   assert(!display.wraps && !display.clippedGlyphs && !display.clippedInk);
  }
 }
 // UTF-8 must count as glyphs instead of producing one GFX character per byte.
 for(const char* sample:{"Portimão", "Санкт-Петербург", "A😀B", "X\xc3", "\xed\xa0\x80"}){
  const char* cursor=sample;int count=0;
  while(*cursor){nextDisplayGlyph(cursor);++count;}
  const int expected=sample[0]=='P'?8:sample[0]=='A'?3:sample[0]=='X'?2:uint8_t(sample[0])==0xED?1:15;
  assert(count==expected);
 }
 return failures ? 1 : 0;
}
