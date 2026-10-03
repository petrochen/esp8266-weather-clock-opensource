# Local API Reference

This reference follows the 1.10.0 handlers in
[`web_server.cpp`](../firmware/weather_clock/web_server.cpp),
[`settings.cpp`](../firmware/weather_clock/settings.cpp),
[`maintenance.cpp`](../firmware/weather_clock/maintenance.cpp) and
[`update_server.cpp`](../firmware/weather_clock/update_server.cpp).
Replace `CLOCK_IP` in examples with your device's address.

## Routes and access

| Method | Path | Purpose | PIN |
| --- | --- | --- | --- |
| GET | `/api/time` | Time and sync state | No |
| GET | `/api/status` | Combined time, weather, display and device snapshot | No |
| GET | `/api/weather` | Cached weather and sun times | No |
| GET | `/api/debug` | NTP/network diagnostics | No |
| GET | `/api/i2c-scan` | Perform an I²C scan | No |
| GET | `/api/config` | Export ordinary settings | No |
| POST | `/api/config` | Apply a partial JSON settings update | No |
| POST | `/maintenance/pin` | Show PIN physically for 30 seconds | No |
| POST | `/api/maintenance/verify` | Verify PIN without updating/restarting | Yes |
| POST | `/api/reboot` | Restart, preserving settings | Yes |
| POST | `/api/eeprom-clear` | Clear all settings and WiFi credentials, then restart | Yes |
| POST | `/update` | Multipart firmware/filesystem upload | Yes |

The `/`, `/config`, `/debug` and `/update` GET routes serve the shared gzip HTML UI,
with ETag revalidation. `/debug` expands diagnostics in the browser. Legacy
`POST /config` accepts form fields using the same validator. `GET /test-ntp` and
`GET /test-display` trigger their test and return a 303 redirect to `/debug`.

Successful `/api/*` responses are JSON; some validation failures are plain text.
The core upload response is HTML/text. Always inspect both status and response.
There is no `/test-weather` route.

## Time and status

```sh
curl http://CLOCK_IP/api/time
```

Example after synchronization:

```json
{"time":"13:48:00","hours":13,"minutes":48,"seconds":0,"epoch":1791031680,"synced":true}
```

`epoch` is UTC Unix seconds; text and hours/minutes/seconds use the configured local
offset and DST, in 24-hour form. Before synchronization, `synced` is false, `time`
is `--:--:--` and the numeric fields are zero. Treat sync state as authoritative.

`GET /api/status` groups fields as follows:

| Object | Fields |
| --- | --- |
| `wifi` | `ssid`, `ip`, `rssi` (dBm), `hostname` |
| `time` | `current` (local text), `timezone_offset` (base seconds), `ntp_synced`, `epoch` (UTC), `offset` (base plus current DST), `hour_format_24` |
| `system` | `firmware_version`, `uptime` (seconds), `free_heap`, `max_free_block` (bytes), `heap_fragmentation` (percent), `chip_id` |
| `display` | `night_active` (scheduled window, even while a PIN temporarily wakes the OLED) |
| `weather` | `enabled`, `valid`, `stale`, `temperature` (°C), `code` (WMO), `windspeed` (km/h), `age_seconds`, `city`, `sunrise`, `sunset` |

The browser interpolates time locally between status requests. Scripts should also
poll at a reasonable interval rather than sending a request per displayed second.

## Weather and diagnostics

`GET /api/weather` returns `enabled`, `valid`, `temperature`, `weathercode`,
`windspeed`, `last_update`, `stale`, `age_seconds`, `sunrise`, `sunset`,
`sunrise_minutes` and `sunset_minutes`.

- `valid` means a successful cached reading exists, including after a failed refresh.
- `stale` marks a failed or overdue refresh. Check it together with `age_seconds`.
- `last_update` is device uptime in milliseconds, **not** a Unix timestamp.
- Sunrise/sunset strings use local time; numeric sun fields are minutes since
  midnight. Strings start as `--:--` before calculation; don't rely on default
  numeric values at that point.
- The standalone weather route uses `weathercode`; the combined status uses `code`.

`GET /api/debug` returns `internet_connected`, `ntp_attempts`, `ntp_successes`,
`last_error`, `gateway` and `dns`. The connectivity flag reflects recorded network
request outcomes; it is not a new Internet reachability probe on each GET.

