# Build, validation and upgrade notes

## 1.11.0-beta.2 local validation

Prepared on 2026-10-03; not yet published or flashed. The pinned isolated Linux
build passed with **478,672 bytes** (560 bytes below the same OTA limit), **38,832
bytes static RAM** and **62,007 / 65,536 instruction bytes including cache**.
The embedded web page is 59,519 bytes before gzip and 18,329 bytes in flash.
Firmware is 48 bytes larger than beta.1 with the browser updater and verified saves.

Passed: all host regressions; the new HTTP update harness with ASan/UBSan;
browser SHA-256 comparisons against Node crypto through 479,232 bytes; semantic
version/catalog checks; publication validation, including rebuilt releases;
the actual minified UI in Chromium at desktop and 320/390-pixel widths; insecure
HTTP hashing, PIN-before-download, corrupt/oversized downloads, local fallback and
credential isolation. An actual browser successfully fetched a public raw GitHub
file from an insecure HTTP origin. Both existing public release images were
staged locally and matched their metadata and SHA-256 digests.

Additional settings regressions passed for mismatched readback after an OK POST,
unavailable readback, retry, float32 coordinate rounding, restart without readback
and saved screen availability. Preset explanations and the laptop layout passed.
On the user's installed early beta.1, enabling the weather pages restored rotation;
the user confirmed the physical display changed. Toggling Wind off/on through
the real browser form and reloading retained both saves. This verifies the installed
build's current behavior, not the new beta.2 code; the original reset was not reproduced.

Embedded-asset reproducibility, script syntax, workflow YAML parsing and diff
whitespace checks passed. The new publication workflow has not run on GitHub;
its integration requires default-branch installation and channel publication.
No physical OTA/reboot/power-loss test was performed. OLED drawing and provisioning
were not changed; their dedicated raster/portal suites were not repeated for beta.2.

## 1.11.0-beta.1 pre-release validation

Local checks completed on 2026-10-03 using the pinned toolchain below:

| Check | Result |
| --- | --- |
| ESP-01S binary | **478,624 bytes**, below the conservative 479,232-byte OTA budget by **608 bytes** |
| Static RAM | **39,044 bytes**, below the 45,000-byte project budget |
| Instruction region including 32 KB cache | **62,007 / 65,536 bytes** (94.62%, below 95%) |
| Embedded web page | 52,041 bytes before gzip, **15,246 bytes** in flash after minification/gzip |
| Host regressions | Passed, including feature EEPROM isolation, atomic validation, source age, parser allocation exhaustion, optional/misaligned forecast arrays, screen timing, card expiry, UV levels/date rollover/missing values and reserved-byte migration |
| Actual OLED raster | **320 renders**, zero automatic wraps/clipped cells/clipped ink; ASan/UBSan passed |
| Chromium against embedded minified UI | Passed desktop/mobile, import preview, presets, dirty-state preservation, forecast/units/control, UV levels/missing/stale values, hidden-tab polling and PIN/upload guards |
| Compact setup portal | Upstream templates/scripts tested at 320 px, including 32-character wide SSID, open/locked selection and password visibility |
| Home Assistant example | YAML and 10 Jinja templates parsed; rendered REST command bodies are valid JSON |
| Live provider sample | Public-coordinate Open-Meteo response: 1,213 bytes, 6 hours/2 days; parsed by the production module in the host harness |
| Reproducibility checks | Clean pinned npm installation, regenerated header matches, JS/Python syntax and diff whitespace checks passed |

The binary was built in an isolated Linux ARM container from a frozen source
snapshot, with Arduino CLI 1.4.1/core 3.1.2 and the pinned libraries. The final
source identity and SHA-256 are included with the release package. The
small remaining OTA margin is a measured build limit, not a live free-space probe.
Do not add dependencies or increase memory budgets to hide a size regression.

