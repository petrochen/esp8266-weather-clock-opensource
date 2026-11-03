# Installation Guide

## Source version and beta builds

This source tree targets **[1.11.0-beta.1](https://github.com/petrochen/esp8266-weather-clock-opensource/releases/tag/v1.11.0-beta.1)**,
a pre-release for testing. The latest stable release remains **1.10.0**.
Publishing the beta does not mean it has completed hardware validation. See the [beta notes](releases/v1.11.0-beta.1.md)
for compatibility and remaining checks. It uses the same single Firmware `.bin`
and existing PIN, without a filesystem upload or factory reset.


Complete step-by-step guide to flash this firmware on your ESP8266 weather clock.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Arduino IDE Setup](#arduino-ide-setup)
3. [Hardware Connection](#hardware-connection)
4. [First Flash (via FTDI)](#first-flash-via-ftdi)
5. [Initial Configuration](#initial-configuration)
6. [OTA Updates](#ota-updates)
7. [Troubleshooting](#troubleshooting)
8. [Recovery and reset](#recovery-and-reset)
9. [Verifying installation](#verifying-installation)

---

## Prerequisites

### Hardware Required

- **ESP8266 Weather Clock** (TJ-56-654 or compatible)
- **FTDI USB-to-Serial adapter** (3.3V!)
  - Recommended: FT232RL, CP2102, CH340
  - ⚠️ Must support 3.3V - 5V adapters will damage ESP8266
- **Jumper wires** (male-to-female, 5 pieces)
- **USB cable** (for FTDI adapter)

### Software Required

- **Arduino IDE** (1.8.19+ or 2.x)
  - Download: https://www.arduino.cc/en/software
- **USB drivers** for your FTDI chip:
  - FT232: https://ftdichip.com/drivers/vcp-drivers/
  - CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
  - CH340: Usually auto-installed on macOS/Linux

---

## Arduino IDE Setup

### 1. Install ESP8266 Board Support

**Method 1: Via Board Manager (recommended)**

1. Open Arduino IDE
2. Go to: **File → Preferences**
3. In "Additional Board Manager URLs", add:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
4. Click **OK**
5. Go to: **Tools → Board → Boards Manager**
6. Search: "ESP8266"
7. Install: **esp8266 by ESP8266 Community** (version **3.1.2**, used for the verified build)
8. Wait for installation to complete

**Method 2: Manual Installation**

See: https://arduino-esp8266.readthedocs.io/en/latest/installing.html

### 2. Install Required Libraries

Go to: **Sketch → Include Library → Manage Libraries**

Install **Adafruit BusIO**, **Adafruit GFX Library**, **Adafruit SSD1306**,
**WiFiManager**, **ArduinoJson**, **ESPAsyncTCP** and **AsyncHTTPRequest_Generic**.
Use the exact versions listed in [Build validation](VALIDATION.md#reproducible-build)
to reproduce the release. NTPClient is no longer used.

**Installation steps for each library:**

1. Search library name in Library Manager
2. Click **Install**
3. Wait for "INSTALLED" badge
4. Repeat for all libraries

### 3. Board Configuration

**Important**: Configure these settings **before** compiling:

1. Go to: **Tools → Board → ESP8266 Boards**
2. Select: **Generic ESP8266 Module**
3. Configure settings:

| Setting           | Value                      | Why                                      |
| ----------------- | -------------------------- | ---------------------------------------- |
| Flash Size        | `1MB (FS:64KB OTA:~470KB)` | Layout used by the verified OTA build      |
| Flash Mode        | `DIO`                      | Compatible with most ESP-01S modules     |
| Flash Frequency   | `40MHz`                    | Safe default for all ESP8266             |
| CPU Frequency     | `80MHz`                    | Standard (can use 160MHz for more speed) |
| Crystal Frequency | `26MHz`                    | Default for ESP-01S                      |
| Upload Speed      | `115200`                   | Balance between speed and reliability    |
| Debug Level       | `None`                     | Reduces firmware size                    |
| lwIP Variant      | `v2 Lower Memory`          | Better for 1MB flash devices             |
| Erase Flash       | `Only Sketch`              | Preserves config on re-flash             |

---

## Hardware Connection

### Step 1: Identify Pins

Use the labels on your module/adapter rather than assuming a header orientation.
Identify **3V3, GND, TX, RX and GPIO0** before connecting power. See the
[hardware pin functions](HARDWARE.md#pin-functions) and module documentation.

### Step 2: Wire FTDI to ESP-01S

**Connections:**

| FTDI Pin | ESP-01S Pin | Wire Color | Notes                             |
| -------- | ----------- | ---------- | --------------------------------- |
| 3.3V     | 3V3         | Red        | Power (NOT 5V!)                   |
| GND      | GND         | Black      | Ground                            |
| TX       | RX          | Yellow     | Data: FTDI transmit → ESP receive |
| RX       | TX          | Green      | Data: FTDI receive → ESP transmit |
| GND      | GPIO0       | Blue       | **Programming mode** (temporary)  |

**⚠️ CRITICAL**:

- **Never connect 5V to ESP-01S** - it's not 5V tolerant!
- Double-check polarity before powering on
- GPIO0-to-GND connection is **temporary** (only for programming mode)

### Step 3: Enter Programming Mode

1. **Connect all wires** as shown above (including GPIO0 to GND)
2. **Plug FTDI into USB** (ESP-01S powers on in programming mode)
3. **Verify**: Some FTDI adapters have a power LED that should light up
4. **Remove GPIO0-to-GND jumper** (keep other connections)

ESP-01S is now in programming mode, ready to receive firmware.

---

## First Flash (via FTDI)

### Step 1: Open Project

1. Download or clone this repository
2. Navigate to: `esp8266-weather-clock-opensource/firmware/weather_clock/`
3. Open: `weather_clock.ino` in Arduino IDE

### Step 2: Verify Board Settings

1. Go to: **Tools → Board → Generic ESP8266 Module**
2. Confirm settings match those in [Board Configuration](#3-board-configuration)
3. Go to: **Tools → Port**
4. Select your FTDI adapter:
   - macOS: `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`
   - Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
   - Windows: `COM3`, `COM4`, etc.

If port doesn't appear:

- Check USB cable is data-capable (not charge-only)
- Install FTDI drivers
- Try different USB port
- Check wire connections

### Step 3: Compile Firmware

1. Click: **Sketch → Verify/Compile** (or press Ctrl+R / Cmd+R)
2. Wait for compilation (1-2 minutes)
3. Check output for:
   ```
   Sketch uses X bytes (X%) of program storage space.
   Global variables use Y bytes (Y%) of dynamic memory.
   ```
4. Verify:
   - The resulting `.bin` is at most **479232 bytes**, the project's conservative OTA budget
   - Instruction RAM plus its 32KB cache stays below **95% of 65536 bytes**
   - Use the [build-size checker](VALIDATION.md#reproducible-build) for exact results

### Step 4: Upload Firmware

1. **Ensure GPIO0 was grounded during power-on** (then removed)
2. Click: **Sketch → Upload** (or press Ctrl+U / Cmd+U)
3. Watch serial monitor for:
   ```
   Connecting........
   Chip is ESP8266EX
   Uploading stub...
   Running stub...
   Writing at 0x00000000... (X %)
   ```
4. Wait for: **Hard resetting via RTS pin...**
5. Success message: **Done uploading**

**If upload fails**, see [Troubleshooting](#upload-fails).

### Step 5: Power Cycle

1. **Disconnect FTDI from USB**
2. **Remove GPIO0-to-GND wire** (very important!)
3. **Reconnect FTDI to USB** (ESP-01S boots into normal mode)
4. Firmware should now be running!

---

## Initial Configuration

### Step 1: Connect to Device AP

1. On your phone/laptop, scan for WiFi networks
2. Look for: **TJ56654-Setup** (or similar)
3. Password: `12345678`
4. Connect to this network

### Step 2: Captive Portal

**Automatic (iOS/Android):**

- Captive portal should pop up automatically
- If not, manually browse to: http://192.168.4.1

**Manual (laptop):**

- Browse to: http://192.168.4.1

### Step 3: Configure WiFi

1. Click: **Configure WiFi**
2. Select your home network from the list
3. Enter WiFi password
4. Click: **Save**
5. The clock continues startup on your WiFi. Set its device name later in Settings.

### Step 4: Find Device IP

**Method 1: Router Admin Panel**

- Log into your router
- Look for device: "tj56654-clock"
- Note its IP address (e.g., 192.168.1.47)

**Method 2: mDNS (if your OS supports it)**

- Browse to: http://tj56654-clock.local/
- Works on macOS, Linux, iOS out-of-box
- If name resolution is unavailable, use the numeric IP address from your router.

**Method 3: Serial Monitor**

1. Keep FTDI connected (no GPIO0 to GND!)
2. Open: **Tools → Serial Monitor**
3. Set baud rate: **115200**
4. Press reset button (if available) or power cycle
5. Watch for: `WiFi connected! IP: 192.168.x.x`

### Step 5: Access Web Interface

Browse to: `http://<device-ip>/` or `http://tj56654-clock.local/`

You should see **Clock**, **Settings** and **Update** navigation, with time and
weather on the home page. **Device details** contains diagnostics and tests.

### Step 6: Configure Settings

Open **Settings**, select the base UTC offset and daylight-saving rule, set your
weather location, and adjust the display. Press **Save settings**. Most changes
apply immediately; changing WiFi credentials, hostname or NTP server restarts the
clock. Night mode is off by default.

The [user guide](USAGE.md) describes every section, PINs and settings backup. The
web UTC selector uses hours/minutes; direct API offsets use seconds.

---

## OTA Updates

After initial FTDI flash, all future updates can be done **over WiFi** (no wires!).

### Method 1: Web Interface (Easiest)

1. Download the clock `.bin` from a [published release](https://github.com/petrochen/esp8266-weather-clock-opensource/releases). When supplied, verify `SHA256SUMS` from the same release.
2. Browse to: `http://<device-ip>/update`
3. Choose the firmware `.bin` file.
4. Press **Show PIN on clock** and read the six digits on the OLED.
5. Enter the PIN (with or without the dash). No username is needed.
6. Press **Upload & restart**, keep power connected, and wait for success.
7. The clock restarts automatically. Check the version on the Clock page.

The web interface is included in the firmware. Leave the advanced Filesystem
option unused unless you deliberately have a separate filesystem image.
An older installed firmware may still ask for `admin` and its existing code for
this first upgrade; the new PIN-only page becomes available after that upgrade.

### Method 2: Arduino IDE

1. Open `.ino` file in Arduino IDE
2. Go to: **Tools → Port**
3. Select: **tj56654-clock at 192.168.x.x** (network port!)
4. Click: **Sketch → Upload**
5. Enter the six-digit maintenance PIN when asked (no dash for ArduinoOTA)
6. Wait for upload and automatic restart

**Note**: Network port only appears if device is online and mDNS is working.

### Method 3: curl (Command Line)

```bash
# Legacy Basic-auth compatibility: curl prompts for the PIN.
# Use one explicit binary path, not a wildcard.
curl -u admin -F "firmware=@/path/to/firmware.bin" http://192.168.x.x/update
```

Replace:

- `192.168.x.x` with your device IP
- `/path/to/firmware.bin` with actual path to .bin file

---

## Troubleshooting

### Upload Fails

**Error: "espcomm_open failed"**

- Check: GPIO0 was grounded during power-on
- Check: FTDI driver installed
- Try: Different USB port
- Try: Lower upload speed (57600 instead of 115200)

**Error: "espcomm_upload_mem failed"**

- Check: Wire connections (especially RX↔TX swap)
- Check: FTDI is 3.3V (not 5V)
- Try: Power ESP-01S from external 3.3V supply (FTDI may not provide enough current)

**Error: "Chip sync error"**

- GPIO0 must be LOW during boot
- Try: Hold GPIO0 to GND, reset ESP, then release GPIO0

### Compilation Fails

**Error: "library not found"**

- Install missing library via Library Manager
- Restart Arduino IDE after installing

**Error: "Sketch too big"**

- Flash size must be set to 1MB
- Use the pinned libraries and exact [build target](VALIDATION.md#reproducible-build)
- Check the `.bin` and IRAM budgets; a web setting does not remove code from the binary

**IRAM overflow error**

- Compare the build target and dependencies with [validation notes](VALIDATION.md)
- Check whether a source change placed more code in IRAM; the remaining margin is small

### WiFi Connection Fails

**Device creates AP but won't connect to home WiFi**

- ESP8266 only supports 2.4GHz (not 5GHz)
- Try: Different WiFi channel (1, 6, or 11)
- Check: WiFi password is correct
- Check: Router supports 802.11n

**Device reboots in a loop**

- Likely: Power supply too weak (brownout)
- Solution: Use powered USB hub or different power adapter
- Minimum: 500mA @ 5V

### Display Issues

**Display is blank**

- Check: I2C wiring (SDA=GPIO0, SCL=GPIO2)
- Check **Settings → Display** for an enabled night schedule. **Show PIN on clock** temporarily wakes it.
- In the intermediate 1.9.11-dev build, set brightness to 4 to recover from the zero-brightness bug; 1.10.0 keeps level 0 visible.
- The firmware tries I2C addresses 0x3C and 0x3D at startup
- Test: Use `/api/i2c-scan` endpoint to detect display

**Display shows garbage**

- Wrong display library or initialization
- This firmware is for SSD1306-compatible OLED
- Verify display model is GM009605v4.3 or similar

**Display is upside down**

- Change `display_orientation` in `/config`
- Values: 0 (normal), 1 (90°), 2 (180°), 3 (270°)

### Time Not Syncing

**Time shows --:--**

- Check: WiFi is connected (`/api/status`)
- Check: NTP server is reachable (default: pool.ntp.org)
- Check: Router firewall allows UDP port 123
- Try: Different NTP server (e.g., time.google.com)

**Time is wrong by hours**

- Check: Timezone offset in `/config`
- The web selector uses UTC hours/minutes; the API uses **seconds**
- European DST adds one hour; disable it where those rules do not apply

### Weather Not Updating

**Weather is missing or marked as a saved reading**

- Check: Internet connectivity (`/api/debug`)
- Check: Latitude/longitude are correct
- Check: Open-Meteo API is accessible (visit https://open-meteo.com/ in browser)
- Check `valid`, `stale` and `age_seconds` in `/api/weather`; the firmware retries failed requests
- There is no manual `/test-weather` route

### OTA Update Fails

**Web upload hangs at 0%**

- Check: Device is online and responsive
- Select a non-empty firmware `.bin` for this board and enter the PIN shown on the OLED
- Try: Upload via Arduino IDE instead

**Upload finishes but reports an error**

- An image can reach 100% upload and still fail validation. Read the final result.
- The core can return `Update error:` with HTTP 200; that is not a successful update.
- If the connection drops, the result is unknown. Check the clock and its version
  before retrying; keep power connected while a write may still be in progress.

### Serial Monitor Shows Errors

**"DNS resolution failed"**

- Check the configured NTP hostname, router DNS and Internet access
- Version 1.10.0 retries startup DNS failures without a manual reboot

**Watchdog reset / exception**

- Likely: Code bug or memory corruption
- Check: IRAM usage < 95%
- Report: Open issue with serial log

---

## Recovery and Reset

### Recover WiFi with three quick power cycles

If the network has changed or WiFi credentials no longer work, start the clock
three times, cutting power before each of the first two runs reaches ten seconds:

1. Power on, then off within ten seconds.
2. Power on again, then off within ten seconds.
3. Power on a third time and leave it running.

The third boot shows `WiFi reset` (`FACTORY RESET!` in the first v1.10.0 build and older versions)
and clears both saved and SDK WiFi credentials.
Despite that screen label, **other settings and the maintenance PIN are retained**.
Connect to **TJ56654-Setup**, password `12345678`, and configure WiFi through the
portal at `http://192.168.4.1/`.

### Recover through the fallback AP

If station connection repeatedly fails, the firmware can also enable
**TJ56654-Setup** while retrying. A first-setup portal timeout can fall back to
**TJ56654-Clock**. Both use `12345678`. On these fallback APs, open
`http://192.168.4.1/config` and save the corrected network details.

The WiFiManager setup portal has a 180-second timeout. The separate fallback AP
is not covered by that timer. A successful station connection returns to normal
station operation. See [Architecture](ARCHITECTURE.md).

### Reset everything

Use **Settings → Restart & reset → Reset all settings**, show/read the PIN, and
confirm. This clears the configuration and PIN record as well as SDK WiFi credentials.
The next boot starts setup with defaults and generates a new PIN.

For a device that cannot boot or show its PIN, use the [FTDI first-flash procedure](#first-flash-via-ftdi).
Erasing the entire flash is a last resort and also removes all settings and credentials.
Do not use the WiFi-only power-cycle recovery as a way to reset a night schedule or PIN.

For PIN formats, night mode and optional display screens, see the [user guide](USAGE.md).

## Verifying Installation

Run the test suite against your device:

```bash
python3 tests/test_device.py 192.168.x.x
```

The default suite is read-only and includes a 30-request heap smoke test.
Use `--fuzz` to temporarily test non-network settings with backup and restoration.
Passing this suite does not establish multi-day stability. See [validation](VALIDATION.md).

## Getting Help

If you're still stuck:

1. **Check existing issues**: https://github.com/petrochen/esp8266-weather-clock-opensource/issues
2. **Open new issue** with:
   - Arduino IDE version
   - ESP8266 board package version
   - Library versions
   - Relevant serial output, with network names/addresses or credentials redacted as needed
   - Steps to reproduce
3. **Join discussion** for general questions

---

**Happy flashing!** 🚀
