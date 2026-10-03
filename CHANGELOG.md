# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.11.0-beta.4] - Unreleased

### Added

- Choose a published version with All / Stable / Beta filters, including a
  same-version reinstall or an explicit downgrade with PIN confirmation.
- Show transition consequences: loss of GitHub updates, manual BIN recovery,
  UV/extra screens and audited settings compatibility. Unknown storage profiles
  and withdrawn releases cannot be installed from the catalog.
- Add a bounded release history, audited capability profiles and history-only
  publication without moving the legacy latest-channel pointers backwards.

### Changed

- Keep schema-1 `channels.json` small and compatible with beta.2/3; publish the
  new history and verified images in the same commit. No extra release attachments.
- Refresh uses a unique catalog request and displays its publication timestamp.

### Fixed

- Stop treating a successful upload as a completed update: wait for reboot and
  check the installed version; report uncertainty instead of automatically retrying.
- Allow selecting stable even when its version number is below the installed beta.

See [beta.4 notes](docs/releases/v1.11.0-beta.4.md). Hardware OTA/rollback tests pending.

## [1.11.0-beta.3] - 2026-10-03 (pre-release)

### Changed

- Bump the firmware version to provide a newer beta for testing the GitHub
  update flow from beta.2: discovery, PIN confirmation, installation and the
  reported version after reboot.
- Firmware behavior, web assets, settings storage and maintenance PIN are
  otherwise unchanged from beta.2. No new features or bug fixes are included.

Use **Stable + Beta** in the Update page. See the
[beta.3 test instructions](docs/releases/v1.11.0-beta.3.md).
Physical OTA, reboot/recovery and long-running hardware validation remain pending.

## [1.11.0-beta.2] - 2026-10-03 (pre-release)

### Added

- GitHub release checks for Stable and Stable + Beta, with PIN-confirmed
  installation from the Update page and browser-side target, size and SHA-256 checks.
- A same-repository publisher for verified update catalogs and firmware images,
  with canonical build selection and protection against channel downgrades.
- Pinned ESLint/Ruff checks, EditorConfig/clang-format guidance and ASan/UBSan
  across all seven host suites in CI.

### Changed

- Replace the unused stock updater page with a compact HTTP adapter using the
  ESP8266 core writer. Manual firmware/filesystem uploads remain available;
  the PIN stays on the local device connection and is never sent to GitHub.
- Clarify screen presets, rename Time & weather to Combined clock + UV, mark
  manual screen choices as Custom, and explain unavailable Clock-page options.
- Share the HTTP JSON reader, avoid repeated weather conversions and sun-label
  formatting, and store the weather URL format in flash. The firmware is
  2,176 bytes smaller and static RAM is 368 bytes lower than the preceding local
  beta.2 build; memory budgets remain unchanged.
- Keep release downloads to the firmware, SHA256SUMS and BUILD_INFO.txt.
  Release notes, screenshots and the complete OLED gallery live in the repository.

### Fixed

- Show Saved only after non-restarting settings changes have been read back
  and verified; preserve edits for retry if values differ or verification fails.
- Prevent firmware finalization for incomplete or multiple-file upload requests;
  commit only after the complete authenticated request has been received.

Verified build: 476,496-byte firmware and 38,464-byte static RAM. Settings and
maintenance PIN storage remain compatible. See [beta.2 release notes](docs/releases/v1.11.0-beta.2.md).
Physical OTA testing and long-running hardware validation remain pending.

## [1.11.0-beta.1] - 2026-10-03 (pre-release)

- Add optional combined clock/weather, outdoor comfort, hourly rain, two-day
  min/max/UV, wind direction, moon and next-sun-event countdown.
- Add a dedicated daytime UV peak screen with today/tomorrow values and levels,
  plus a visible web summary; preserve screen IDs and feature EEPROM layout.
- Add screen presets, per-page durations, hold/next/direct selection, configurable
  transitions, temperature/wind units and scheduled night dimming.
- Extend local status/control APIs and add one opt-in external card with bounded
  text, update rate and TTL; provide a Home Assistant package example.
- Add browser-only city search, reviewed settings import and private diagnostic
  export while retaining the compact default settings layout.
- Preserve legacy settings/PIN storage and API units. Bound weather response and
  parser memory; distinguish download age from source age.
- Recover flash through pinned build-time minification and compact setup portal
  resources; add regressions and 320 native OLED renders.

