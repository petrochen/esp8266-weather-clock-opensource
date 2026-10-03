# Hardware Documentation

## Device Specifications

### Original Product
- **Name**: ESP8266 Mini Weather Clock Kit
- **Model**: TJ-56-654
- **Source**: [AliExpress Link](https://pt.aliexpress.com/item/1005008333782531.html)
- **Purchase price in the project story**: about €5 (not a current price quote)
- **Dimensions**: 40mm x 40mm x 43mm

### Components

#### ESP-01S WiFi Module
- **Chip**: ESP8266EX
- **Flash**: 1MB (8Mbit)
- **Memory**: separate data RAM and instruction/cache regions; see [Memory map](#memory-map)
- **CPU**: 80MHz (can be overclocked to 160MHz)
- **WiFi**: 802.11 b/g/n (2.4GHz only)
- **GPIO**: 2 usable pins (GPIO0, GPIO2)
- **Voltage**: 3.3V (NOT 5V tolerant!)

#### Display Module
- **Model**: GM009605v4.3
- **Type**: OLED (Organic LED)
- **Resolution**: 128x64 pixels
- **Size**: 0.96 inches diagonal
- **Driver used by this firmware**: Adafruit SSD1306; SH1106 operation is not verified
- **Interface**: I2C
- **I2C Address**: 0x3C (default), 0x3D (fallback)
- **Framebuffer**: one bit per pixel; display color zones depend on the fitted panel

#### Power Supply
- **Input**: 5V via Micro-USB
- **Regulator**: Onboard 3.3V LDO (on main PCB)
- **Current**: depends on WiFi activity and OLED content; full-board consumption has not been measured for this release

#### Case
- **Material**: Transparent acrylic
- **Pieces**: 6 (top, bottom, 4 sides)
- **Assembly**: Brass standoffs and M2.5 screws

## Pinout

### Identifying pins

Use the printed labels on the module or programming adapter. Do not infer header
orientation from a generic ASCII diagram: the earlier diagram in this document
incorrectly repeated GND and omitted reset. The wiring tables below name signals,
not physical pin positions.

### Pin Functions

| Pin | Standard Use | This Project |
|-----|--------------|--------------|
| 3V3 | Power (3.3V) | Power |
| GND | Ground | Ground |
| TX | UART TX | Serial debug output |
| RX | UART RX | Serial input (flashing) |
| GPIO0 | General I/O | **I2C SDA** (data line) |
| GPIO2 | General I/O | **I2C SCL** (clock line) |
| EN | Chip Enable | Must be held high for normal operation |
| RST | Reset, active low | Module reset; not used for I²C |

**⚠️ Important**: This project uses **non-standard I2C pin mapping**!
- Typical ESP8266: SDA=GPIO4, SCL=GPIO5
- **This device**: SDA=GPIO0, SCL=GPIO2

### I2C Connection

```
ESP-01S          OLED Display
─────────────────────────────
3V3      →       VCC
GND      →       GND
GPIO0    →       SDA
GPIO2    →       SCL
```

### Programming Connection (FTDI)

```
FTDI Adapter     ESP-01S
──────────────────────────
3V3      →       3V3
GND      →       GND
TX       →       RX
RX       →       TX
GND      →       GPIO0  (boot mode - connect only during programming)
```

**Programming Mode:**
1. Connect GPIO0 to GND
2. Power on the ESP-01S
3. Remove GPIO0-GND connection
4. Upload firmware
5. Power cycle to run new code

## PCB Layout

The main PCB (TJ-56-654) contains:
- ESP-01S socket (8-pin header)
- OLED display connector (4-pin header)
- 3.3V voltage regulator (AMS1117-3.3)
- Micro-USB connector for power
- Bypass capacitors

## Memory Map

### Flash memory: the verified 1M64 layout

This build uses ESP8266 Arduino core 3.1.2's
[`eagle.flash.1m64.ld`](https://github.com/esp8266/Arduino/blob/3.1.2/tools/sdk/ld/eagle.flash.1m64.ld).
Physical flash offsets below are derived from its memory-mapped addresses by
subtracting `0x40200000`:

| Start | End (exclusive) | Use |
| --- | --- | --- |
| `0x00000` | `0xEB000` | Sketch and available OTA staging space, subject to core alignment/reservations |
| `0xEB000` | `0xFB000` | 64KB filesystem area |
| `0xFB000` | `0xFC000` | EEPROM emulation sector |
| `0xFC000` | `0xFD000` | RF calibration |
| `0xFD000` | `0x100000` | SDK WiFi/system parameters |

There is no fixed pair of 470KB A/B application partitions. OTA needs free sketch
space for a sector-aligned incoming image; the project enforces a conservative
**479232-byte binary budget**. Web assets are in the firmware, not the filesystem.
The firmware uses 512 bytes of EEPROM emulation; record offsets inside that buffer
are documented in [Architecture](ARCHITECTURE.md#eeprom-compatibility).

### RAM and instruction cache

Data RAM and instruction/cache memory are separate budgets. Under the default
32KB cache configuration, the build report counts IRAM code plus the 32768-byte
cache against a 65536-byte region. Static data, heap and stack use the data region;
free heap is measured at runtime, not inferred from the flash size or IRAM report.
See [Espressif's memory explanation](https://www.espressif.com/en/products/socs/esp8266ex/resources)
and the [actual build measurements](VALIDATION.md#local-validation-on-2026-10-03).

## Power Consumption

This release has no whole-board power measurements. WiFi transmission, OLED content,
contrast and the board regulator all affect consumption. Night mode switches the
OLED off; it does not put the ESP8266 into deep sleep, and network work continues.
Do not interpret chip-only sleep figures as consumption of the assembled clock.

## Hardware Modifications

### Optional Improvements

1. **External antenna**: Solder U.FL connector for better WiFi range
2. **Temperature sensor**: Add DHT22 or BME280 to GPIO (requires software changes)
3. **Buttons**: Add physical buttons for display control (requires free GPIO)
4. **Battery backup**: Add 18650 cell + TP4056 charger for UPS functionality

### Pin Availability

ESP-01S has very limited GPIO:
- **GPIO0**: Used for I2C SDA (can't use for other purposes)
- **GPIO2**: Used for I2C SCL (can't use for other purposes)
- **TX/RX**: Can be repurposed (breaks serial console)

For additional peripherals, consider upgrading to ESP-12F or ESP32.

## Troubleshooting

### Display not working
- Check I2C address: Try 0x3C and 0x3D
- Verify pin mapping: SDA=GPIO0, SCL=GPIO2
- Check power: Display needs 3.3V
- Test with I2C scanner (`/api/i2c-scan`)

### WiFi connection fails
- ESP8266 only supports 2.4GHz (not 5GHz)
- Check power supply: Weak USB port can cause brownouts
- Some routers don't like ESP8266 - try different channel

### Bootloop/crashes
- Check IRAM usage: Must be <95%
- Verify flash mode: Should be "DIO" not "QIO"
- Bad power supply: Use quality USB cable

### Can't flash firmware
- GPIO0 must be LOW during boot for programming mode
- Some FTDI adapters need DTR/RTS wiring for auto-reset
- Baud rate: Try 115200 (default) or 57600 if errors

## Datasheets

- [ESP8266EX Datasheet](https://documentation.espressif.com/0a-esp8266ex_datasheet_en.html)
- [ESP-01S Pinout](https://components101.com/wireless/esp8266-pinout-configuration-features-datasheet)
- [SSD1306 OLED Controller](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)

## Safety Warnings

⚠️ **Do NOT connect 5V to ESP-01S GPIO pins** - they are NOT 5V tolerant!

⚠️ **Use 3.3V FTDI adapter** - 5V will permanently damage the ESP8266

⚠️ **Check polarity** - Reversing power can destroy the module

⚠️ **ESD sensitive** - Touch grounded metal before handling board