`GET /api/i2c-scan` returns `i2c_scan.devices` (each with hexadecimal `address` and
numeric `decimal`), `i2c_scan.count`, and `i2c_scan.oled_test` for `0x3C` and `0x3D`.
Use this on demand; it actively scans the bus.

## Configuration

`GET /api/config` returns `firmware_version` plus the fields below. It sets a download
filename of `clock-config.json`. Password and PIN values are never exported.

| Field(s) | Type / accepted range |
| --- | --- |
| `ssid`, `hostname` | Non-empty string, up to 31 UTF-8 bytes |
| `city_name` | String, up to 31 UTF-8 bytes; empty allowed |
| `ntp_server` | Non-empty string, up to 63 UTF-8 bytes |
| `timezone_offset` | Integer seconds, −43200 to 50400; legacy alias `timezone` accepted on input |
| `brightness` | Integer 0–7; zero remains visible |
| `latitude`, `longitude` | Finite numbers, −90…90 and −180…180 |
| `ntp_interval`, `weather_interval` | Integer seconds, 60–86400 |
| `display_rotation_sec` | Integer seconds, 1–60 |
| `display_orientation` | Integer 0–3, representing 0°/90°/180°/270° |
| `dst_enabled`, `hour_format_24`, `weather_enabled`, `show_weather`, `show_sunrise_sunset` | Boolean |
| `night_enabled` | Boolean; false by default |
| `night_start_hour`, `night_end_hour` | Integer 0–23 |
| `night_start_minute`, `night_end_minute` | Integer 0–59 |

`POST /api/config` accepts a JSON object of up to 2048 bytes. Omitted fields remain
unchanged. Control characters and embedded NULs in string settings are rejected.
Legacy numeric fields are clamped to their ranges; invalid types, malformed or
non-finite numbers, fractional integers and out-of-range night fields are rejected.
Booleans may also be the strings `true`, `false`, `1`, `0`; numeric JSON 0/1 is not
a boolean. Unknown fields, including the exported version, are ignored.

```sh
curl -H 'Content-Type: application/json' \
  --data '{"brightness":4,"night_enabled":true,"night_start_hour":23,"night_start_minute":0,"night_end_hour":7,"night_end_minute":0}' \
  http://CLOCK_IP/api/config
```

```json
{"status":"ok","restart":false}
```

WiFi credentials, hostname or NTP server changes return `restart:true`, then reboot.
Other changes apply live. Both configuration records are validated before saving;
validation failures return 400, and a failed settings commit returns 500 without
applying runtime changes. The legacy form route can return 413 for allocation overflow.

Two input-only fields control WiFi credentials:

- `password`: a non-empty string up to 63 bytes replaces it; empty/omitted preserves it.
- `clear_password`: `true` explicitly clears it for an open network, including when
  a password is also supplied in the same request.

For a settings-file import, use `--data-binary @clock-config.json` with the same
JSON content type. Exports cannot restore a password or PIN onto another device.

## PIN and maintenance

`POST /maintenance/pin` displays the saved PIN on the OLED and returns:

```json
{"status":"shown","seconds":30}
```

It never returns the PIN itself. Protected requests use
`Authorization: Bearer <six-digit-PIN>`; `xxx-xxx` spelling is also accepted. The
header must accompany each request. Verification does not create a session.
Wrong/missing credentials return 401 without a browser login challenge; unavailable
credentials return 503. Legacy Basic authentication with username `admin` remains
accepted for existing scripts. ArduinoOTA accepts six digits without the dash.

`POST /api/maintenance/verify` returns `{"status":"ok"}` on success.
`POST /api/reboot` returns:

```json
{"status":"ok","message":"Device rebooting..."}
```

`POST /api/eeprom-clear` returns:

```json
{"status":"ok","message":"EEPROM cleared, device will reboot"}
```

That reset removes EEPROM settings/PIN and SDK WiFi credentials. The physical
three-power-cycle recovery has a different scope: it clears WiFi only.

`POST /update` takes a multipart file part named `firmware`, or `filesystem` for an
explicit filesystem image. Missing/zero-byte uploads return 400 before OTA starts.
The request guard checks authorization before delegating to the ESP8266 core.
**Core image-validation errors can return HTTP 200 with `Update error:` in the
body.** Success contains `Update Success!` and restarts the clock. The web UI handles
these cases; do not infer success from HTTP status alone.

PINs are not URL parameters and the browser does not persist them in cookies or
storage. These APIs use local HTTP, not TLS; use a trusted network.
