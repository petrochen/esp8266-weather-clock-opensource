# Architecture Notes

## 1.11 beta extensions

A separate `FeatureSettings` record (magic FTR1, 32 bytes at EEPROM 432) keeps the
legacy Config ABI untouched. Invalid records load conservative defaults without a
migration write. One settings transaction validates Config, night and features
before committing. External card data, hold state and forecasts live only in RAM. UV uses two former
reserved bytes (show flag and duration) without changing record size/magic;
previous beta records have these bytes zeroed. Existing screen IDs 0–7 remain
stable; UV is ID 8.

The weather request fetches current conditions, six hourly slots and two daily
slots using Unix UTC timestamps. A 3072-byte transport check aborts oversized
responses; `BoundedJsonAllocator` separately limits parser allocations to 4096
bytes on ESP8266 (plus small allocation headers; host budget scales with pointer
size). Current fields are required, optional data is explicitly unavailable.
A successful response replaces the forecast. Display conversions leave stored
values/API units canonical. Sun times/countdowns use the clock's offset/DST.

Eight normal screen IDs plus one transient card share the scheduler. Zero dwell
means inherit, availability gates selection, and expiring a held card returns to
the clock. PIN/test overlays and night rules retain precedence. Dimming issues
contrast commands only when needed; static pages still suppress redundant frames.

JS/CSS are minified by pinned esbuild before deterministic gzip embedding. The
ESP8266 core 3.1.2 global-header mechanism applies `WM_NODEBUG`, `WM_NOHELP` and a
compact WiFiManager stylesheet across library compilation. The stock portal
forms/scripts/routes remain intact; signal labels replace bitmap CSS sprites.
These flags are source-controlled in `weather_clock.ino.globals.h`, not local
compiler preferences. Recheck flash/IRAM/RAM budgets when changing dependencies.


## Runtime and dependencies

`setup()` loads the existing EEPROM configuration, initializes WiFi, maintenance
credentials, web/OTA services, and the UDP socket. Initial WiFi provisioning is
synchronous; operation after setup uses state machines for weather, DNS, and NTP.
The synchronous web server, OLED I2C transfers, EEPROM writes, OTA implementation,
and explicit reboot handlers can still occupy the loop; this is not a hard
real-time system.

- **Weather:** `processWeather()` owns scheduling, retry consumption and the 15s
  watchdog. HTTP errors, malformed payloads and failed request creation all return
  to IDLE and schedule up to three retries (1s, 2s, 4s). Exhaustion waits a full
  configured interval. The watchdog aborts the transport before scheduling retry.
- **Weather cache:** keep the last valid reading during errors, mark it stale and
  expose `stale`/`age_seconds`. A `*` on the OLED marks stale data. Weather fetching
  does not depend on NTP having succeeded.
- **NTP:** lwIP callback DNS lookup followed by one UDP request. Lookups and replies
  time out, retries are bounded, and stale DNS callbacks are ignored by generation.
  Replies must match the server, source port and originate token, and have usable
  NTP status. No second NTPClient request is made.
- **Time:** one UTC epoch anchor; the loop advances it while preserving fractional
  milliseconds, including across `millis()` rollover. OLED and web formatting use
  the same timezone/DST conversion. `/api/time.epoch` remains UTC.
- **WiFi:** existing boot provisioning and background reconnection are retained.

## Timers

Network retries use explicit `pending` flags, so a deadline equal to zero at the
49.7-day millis rollover is valid. `uint32_t(now - deadline) < 0x80000000UL`
compares short-lived deadlines safely. Starting a request consumes its retry;
periodic scheduling cannot bypass a pending backoff.

## Display

128x64 SSD1306 over `Wire.begin(0, 2)` (SDA GPIO0, SCL GPIO2).
Clock frames update at 2 Hz. Static weather and sun screens update only when
invalidated. Network results, configuration and screen changes invalidate them.
Dissolve uses a bijection over 8192 pixels and direct buffer masks, with exactly
0%/100% hidden at the endpoints. No animation runs between identical modes.
Connection/test overlays and the maintenance-code screen use deadlines instead of
runtime delays. Maintenance text temporarily uses landscape orientation.
Configuration is loaded before the startup frame. Brightness zero remains visible.
Night mode switches the OLED off once, cancels transitions, and leaves network
processing active. It requires a synchronized clock and uses the same local offset
as the time display. Equal endpoints disable the window. PIN/test overlays wake
the panel temporarily; morning wake-up redraws it. Weather icons are 16×16 flash
bitmaps with WMO mapping and an explicit unknown state.

## EEPROM compatibility