See [beta release notes](docs/releases/v1.11.0-beta.1.md). Published as a pre-release; physical
OTA, provisioning and long-running beta validation are still pending.

## [1.10.0] - 2026-10-03

PIN-only maintenance, a refreshed web interface, night mode and
network recovery fixes. This entry includes the local 1.9.11–1.9.13 development
builds; they were not separate published releases. The previous public release is
1.9.10. See the [release notes](docs/releases/v1.10.0.md) for upgrade instructions.

### OLED revision in the same release

Refreshed on 2026-10-03, superseding the first `5ae2387ac626` build while retaining
the v1.10.0 release number. The release gallery includes all 408 before/after renders.

- Fit OLED text to the actual orientation: prevent automatic wrapping in headings,
  clipped long city/SSID names, and the stray last letter in `No Data`. Long city
  labels wrap within their footer; short-word breaks never discard a long remainder.
- Decode UTF-8 before drawing city/SSID labels; add compact Russian glyphs and
  Portuguese tildes, reuse native Latin-1 glyphs, and correct the degree sign.
- Validate the existing 31-byte city limit in the browser, with a UTF-8 byte counter.
  The settings API and EEPROM layout are unchanged; 55-character names remain invalid.
- Replace ambiguous connecting dots/stars with a native Wi-Fi wave animation and
  an explicit `Connecting` label, reusing the existing connection timing.
- Replace standalone sunrise/sunset arrows with native 16×16 sun/horizon symbols
  and small direction cues (64 bytes in flash).
- Center clock/status content, mark 12-hour time with AM/PM, label ArduinoOTA
  progress with `%`, and rename physical WiFi recovery to `WiFi reset`.
- Add reproducible OLED previews using the real Adafruit_GFX rasterizer: 204
  screen/orientation combinations, clipping checks, UTF-8 and name-length cases,
  optional sanitizers, and a generated gallery. Run these checks in CI.
- Prevent the dependency installer from silently replacing the pinned GFX version;
  the firmware build and OLED previews now enforce the same 1.12.4 library.

### Added

- Optional OLED night schedule using synchronized local time, including windows
  across midnight. A separate versioned EEPROM record leaves legacy settings
  intact and defaults to disabled on upgrade. PIN/test display actions wake the OLED.
- Persistent random six-digit maintenance PIN, shown as `xxx-xxx` on the physical
  display for 30 seconds when requested. No PIN screen or delay is added at startup.
- Native 16×16 weather icons in flash, with corrected overcast/thunderstorm mapping
  and distinct partly-cloudy and unknown conditions. Bitmap integration was adapted
  with night mode and startup orientation from Stibax's fork; the eight current
  glyphs are hand-drawn local replacements, with hollow clouds and distinct
  precipitation silhouettes. See [provenance](docs/BACKPORTS.md).
- CI artifacts containing a version/commit-named `.bin`, SHA-256 and build metadata
  after pinned host/browser/build checks and memory budgets pass. No automatic
  release publication.

### Changed

- Replace the browser login with a PIN field. The UI checks the PIN before sending
  firmware; the upload guard authenticates chunks and final POST before the core
  writer. Legacy Basic-auth scripts remain compatible; ArduinoOTA uses six digits.
- Redesign the English desktop/mobile interface around Clock, Settings and Update,
  with a single firmware selector, progress, error states and on-demand diagnostics.
- Fit all settings into four compact desktop groups with aligned labels and visible
  NTP options. Track unsaved changes, send only changed fields and support local
  Discard; keep touch controls large and focused fields clear of the sticky action bar.
- Serve a self-contained gzip page from flash with ETag caching. No framework,
  external fonts or filesystem image; browser seconds tick locally and status
  polls once per minute only while the dashboard is visible.
- Use one asynchronous DNS/UDP NTP path. Web time text and OLED share local time;
  API epoch stays UTC. Remove the NTPClient library dependency.
- Keep the last good weather reading on failure with a stale marker and age.
- Redraw static OLED screens only when needed; use bounded dissolve masks, skip
  transitions to the same screen, and use deadlines for connection/test overlays.
- Validate form/JSON settings consistently before saving. Keep ordinary settings
  open; omit WiFi passwords from pages, exports and application logs. Blank or
  omitted passwords preserve the saved one; `clear_password:true` selects an open network.
- Keep the legacy Config/PIN EEPROM layout. Commit validated legacy and night
  settings together and report failed storage before applying runtime changes.
