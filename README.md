# Reverse Engineering a €5 AliExpress Weather Clock: A Security Story

<p align="center">
  <a href="https://github.com/petrochen/esp8266-weather-clock-opensource/releases">
    <img src="https://img.shields.io/github/v/release/petrochen/esp8266-weather-clock-opensource?style=flat-square&label=Release&color=green" alt="Release">
  </a>
  <a href="https://github.com/petrochen/esp8266-weather-clock-opensource/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/petrochen/esp8266-weather-clock-opensource?style=flat-square&label=License&color=blue" alt="License">
  </a>
  <a href="https://github.com/petrochen/esp8266-weather-clock-opensource/issues">
    <img src="https://img.shields.io/github/issues/petrochen/esp8266-weather-clock-opensource?style=flat-square&label=Issues&color=orange" alt="Issues">
  </a>
  <img src="https://img.shields.io/badge/ESP8266-ESP--01S-blue?style=flat-square" alt="Hardware">
  <img src="https://img.shields.io/badge/Status-Production%20Ready-brightgreen?style=flat-square" alt="Status">
</p>

## TL;DR

I bought a cute weather clock kit from AliExpress ([TJ-56-654](https://pt.aliexpress.com/item/1005008333782531.html)) and discovered it was **leaking my WiFi password in plaintext** to anyone within radio range. So I ripped out the firmware, wrote my own, and ended up with a fully async, OTA-updatable, Home Assistant-ready smart clock that's actually secure.

---

## Table of Contents

**Story**

- [The Discovery: When "Smart" Means "Insecure"](#the-discovery-when-smart-means-insecure)
- [The Device](#the-device)
- [The Investigation](#the-investigation)
- [The Solution: Custom Firmware](#the-solution-custom-firmware)
- [Hardware Quirk: Swapped I2C Pins](#hardware-quirk-swapped-i2c-pins)
- [Version History](#version-history)

**Use it**

- [How to Flash This Firmware](#how-to-flash-this-firmware)
- [Web Interface](#web-interface)
- [API Documentation](#api-documentation)
- [Testing](#testing)
- [Security Improvements](#security-improvements)
- [Project Structure](#project-structure)

For internal architecture notes (state machines, memory, EEPROM layout), see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## The Discovery: When "Smart" Means "Insecure"

It started innocently enough. I ordered what looked like a fun DIY electronics project: an ESP8266-based weather clock with a transparent acrylic case and an OLED display. The listing promised:

- ✅ WiFi weather updates
- ✅ 3-day forecast
- ✅ Temperature, humidity, date/time
- ✅ "Intelligent connected to WIFI"

What they didn't mention:

**🚨 CRITICAL SECURITY FLAW 🚨**

When you first set up the device, it creates an access point with a default password. Fair enough - that's how WiFiManager works. But here's where it gets bad:

1. You connect to the AP (192.168.4.1)
2. You configure your home WiFi credentials
3. Device connects to your network
4. **The open AP stays active in parallel**
5. **Your WiFi password is displayed in plaintext on the config page**

Anyone within WiFi range could:

- Connect to the device's AP (weak default password)
- Browse to 192.168.4.1
- Read your WiFi password in plaintext
- Access your network

This is a textbook example of poor IoT security design. No thanks.

---

## The Device

**Product**: ESP8266 Mini Weather Clock Kit
**Model**: TJ-56-654
**Price**: ~€5 EUR
**Source**: [AliExpress Link](https://pt.aliexpress.com/item/1005008333782531.html)

### Original Hardware Specifications

| Component   | Details                                  |
| ----------- | ---------------------------------------- |
| **MCU**     | ESP-01S (ESP8266EX, 1MB flash, 80KB RAM) |
| **Display** | GM009605v4.3 OLED (128x64, I2C)          |
| **Power**   | 5V USB (Micro-USB)                       |
| **Case**    | Transparent acrylic (40x40x43mm)         |
| **PCB**     | TJ-56-654 main board                     |

### What It Came With

- Acrylic case parts (6 pieces)
- ESP-01S WiFi module
- OLED display module
- Main PCB with headers
- USB power cable
- Brass standoffs and screws
- Pin headers (soldering required)

### Original Firmware Issues

Beyond the password leak:

- **Dependency on QWeather API**: Requires account registration, project setup, API key management
- **Chinese cloud service**: All weather data routes through proprietary servers
- **No OTA updates**: Firmware updates require disassembly and FTDI connection
- **Limited features**: Fixed display modes, no customization
- **Unknown code**: Closed-source firmware, no way to audit what it's doing

---

## The Investigation

### Opening It Up

The transparent case made inspection easy - just unscrew the brass standoffs. Inside:

- **ESP-01S module** clearly labeled with pinout
- **I2C OLED display** connected via 4 pins (VCC, GND, SDA, SCL)
- **No additional sensors** (temperature/humidity were from weather API, not local)

The ESP-01S pinout is printed right on the PCB:

```
3V3  |  GND
 TX  |  GPIO0  (I2C SDA)
 RX  |  GPIO2  (I2C SCL)
EN   |  GND
```

### Connecting FTDI

To flash custom firmware, you need:

1. **FTDI USB-to-Serial adapter** (3.3V! Not 5V - you'll fry the ESP8266)
2. **Jumper wires**
3. **Steady hands**

**Wiring:**

```
FTDI        ESP-01S
────────────────────
 3V3    →   3V3
 GND    →   GND
 TX     →   RX
 RX     →   TX
 GND    →   GPIO0  (for programming mode)
```

**Boot into flash mode:**

1. Connect GPIO0 to GND
2. Power on the device
3. Remove GPIO0 to GND connection after boot
4. Device is now in programming mode

**Programming:**

- Use Arduino IDE with ESP8266 board support
- Select board: "Generic ESP8266 Module"
- Flash size: 1MB (FS:64KB OTA:~470KB)
- Upload speed: 115200 baud

After the first flash with OTA support, you never need wires again - all updates happen over WiFi.

---

## The Solution: Custom Firmware

I decided to write a complete replacement firmware with:

### Core Principles

1. **Security First**: No hardcoded credentials, no open networks, WiFiManager with proper AP timeout
2. **Privacy**: Use free, open APIs (Open-Meteo instead of QWeather)
3. **Maintainability**: OTA updates for painless improvements
4. **Performance**: Fully async architecture, no blocking operations
5. **Reliability**: Proper error handling, exponential backoff, memory safety

### Features Implemented

#### 🌐 Network & Time

- **WiFiManager** captive portal for secure first-time setup
- **Hybrid WiFi**: Synchronous on boot (ensures proper init), async reconnect during operation
- **NTP time sync** with configurable server and interval
- **Timezone support** with automatic European DST calculation
- **mDNS**: Access via `http://tj56654-clock.local/`

#### 🌦️ Weather Data

- **Open-Meteo API**: Free, no registration, no API key
- **Configurable location**: Latitude/longitude + city name
- **Data**: Temperature, sunrise, sunset, daylight duration
- **Smart updates**: Async fetch every 30 minutes (configurable)

#### 🔄 OTA Updates

- **Web-based OTA**: Upload .bin files via browser at `/update`
- **ArduinoOTA**: Update directly from Arduino IDE
- **Non-blocking**: System stays responsive during updates
- **Secure**: Password-protected upload (admin/admin - change it!)

#### 📺 Display Modes

Three rotating display screens (configurable interval):

1. **Time Mode**
   - Large HH:MM display
   - Blinking colon animation
   - Day of week and date
   - 12/24 hour format support

2. **Weather Mode**
   - Temperature with superscript °c
   - City name
   - Clean, minimalist layout

3. **Sunrise/Sunset Mode**
   - Sunrise time with ↑ arrow
   - Sunset time with ↓ arrow
   - **Daylight duration** (e.g., "Day 9h 41m")

All modes are center-aligned, rotation-aware, and gracefully handle missing data.

#### 🌐 Web Interface

- `/` - Home page with live time
- `/config` - Full configuration form
- `/debug` - System diagnostics
- `/update` - OTA firmware upload

#### 🔌 REST API

All endpoints return JSON:

- `GET /api/time` - Current time
- `GET /api/status` - System status (WiFi, uptime, heap)
- `GET /api/debug` - Detailed diagnostics
- `GET /api/weather` - Weather + sunrise/sunset
- `GET /api/config` - Export configuration
- `POST /api/config` - Import configuration
- `POST /api/eeprom-clear` - Factory reset
- `POST /api/reboot` - Remote reboot

---

## Hardware Quirk: Swapped I2C Pins

ESP-01S exposes only GPIO0 and GPIO2. The TJ-56-654 board designer used them as I2C — but **swapped from typical breakouts**:

```cpp
Wire.begin(0, 2);  // SDA=GPIO0, SCL=GPIO2 (non-standard!)
```

Standard ESP8266 boards use GPIO4=SDA, GPIO5=SCL. Easy to miss if you're porting code from a different ESP8266 setup. The display is a GM009605v4.3 OLED (SSD1306-compatible) at I2C address 0x3C.

For architecture details (async state machines, memory budget, EEPROM layout, factory reset), see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Version History

The clock has gone through many iterations — display hardware discovery (v1.5–v1.7), stability and security fixes (v1.8), full async refactor (v1.9.0), and a long series of bug fixes informed by community reports and AI-assisted code review (v1.9.1–v1.9.10).

See [CHANGELOG.md](CHANGELOG.md) for the full version history with technical details.

---

## How to Flash This Firmware

### Requirements

- **Hardware**: TJ-56-654 weather clock or compatible ESP-01S + OLED setup
- **FTDI Adapter**: 3.3V USB-to-Serial (CP2102, FT232RL, CH340)
- **Software**: Arduino IDE 1.8.x or 2.x

### Arduino IDE Setup

1. **Install ESP8266 Board Support**
   - File → Preferences
   - Additional Board Manager URLs: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools → Board → Boards Manager → Search "ESP8266" → Install

2. **Install Required Libraries**
   - Sketch → Include Library → Manage Libraries
   - Install:
     - `Adafruit GFX Library`
     - `Adafruit SSD1306`
     - `NTPClient`
     - `WiFiManager` (by tzapu)
     - `AsyncHTTPRequest_Generic`
     - `ESPAsyncTCP`
     - `ArduinoJson` (by Benoit Blanchon)

3. **Board Configuration**
   - Board: "Generic ESP8266 Module"
   - Flash Size: "1MB (FS:64KB OTA:~470KB)"
   - Flash Mode: "DIO"
   - Flash Frequency: "40MHz"
   - CPU Frequency: "80MHz"
   - Upload Speed: "115200"

### First Flash (via FTDI)

1. **Wire the ESP-01S**:

   ```
   FTDI 3.3V  →  ESP-01S 3V3
   FTDI GND   →  ESP-01S GND
   FTDI TX    →  ESP-01S RX
   FTDI RX    →  ESP-01S TX
   FTDI GND   →  ESP-01S GPIO0 (boot mode)
   ```

2. **Compile and Upload**:
   - Open `weather_clock.ino`
   - Sketch → Upload
   - Wait for "Done uploading"
   - Remove GPIO0-to-GND jumper
   - Press reset or power cycle

3. **Initial Setup**:
   - Device creates AP: "TJ56654-Setup"
   - Connect to it (password: `12345678`)
   - Captive portal opens automatically
   - Select your WiFi network and enter password
   - Device reboots and connects

### Subsequent Updates (OTA)

1. **Via Web Interface** (easiest):
   - Browse to `http://192.168.x.x/update` (find IP from router)
   - Or use mDNS: `http://tj56654-clock.local/update`
   - Login: `admin` / `admin`
   - Choose .bin file from `build/` folder
   - Click "Update"
   - Device reboots automatically (~15 seconds)

2. **Via Arduino IDE**:
   - Tools → Port → Select "tj56654-clock at 192.168.x.x"
   - Sketch → Upload
   - No wires needed!

---

## Web Interface

### Home Page (`/`)

Current time display with live updates via JavaScript (fetches `/api/time` every second).

### Configuration Page (`/config`)

Comprehensive settings form:

**WiFi Settings**

- SSID
- Password
- Hostname (for mDNS)

**Time Settings**

- Timezone offset (seconds from UTC)
- DST enabled (European rules)
- NTP server address
- NTP sync interval (seconds)
- Hour format (12h/24h)

**Weather Settings**

- Enabled/disabled toggle
- Latitude
- Longitude
- City name (for display)
- Update interval (seconds)

**Display Settings**

- Brightness (0-7)
- Rotation (0°, 90°, 180°, 270°)
- Display rotation interval (seconds)
- Show weather screen (toggle)
- Show sunrise/sunset screen (toggle)

All settings persist to EEPROM and survive reboots.

### Debug Page (`/debug`)

Real-time diagnostics:

- **System**: Uptime, free heap, chip ID, flash size
- **WiFi**: SSID, IP, signal strength, MAC address, gateway, DNS
- **Time**: Current time, timezone, DST status, NTP sync status
- **NTP Stats**: Last sync, attempts, successes, failures
- **Weather**: Temperature, sunrise/sunset, last update, API status
- **Network Tests**: Internet connectivity, DNS resolution
- **Display**: Current mode, rotation, brightness

Perfect for troubleshooting connectivity or API issues.

---

## API Documentation

All endpoints return JSON (except `/update` which is for file upload).

### `GET /api/time`

Current time information.

**Response:**

```json
{
  "current": "14:23:45",
  "date": "2026-01-03",
  "day": "Friday",
  "timezone_offset": 0,
  "dst_active": false
}
```

### `GET /api/status`

System status overview.

**Response:**

```json
{
  "wifi": {
    "ssid": "MyNetwork",
    "ip": "192.168.1.47",
    "rssi": -38,
    "hostname": "tj56654-clock"
  },
  "time": {
    "current": "14:23:45",
    "timezone_offset": 0,
    "ntp_synced": true
  },
  "system": {
    "uptime": 3627,
    "free_heap": 35104,
    "chip_id": "f77134"
  }
}
```

### `GET /api/weather`

Current weather data.

**Response:**

```json
{
  "temperature": 15.4,
  "city": "Portimao",
  "sunrise": "07:48",
  "sunset": "17:29",
  "daylight_hours": 9,
  "daylight_minutes": 41,
  "last_update": "14:20:00",
  "valid": true
}
```

### `GET /api/config`

Export full configuration as JSON.

**Response:**

```json
{
  "ssid": "MyNetwork",
  "timezone_offset": 0,
  "dst_enabled": true,
  "brightness": 5,
  "ntp_server": "pool.ntp.org",
  "ntp_interval": 3600,
  "hour_format_24": true,
  "hostname": "tj56654-clock",
  "latitude": 37.19,
  "longitude": -8.54,
  "city_name": "Portimao",
  "weather_enabled": true,
  "weather_interval": 1800,
  "display_rotation_sec": 5,
  "show_weather": true,
  "show_sunrise_sunset": true,
  "display_orientation": 0
}
```

### `POST /api/config`

Import configuration from JSON.

**Request Body**: Same structure as export response (password field optional for security).

**Response:**

```json
{
  "status": "ok"
}
```

Device automatically reboots after import.

### `POST /api/eeprom-clear`

Factory reset (clears EEPROM).

**Response:**

```json
{
  "status": "cleared"
}
```

Device reboots to WiFiManager captive portal.

### `POST /api/reboot`

Remote reboot.

**Response:**

```json
{
  "status": "rebooting"
}
```

Device reboots immediately.

---

## Security Improvements

### What Changed from Original Firmware

| Issue                  | Original                       | Custom Firmware                   |
| ---------------------- | ------------------------------ | --------------------------------- |
| **WiFi Password Leak** | Plaintext in open AP           | No open AP after setup            |
| **Persistent AP**      | Always active                  | Only on first boot or failure     |
| **API Keys**           | QWeather requires registration | Open-Meteo (no key needed)        |
| **Cloud Dependency**   | Chinese servers                | Direct API calls, no intermediary |
| **Firmware Updates**   | Manual FTDI only               | OTA via WiFi (password-protected) |
| **Config Access**      | No authentication              | Admin password required           |
| **Code Transparency**  | Closed source                  | Open source (you're reading it!)  |

### Best Practices Implemented

1. **WiFiManager Timeout**: AP automatically closes after 180 seconds if no configuration
2. **Fallback AP Mode**: If credentials fail, device creates secure AP ("TJ56654-Clock" with password)
3. **EEPROM Validation**: Magic number check prevents loading corrupted data
4. **Input Sanitization**: Buffer overflow protection on all user inputs
5. **Memory Safety**: No dynamic String allocations in loops, fixed-size buffers
6. **Error Handling**: Graceful degradation (e.g., display shows time even if weather fails)

### Recommended Post-Flash Steps

1. **Change OTA password**: Edit line ~60 in `.ino` file:

   ```cpp
   ArduinoOTA.setPassword("admin");  // Change this!
   ```

2. **Change web admin password**: Edit line ~430:

   ```cpp
   if (!server.authenticate("admin", "admin")) {  // Change this!
   ```

3. **Set strong WiFi AP fallback password**: Edit line ~780:

   ```cpp
   WiFi.softAP("TJ56654-Clock", "12345678");  // Change this!
   ```

4. **Disable unnecessary features**: If you don't need weather, disable it in `/config` to save bandwidth

---

## Credits

**Hardware**: TJ-56-654 Weather Clock Kit ([AliExpress](https://pt.aliexpress.com/item/1005008333782531.html))

**Firmware**: Written from scratch with love and frustration

**Libraries Used**:

- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [AsyncHTTPRequest_Generic](https://github.com/khoih-prog/AsyncHTTPRequest_Generic)
- [NTPClient](https://github.com/arduino-libraries/NTPClient)

**APIs**:

- [Open-Meteo](https://open-meteo.com/) - Free weather API, no registration required

**Tools**:

- Arduino IDE 2.x
- FTDI FT232RL USB-to-Serial adapter
- Lots of coffee ☕

---

## Project Structure

```
esp8266-weather-clock/
├── firmware/
│   └── weather_clock/              # Modular firmware
│       ├── weather_clock.ino       # setup() and loop()
│       ├── config.h                # Config struct, EEPROM layout
│       ├── globals.h               # Shared state
│       ├── display.cpp             # OLED rendering
│       ├── ntp_client.cpp          # Async NTP + DST
│       ├── weather.cpp             # Open-Meteo API
│       ├── web_server.cpp          # HTTP UI + REST API
│       └── wifi_manager.cpp        # WiFi resilience
├── tests/
│   └── test_device.py              # HW-in-the-loop test suite (73 cases)
├── docs/
│   ├── HARDWARE.md                 # Hardware specifications
│   └── INSTALLATION.md             # Flashing guide
├── CHANGELOG.md                    # Version history
└── README.md                       # This file
```

## Testing

```bash
# Hardware-in-the-loop test suite — verifies API, fuzzes config endpoint,
# checks heap stability over 5 samples
python3 tests/test_device.py 192.168.x.x
```

The test suite covers:

- All REST API endpoints (functional validation)
- Boundary fuzz against `/config` (oversized SSID, invalid intervals, etc.)
- Malformed JSON fuzz against `/api/config` import
- Heap stability check (must stay >20KB, drift <2KB across runs)

---

## License

This project is released into the public domain. Do whatever you want with it. If you improve it, consider sharing your changes - that's how we make IoT better.

---

---

**Author**: apetrochenko · **License**: MIT · **Firmware**: v1.9.10