`EEPROM.begin(512)` retains the legacy `Config` structure at offset 0 unchanged.

| Offset | Content |
| --- | --- |
| 0 | Legacy Config, validated by CONFIG_MAGIC |
| 384 | PIN magic and six-digit password; original 13-byte password field retained |
| 416 | NightSettings NIT1 record (enabled and start/end hours/minutes) |
| 432 | FeatureSettings FTR1 record, 32 bytes (1.11 beta) |
| 480 | Two-byte triple-power-cycle counter |

Compile-time assertions prevent overlap. Maintenance credentials are initialized
only when missing/invalid, independently of WiFi settings. Development build 1.9.13
introduced the record magic used by 1.10.0 to migrate older long codes once. The
six-digit PIN uses hardware randomness with rejection sampling and is stored with leading zeros intact. It
is not derived from the MAC address. Credentials are not exported or logged. If
persistence fails, OTA/reset access stays disabled.

Invalid/missing night records default to disabled without extending Config.
Triple power-cycle recovery clears saved and SDK WiFi credentials and retains
other settings. HTTP reset clears all stored configuration and SDK WiFi credentials.
HTTP factory reset clears the maintenance record too; the next boot creates a
new PIN. An ordinary restart or OTA update preserves it.

## Web and settings

Read-only APIs and normal configuration remain unauthenticated by explicit design
for a trusted home network. Web firmware upload, ArduinoOTA, reboot and EEPROM
reset require the maintenance PIN. Web authentication accepts both six digits
and the grouped `xxx-xxx` spelling; ArduinoOTA uses the canonical six digits.
The web UI sends `Authorization: Bearer <PIN>` and never stores the PIN in a URL,
cookie or browser storage. POST `/api/maintenance/verify` checks it before the
browser sends firmware bytes; this does not create a session. Every actual upload
chunk and final POST is independently authenticated by the outer guard. The core
updater is configured without credentials behind that guard and owns image
validation and flash writing. Registration order is a security boundary.
Legacy Basic-auth scripts with username `admin` remain compatible; errors do not
issue a browser authentication challenge. These remain local HTTP/OTA protocols,
not TLS endpoints.

The unauthenticated POST `/maintenance/pin` shows the PIN on the physical OLED
for 30 seconds without returning it in the HTTP response. The update page and
restart/reset dialog offer this action so reading the PIN does not require a reboot. There is
no automatic PIN screen at boot. The startup logo has no blocking pause, and
setup dismisses the initial IP overlay before entering the main loop. WiFi
status overlays leave an explicitly requested PIN screen visible until expiry.

Form and JSON updates share `updateSettings()`: parse/check a candidate and apply
only after all fields and the independent night-settings candidate pass. Both
records are committed together; a commit failure leaves runtime settings unchanged. Existing numeric clamping is retained; invalid types,
non-finite values and malformed strings are rejected. Empty/omitted WiFi passwords
preserve the saved value; `clear_password=true` explicitly clears it.

Exports omit passwords and include all ordinary settings. JSON uses ArduinoJson
with a buffered writer; browser-rendered device values use textContent. Settings responses and
logs never contain WiFi or maintenance secrets. Changing network identity reboots;
intervals, display settings and locations apply live. A changed weather location
cancels the old request and clears its cache.

The update page requires a non-empty file and disables the corresponding submit
button otherwise. A POST guard registered before the core updater rejects missing
or zero-byte uploads with HTTP 400. It defers the core upload START until the first
non-empty chunk; actual firmware/filesystem validation and writing remain in the
ESP8266 core. The guard must be registered before the stock updater handlers.

The pages share a static gzip bundle generated from `web/` by
`python3 tools/embed_web.py`. It streams from PROGMEM, includes no external
libraries/fonts and needs no filesystem. ETag/304 revalidation avoids repeated
transfers. The dashboard polls one combined `/api/status` per minute while visible;
seconds are calculated in the browser. Settings fetch configuration only when
opened. Diagnostics fetch additional data only on demand. Existing API routes
and fields remain; status adds epoch, offset, weather and display state.

## Memory and validation

The 1M64 flash layout must fit both the running image and an OTA image. Total
flash capacity alone is not the OTA budget. CI checks a conservative 479232-byte
binary limit, the existing <95% instruction-region limit (including the 32KB
instruction cache), and a 45KB static RAM budget.

Heap size, largest free block and fragmentation are exposed in `/api/status`.
Allocation does not itself prove a leak; long-duration hardware measurements are
still required. See [VALIDATION.md](VALIDATION.md) for pinned builds, host tests,
upgrade behavior and outstanding hardware checks.