- Shorten README while retaining the original discovery story; split user/API
  references into dedicated guides and correct installation, recovery and memory docs.

### Fixed

- Normalize the gzip OS marker so embedded web assets pass consistency checks
  across Python versions and build hosts.
- Weather recovery after HTTP/JSON/open errors and watchdog timeout; retry bursts
  return to normal scheduling and cancel stalled transport.
- NTP startup DNS recovery, duplicate requests, late DNS callbacks and clock rollover.
- Missing/empty firmware uploads return HTTP 400 without starting OTA or rebooting.
  Core validation errors with HTTP 200 are shown as failures in the browser.
- Brightness zero remains visible. This repairs the black-screen regression in
  the intermediate 1.9.11-dev build; out-of-range legacy values are clamped.
- Apply saved orientation and brightness before drawing the startup frame.
- Clear SDK WiFi credentials during recovery/reset, preventing unwanted reconnect
  through the SDK cache. Physical recovery retains other settings and the PIN;
  HTTP full reset clears them.

### Validation

- Host regressions cover network failures, DNS/timers, settings atomicity, PIN
  storage/authentication, upload guard ordering, night windows and OLED scheduling.
- Chromium exercises actual UI sources with a simulated API, including widths down
  to 320px, uploads, maintenance dialogs, data escaping and hidden-tab polling.
- Hardware tests are read-only by default; non-network settings fuzz is opt-in.
- See [validation notes](docs/VALIDATION.md) for measured builds and remaining
  hardware OTA, night-mode, runtime-heap and long-duration checks.

## [1.9.10] - 2026-09-20

### Fixed