No beta was flashed. A read-only status check reached the user's existing 1.10.0
clock (time synchronized, valid cached weather); that does **not** validate this
beta. Physical OTA, captive-portal provisioning, OLED appearance/brightness,
Home Assistant end-to-end operation and a 24-hour soak remain pending. The
[GitHub Actions history](https://github.com/petrochen/esp8266-weather-clock-opensource/actions?query=branch%3Acodex%2Fweather-clock-beta)
records the release commit's automated checks separately from these local results.
The beta tag includes the integration guide, source and all README demo assets.


## Reproducible build

Verified target: ESP8266 core **3.1.2**, `generic`, **80 MHz**, **DIO**,
**1MB / 64KB filesystem**, default **32KB instruction cache**.
Arduino CLI **1.4.1**, GCC **10.3**.

Pinned libraries:

| Library | Version |
| --- | --- |
| ArduinoJson | 7.4.2 |
| Adafruit BusIO | 1.17.4 |
| Adafruit GFX Library | 1.12.4 |
| Adafruit SSD1306 | 2.5.16 |
| WiFiManager | 2.0.17 |
| ESPAsyncTCP | 1.2.4 |
| AsyncHTTPRequest_Generic | 1.13.0 |

NTPClient is no longer required. The GitHub workflow installs these versions and
runs host and browser regressions, the firmware build and size budgets. Successful
runs attach a version/commit-named binary, SHA256SUMS and build information. The
workflow does not flash a device or publish a release.

The CLI install uses `--no-deps` with the complete ESP8266 library list above.
Otherwise installing SSD1306 can silently replace the requested GFX 1.12.4 with a
newer dependency version. The OLED regression runner verifies the installed GFX
version before rendering. The first v1.10.0 CI build allowed this replacement;
the refreshed build corrects it.

```sh
npm ci  # Node.js 22; esbuild 0.28.2 is build-only
python3 tools/embed_web.py --check
arduino-cli compile \
  --fqbn esp8266:esp8266:generic:eesz=1M64,FlashMode=dio,xtal=80 \
  --warnings all --build-path build firmware/weather_clock
python3 tests/run_host_tests.py --arduinojson /path/to/ArduinoJson/src
python3 tests/render_display.py --arduinojson /path/to/ArduinoJson/src \
  --gfx /path/to/Adafruit_GFX_Library --sanitize
python3 -m pip install playwright==1.58.0
python3 -m playwright install chromium
python3 tests/test_web.py
python3 tests/test_portal.py --wifimanager /path/to/WiFiManager
python3 tests/check_build_size.py build \
  --size-tool /path/to/xtensa-lx106-elf-size
```

Host tests compile the production settings and network/display/maintenance modules
with substituted hardware APIs. They check retry recovery/exhaustion, rollover,
transport aborts, synchronous callbacks, invalid weather data, DNS recovery, late
DNS callbacks, single-request NTP, timezone/DST boundaries, settings atomicity,
JSON round-tripping, password omission, maintenance storage/authentication, static
frame suppression and dissolve coverage. Brightness tests capture the actual
OLED command arguments for all eight levels and out-of-range legacy values.
The display and maintenance modules also run together through startup, code
expiry, offline placeholders, full screen transitions and timed overlays; text
capture checks rendering dispatch, not the physical font raster or I2C transport.
PIN regressions cover six-digit generation, leading zeros, stable storage,
one-time migration from the old credential record, invalid credentials, both web
spellings, OLED expiry across rollover and a secret-free on-demand HTTP response.
Night regressions validate erased/corrupt EEPROM migration, isolation from the
legacy Config/PIN/reset records, failed saves, every minute of midnight/daytime
windows, equal endpoints, unsynchronized time and local offsets. Display tests
verify single OFF commands, suppressed transitions, temporary PIN/test wake-up,
expiry and morning redraw. All supported weather-code groups, including the
separate partly-cloudy glyph, are checked. Each category is drawn at native 16×16
size outside the temperature, stale-marker and city areas. Previews decoded from
the actual bitmap bytes were inspected at 1× and enlarged without smoothing,
including complete 128×64 layouts; physical OLED legibility still needs checking.

`tests/render_display.py` additionally executes the actual screen drawing code
with the pinned Adafruit_GFX rasterizer and classic font. Only the Arduino Print
and SSD1306 hardware boundaries are substituted. It renders 51 scenarios in all
four orientations (204 PNGs), writes `build/display-preview/index.html`, and fails
on automatic line wraps, out-of-bounds character cells or clipped drawing. Cases
include all main/status/setup/recovery/PIN/test/ArduinoOTA screens, missing/stale
data, all eight weather icons, 12-hour boundaries, long SSID/IP/city labels,
UTF-8, both temperature limits, night blanking and dissolve frames. All accepted
ASCII city lengths 0–31 are checked too. The optional sanitizers catch memory and
undefined-behavior errors in the host run. The color band follows the physical
panel rotation; the previews do not simulate optical blur, brightness, I²C,
real upload timing or actual hardware. The browser/core HTTP updater retains its
existing progress behavior; the labeled OLED percent screen belongs to ArduinoOTA.

The update handler rejects missing or zero-byte files before starting OTA.
From beta.2, firmware/filesystem byte streams, writer errors, interrupted full-size
images, multiple-file rejection, per-request state and commit timing are tested with
a substituted core writer. The HTTP adapter authenticates each chunk and the final
POST. The core retains image validation/writing; it does not provide a second
authentication layer. See [update-channel checks](UPDATES.md#checks).

`python3 tests/test_web.py` executes the actual UI in Chromium against a simulated
API: settings round-trip, empty file, wrong-PIN preflight, successful upload,
HTTP-200 core validation errors, restart/reset dialog, cancelled reset, secret-free
browser storage, stale/unsynchronized data, safe rendering and hidden-tab polling.
Settings checks cover dirty state, local Discard, changed-only payloads, validation,
save failures and the unsaved-navigation warning. All 22 controls and Save fit at
1280×800 and 1366×768. Widths of 320, 390, 640, 768 and 900px pass overflow checks;
keyboard traversal keeps every setting above the sticky action bar at 320px and
640×400 (the reflow viewport of a 1280×800 screen at 200% zoom). Desktop/mobile
screenshots were inspected. These tests do not emulate the SDK, radio, flash-write
failures beyond injected cases, or physical OLED timing.

## Upgrade from 1.9.10

- The legacy Config layout and saved WiFi settings are unchanged.
- In 1.9.12-dev, brightness 0-7 maps to contrast 32-255. A saved zero no longer
  selects the SSD1306's zero-contrast dim mode. In 1.9.11-dev, restore a dark
  display immediately by setting brightness to 4 through the Config page.
- Use the old firmware's credentials for the upgrade itself. On the first boot of
  1.9.13-dev, a six-digit random PIN is saved separately, replacing any long code
  from 1.9.11/12 exactly once. Other configuration is retained.
- In 1.10.0 the web page asks only for a PIN, with or without the grouping
  dash. ArduinoOTA uses six digits only. Press **Show PIN on clock** on the Update
  page or restart/reset dialog to show `xxx-xxx` on the OLED for 30 seconds. There
  is no PIN screen at startup. HTTP/Serial/export never return it. The PIN survives
  restart/OTA; HTTP **Reset all settings** removes it. Physical triple-power-cycle
  WiFi recovery retains it. Legacy `admin` Basic-auth scripts remain compatible.
- Night mode is disabled on upgrade. Its independent record uses EEPROM offset
  416; legacy Config and PIN storage keep their layouts. It stays inactive before
  the first time synchronization. The PIN button temporarily wakes the OLED.
- Ordinary settings and read-only APIs still require no login.
- Configuration export omits the WiFi password. Empty/omitted passwords preserve
  the existing one; `clear_password=true` explicitly selects an open network.
  An export alone cannot configure WiFi credentials on a different device.
- `/api/time.time` and `/api/status.time.current` now use the configured local
  timezone like the OLED. `/api/time.epoch` remains UTC; consumers that relied on
  the old UTC text should use epoch. `synced` is false before the first sync.
- Weather `valid` means a successful cached reading exists; it remains true on a
  transient failure. Check new `stale` and `age_seconds` to determine freshness.
- JSON import applies the same validation as the form and returns HTTP 400 for
  malformed data. Network identity changes reboot; other fields apply live.

## Local validation on 2026-10-03

Full target compilation succeeded with the pinned dependencies above. On this
Apple Silicon Mac, the installed macOS Xtensa compiler could not execute; the
same GCC 10.3 toolchain's official Linux ARM build was used in an isolated Linux
container. The final build emitted no compiler warnings. Host regression groups
and size budgets passed; Python syntax and `git diff --check` passed.

The baseline was rebuilt from aee1d3e with the same options and libraries (it is
not assumed byte-identical to the published release artifact).

| Metric | Baseline 1.9.10 | First 1.10.0 build | Refreshed 1.10.0 (local build) |
| --- | ---: | ---: | ---: |
| Binary | 455152 B | 472272 B | 474304 B |
| Static RAM | 37664 B | 36956 B | 37068 B |
| IRAM code + instruction cache | 61987 B | 62007 B | 62007 B |

Relative to the rebuilt public 1.9.10 baseline, static RAM is 596 bytes smaller
and the image is 19152 bytes larger, including the reliability fixes, backports,
web interface and display revision. Compared with the first 1.10.0 build at
`5ae2387ac626`, the refreshed image adds 2032 bytes and 112 bytes of static RAM;
instruction-region usage is unchanged.

The eight weather icons occupy 256 bytes in flash. The new Wi-Fi animation uses
144 bytes, the sunrise/sunset pair 64 bytes, and the additional text glyphs and
Latin-1 mapping 414 bytes. There is no additional framebuffer or font library,
heap allocation for text layout, scrolling timer or startup wait.

The compressed web bundle is 11892 bytes in flash, streamed without copying the
whole page into RAM; uncompressed sources are 43243 bytes. The image has 4928 bytes
left below the conservative 479232-byte OTA limit. Instruction-region usage
remains 94.62%, with 252 bytes before the 95% threshold. These are build
measurements, not runtime heap or timing results; the combined status response
and new display behavior still need a hardware smoke test.

GitHub build results and artifact revisions are linked from the
[release page](https://github.com/petrochen/esp8266-weather-clock-opensource/releases/tag/v1.10.0).

Earlier hardware checks on 1.9.11-dev confirmed working NTP/weather APIs and passed
a 30-request heap smoke test. The saved brightness was zero; changing only brightness
to 4 restored the OLED without restarting or changing other settings. This
reproduced the startup regression subsequently covered by the contrast test.
These observations apply to that development build, not the prepared 1.10.0 image.
No device was flashed during local validation of this release.

The prepared 1.10.0 binary, night schedule, PIN migration and hardware OTA with both
PIN spellings, the physical OLED at minimum brightness, hardware failure recovery
and a multi-day soak still require testing. The earlier build's API results do
not validate the new binary on hardware.

## Hardware validation still required

Run read-only smoke tests with an explicit device address:

```sh
python3 tests/test_device.py 192.168.x.x
```

`--fuzz` additionally changes non-network settings, compares stored values,
checks rejection is atomic, and restores an actual backup in `finally`. It does
not update firmware, reset the device, or alter WiFi credentials. Restoration can
still fail if connectivity is lost; use a development device.

Before considering a release verified on hardware:

1. Upgrade a configured 1.9.10 and a 1.9.11/12 device; verify WiFi survives and the
   six-digit PIN is readable and unchanged after a reboot. Check on-demand display
   and all four display orientations.
2. Verify unauthenticated/wrong-PIN OTA and reset requests fail, both web PIN
   spellings work, and ArduinoOTA accepts the six digits. Verify factory-reset
   provisioning and generation of a new PIN.
3. Introduce DNS failure, WiFi outage, HTTP 502 and an interrupted weather response;
   restore connectivity and verify recovery without manual intervention.
4. Run a 24-72 hour soak with web polling and display rotation; record maximum loop
   latency, free heap, largest free block and fragmentation.
5. Check weather icons, negative temperatures, OLED contrast and all supported
   orientations on the physical panel, including startup orientation.
6. Enable a short night window spanning midnight; verify OFF/ON, PIN/test wake-up,
   WiFi recovery and uninterrupted weather/time updates. Check DST transitions.
7. Reopen cached pages after an update and verify ETag/304 handling, displayed
   firmware version, PIN preflight, upload progress and the restart flow.

A compiled image and passing host tests do not substitute for these checks.

## Prepared release

The release version is **1.10.0**; the source-of-truth macro is
`FIRMWARE_VERSION` in `firmware/weather_clock/config.h`. The
[release description](releases/v1.10.0.md) covers changes since the last public
release, 1.9.10, and includes the local intermediate dev builds.

Local packaging includes the verified `.bin`, SHA256SUMS, source hashes, a source
snapshot and release notes. Its `local-<digest>` name identifies the uncommitted
source snapshot; it must not be mistaken for a published Git commit or tag.
Publishing remains a separate step. GitHub release notes should retain the
hardware-validation limits until those checks have actually been completed.