- **WiFi password lost after captive-portal setup, no reconnect after reboot** (#12):
  after a successful connect via SDK-cached credentials (Try 1) or the WiFiManager
  portal, only the SSID was written to EEPROM. On the next boot `config.password`
  was empty, so `setupWiFi()` and the reconnect loop called `WiFi.begin(ssid)` as
  if the network were open and failed with `WL_WRONG_PASSWORD`. v1.9.6 masked this
  through the bare `WiFi.begin()` fallback in the retry loop, which 02834ba (v1.9.7)
  replaced with `WiFi.begin(config.ssid)`. Now the password is read back with
  `WiFi.psk()` and saved next to the SSID in all three places that sync the SSID.
  Devices already stuck recover by entering the password once in the web UI.

## [1.9.9] - 2026-05-19

### Fixed

- **Factory reset atomicity**: cleared WiFi credentials are now committed BEFORE
  the reset counter is zeroed. If power fails between the two writes, the device
  still boots into WiFiManager AP on next start (cleared creds win over stale counter)
  instead of being stuck in an inconsistent state
- **`isValidSSID` rejected single-char SSIDs**: length-1 networks like "A" were
  incorrectly flagged as all-same garbage. Now allowed (per 802.11 spec)
- **`ntp_interval` config changes ignored until reboot**: `NTPClient` is constructed
  in `setup()` with this value and never re-initialized. Now triggers `needsRestart`
  when the interval actually changes
- **Open WiFi (no password) failed to connect at setup** (M2): Try-2 block in
  `setupWiFi()` required both ssid and password; now falls back to `WiFi.begin(ssid)`
  when password is empty
- **Unseeded PRNG, identical dissolve pattern every boot** (M4): `randomSeed()`
  now called with `ESP.getChipId() ^ micros()` — varies between devices and boots

### Removed

- **Dead `NTPState` enum values** (M3): `NTP_WAITING`, `NTP_SUCCESS`, `NTP_FAILED`
  were defined but never assigned. Removed to reduce code surface area

## [1.9.8] - 2026-05-19

### Fixed

- **handleConfigSave always rebooted device on save**: only reboots now if WiFi/network
  fields changed (`ssid`, `password`, `hostname`, `ntp_server`). Other settings
  (brightness, timezone, intervals, coordinates, display options) apply live without
  restart. Eliminates reboot cascades during config tweaks and makes the test suite
  safe to run repeatedly.

## [1.9.7] - 2026-05-19

### Fixed

- **Config endpoint accepted garbage values, bricking device** (M1): `/config` form handler
  now validates all inputs. SSID rejected if empty, >31 chars, non-printable, or all-same-char
  (fuzz garbage like "AAAA..."). Numeric fields are `constrain()`-ed to safe ranges
  (`ntp_interval`/`weather_interval`: 60–86400, `brightness`: 0–7, `timezone`: ±12h,
  `latitude`/`longitude`: physical ranges, `display_orientation`: 0–3). Invalid input
  returns HTTP 400 instead of silently saving and rebooting.

## [1.9.6] - 2026-05-18

### Fixed

- **Weather hangs permanently after TCP timeout** (C1): `WEATHER_REQUESTING` state now has a
  15-second watchdog — if the HTTP callback never fires (NAT timeout, server half-close),
  state resets to IDLE and retry logic resumes
- **All retry timers freeze at ~49 days** (C2): Replaced unsafe `millis() >= nextRetryTime`
  with subtraction-safe `(millis() - nextRetryTime) < 0x80000000UL` in RetryConfig and
  WiFiRetryConfig; fixed boot guard with static flag
- **DST switches on wrong day** (H1): Last-Sunday formula was using year-only heuristic;
  now computes weekday of the 31st from current `tm_wday`: verified correct for 2026–2030
- **Heap fragmentation from web API polling** (H2): Replaced String+= concatenation with
  `snprintf`+`sendContent` in `handleAPITime`, `handleAPIStatus`, `handleAPIDebug`,
  `handleAPIWeather` — eliminates permanent heap fragmentation from 1s JS polling
- **Race condition on shared state** (H3): Added `volatile` to `weatherState` and `ntpState`
  — prevents compiler from caching stale values across ESPAsyncTCP callback boundaries
- **Malformed JSON on long error messages**: `lastError` clamped to 79 chars before snprintf

### Changed

- RAM usage: 37,268 bytes (46%) — down from 37,560 due to String elimination in API handlers

## [1.9.5] - 2026-05-18

### Fixed

- **Temperature disappears permanently** (#9): After 3 consecutive API failures,
  the weather state machine was permanently locked — `weatherState` stayed `WEATHER_FAILED`
  and no new requests were ever made even after the API recovered. Fix: reset retry
  counter and state after max retries so periodic refresh resumes after next interval (30 min).

## [1.9.4] - 2026-05-14

### Fixed

- **Date timezone** (#5): Date string now uses local time, so date rolls over at local midnight instead of UTC midnight
- **Weather periodic refresh** (#7): Removed `WEATHER_SUCCESS` state that permanently blocked periodic weather updates after first fetch; also fixed "Last update" counter showing raw timestamp instead of elapsed seconds
- **WiFi connects to wrong network** (#3): `WiFi.begin()` without params is now skipped when a saved SSID exists, preventing connection to SDK-cached open hotspots and config corruption

### Changed

- WiFi startup: SDK-cached credentials only attempted on first boot (no saved SSID); subsequent boots go directly to saved credentials
- ArduinoJson: `StaticJsonDocument` → `JsonDocument` for compatibility with ArduinoJson v7

### Removed

- Unused `weekday` variable in NTP DST calculation (compiler warning)

### Internal

- Split `clock_ntp_ota_v1.9/` directory renamed to `weather_clock/` — version no longer baked into path
- Removed internal development documents from repository

## [1.9.3] - 2026-01-06

### Changed

- Refactored monolithic 2,100-line `.ino` into modular structure:
  `display.cpp`, `ntp_client.cpp`, `weather.cpp`, `web_server.cpp`, `wifi_manager.cpp`

## [1.9.2] - 2026-01-06

### Fixed

- **CRITICAL**: WiFi credentials no longer cleared on connection failure
  - Previous behavior erased SSID/password after failed connection attempts
  - Now credentials persist indefinitely through WiFi outages
- OTA update credential loss issue (SDK credentials now tried first, then EEPROM)

### Added

- **WiFi Resilience**: Infinite retry with exponential backoff (5s → 10s → 20s → ... → 5min max)
- **Fallback AP**: "TJ56654-Setup" enabled after ~5 min of failed attempts
  - Device continues retry attempts while AP is active (dual STA+AP mode)
  - AP automatically disabled when WiFi reconnects
- **"No WiFi" display**: Shows retry countdown instead of numeric counter
- **"!" indicator**: Shown in date line when WiFi is disconnected
- **SDK credentials support**: Tries WiFiManager-stored credentials first

### Changed

- Clock continues running with last synced time during WiFi outages
- Improved user experience during network failures
- Removed aggressive credential clearing behavior

### Network Activity

- NTP sync: 1 hour interval (pool.ntp.org:123 UDP)
- Weather fetch: 30 min interval (api.open-meteo.com HTTP)
- mDNS: continuous (224.0.0.251 UDP multicast)
- ~50 requests/day total

## [1.9.1] - 2026-01-03

### Fixed

- **CRITICAL**: WiFi startup sequence - synchronous connection in setup() to ensure proper initialization order
- Display blank screen for 10+ seconds on boot (now shows time after ~15 seconds)
- "DNS resolution failed" errors during startup
- Sunrise/sunset labels cut off on 128px screen (removed labels, arrows are self-explanatory)

### Changed

- Hybrid WiFi model: synchronous in setup(), async reconnect in loop()
- Display formatting: superscript degree symbol and lowercase 'c' for temperature
- Sunrise/sunset screen now shows daylight duration (e.g., "Day 9h 41m") instead of static "Sun Times" text

### Documentation

- Added detailed v1.9.1_HYBRID_FIX.md explaining startup sequence problem and solution

## [1.9.0] - 2026-01-02

### Added

- Fully async architecture (zero blocking operations in loop)
- Custom async NTP implementation (manual UDP packet handling)
- Async HTTP weather fetch (AsyncHTTPRequest library)
- Exponential backoff retry logic for network failures
- Independent epoch tracking for accurate time between NTP syncs

### Changed

- Replaced blocking NTPClient with custom async UDP implementation
- Replaced blocking HTTP weather with AsyncHTTPRequest
- Removed all delay() calls from loop()
- WiFi connection now async (later fixed in v1.9.1)

### Performance

- Loop time: 10ms → <1ms (10x improvement)
- Weather fetch: 1-10s blocking → 0ms
- NTP sync: 5-20s blocking → 0ms
- WiFi reconnect: 15s blocking → 0ms
- OTA updates now work during active weather fetching

### Technical

- RAM usage: +536 bytes (36,980 → 37,516)
- Flash usage: +1040 bytes (407,500 → 408,540)
- IRAM: 61,987 bytes (94% - stable)

## [1.8.0] - 2026-01-01

### Security

- **CRITICAL**: Removed hardcoded WiFi credentials
- Integrated WiFiManager for secure captive portal setup
- Added config validation (magic number check)
- Input sanitization to prevent buffer overflows

### Fixed

- IRAM overflow crisis (94% → 70% via ICACHE_FLASH_ATTR)
- NTP interval bug (config value was ignored, always used hardcoded 1 hour)
- Boolean parsing errors in JSON config import/export
- Infinite loop protection in display mode rotation
- Memory leaks from String concatenation in web handlers

### Changed

- Web responses now use chunked transfer (eliminated 140+ String concatenations)
- Applied ICACHE_FLASH_ATTR to 26 functions (moved code from IRAM to Flash)
- Improved error handling throughout codebase

### Performance

- Peak heap usage reduced by ~8KB
- EEPROM validation prevents loading corrupted config

## [1.7.0] - 2025-12-31

### Added

- Initial working firmware with correct display support
- NTP time synchronization
- Weather data from Open-Meteo API (free, no API key required)
- OTA update support (web-based and ArduinoOTA)
- Web interface (/, /config, /debug, /update)
- REST API (time, status, weather, config export/import)
- Display rotation (time, weather, sunrise/sunset modes)
- Timezone support with manual DST configuration
- EEPROM configuration persistence

### Hardware Discovery

- Identified display as GM009605v4.3 (not TM1637 or TM1650)
- Discovered swapped I2C pins: SDA=GPIO0, SCL=GPIO2
- Switched to Adafruit_SSD1306 library

### Replaced

- QWeather API → Open-Meteo (no registration required)
- Proprietary firmware → Open source custom firmware
- Insecure WiFi handling → WiFiManager with timeout

## [1.6.0] - 2025-12-30 (unreleased)

### Attempted

- TM1650 LED driver support (incorrect - device has OLED)

## [1.5.0] - 2025-12-29 (unreleased)

### Attempted

- TM1637 7-segment display support (incorrect - device has OLED)

---

## Version Numbering

- **Major version** (X.0.0): Breaking changes, incompatible config format
- **Minor version** (1.X.0): New features, backward-compatible
- **Patch version** (1.9.X): Bug fixes, no new features

---

**Status**: v1.9.3 is production-ready and actively used 24/7.
