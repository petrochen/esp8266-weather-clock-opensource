# Using Your Weather Clock

Once the clock is on WiFi, open its IP address or `http://tj56654-clock.local/`
where mDNS is available. This guide describes the 1.10.0 interface. For the first
flash or recovery, see [Installation](INSTALLATION.md).

## Clock

The home page shows the clock's local time, weather, wind and sunrise/sunset.
Seconds tick in the browser; fresh data comes from the clock once a minute while
the page is visible. Closing the page does not stop the clock.

- **Waiting for time** means NTP has not synchronized yet. The display uses
  `--:--` instead of showing a misleading time.
- **Saved reading** means a previous weather reading is still available but a
  refresh failed or became overdue. Its age is shown; `*` marks stale OLED weather.
- Address, signal, uptime and free heap are always visible below the clock.
  Expand **Device details & diagnostics** for NTP and network details.
  **Test display** shows `8888` for three seconds, **Sync time** requests NTP,
  and **Scan I²C** lists detected display/bus addresses.

The old `/debug` address opens the Clock page with Device details expanded.

On the physical OLED, weather uses native **16×16** pixel icons: sun, sun with
cloud (partly cloudy), outline cloud (overcast), fog bands, cloud with rain strokes,
snowflake, cloud with lightning, or `?` for an unknown code. The hollow cloud and
separate precipitation marks avoid a solid bright patch beside the temperature.

The refreshed v1.10.0 adapts text to the actual display orientation. Clock and
status text are centered; 12-hour time includes AM/PM. Long city labels use the
small font and wrap inside the footer, with no scrolling or extra startup delay.
The PIN screen temporarily stays in landscape so its six digits remain large.
Wi-Fi waves and a `Connecting` label show connection activity; sun/horizon icons
identify sunrise and sunset. See the [complete render gallery](OLED_PREVIEWS.md).
These changes are in the refreshed v1.10.0 build, replacing the first build from
commit `5ae2387ac626` under the same release number.

## Settings

Open **Settings** (`/config`). Ordinary settings do not require a PIN. Changes
apply only when you press **Save settings**.

On desktop, four compact groups show all settings together, including NTP options.
The bottom bar indicates unsaved changes; **Save settings** sends only changed
fields, while **Discard** restores the last saved values without contacting the
clock. Both buttons are inactive until something changes. On narrow screens,
the groups stack and the action bar stays within reach while scrolling.

| Section | Controls |
| --- | --- |
| Display | Brightness 0–7, orientation, time per screen, weather/sun screens, night schedule |
| Time & NTP | Base UTC offset, 12/24-hour format, European daylight saving, NTP server and interval |
| Weather | Fetch enable, city label, latitude/longitude and refresh interval |
| Connection | WiFi SSID, hostname, new WiFi password or explicit open-network option |

Display and weather changes apply without restarting. Changing the WiFi network
or password, device name, or NTP server restarts the clock. After changing network
details you may need its new address to reconnect.

**City label is limited to 31 UTF-8 bytes**, including spaces. That is at most
31 ASCII characters or 15 Russian letters (a mixed name can differ). For example,
`Санкт-Петербург` uses 29 bytes. A 55-character city name is rejected, rather than
silently shortened. Use a short display label; coordinates determine the weather.
The byte counter in Settings catches this before saving. The OLED supports ASCII,
Russian letters including Ё/ё, and common Latin-1 accents; some uncommon Latin
accents fall back to their base letter and unsupported scripts/emoji show `?`.
Original UTF-8 text stays intact in settings and in the browser.

The UTC offset is the **base/winter offset**. European automatic DST adds one hour
between the last Sundays in March and October. It is not a general timezone
database; for other regional rules, disable automatic DST and manage the offset.

Brightness **0 is dim, not off**. Screen intervals are 1–60 seconds; zero is not
a supported way to disable rotation. To show time only, uncheck **Weather**
and **Sunrise / sunset** in the **Screens** row. Disabling **Fetch weather**
separately stops requests.

### Night schedule

Enable **Night mode → Turn display off**, then set the **Off / on** times.
It is disabled by default, including when upgrading from an older firmware.

- The schedule uses synchronized local time and supports crossing midnight.
- The start minute is included; the end minute wakes the screen.
- Equal start/end times leave the screen on.
- Before the first NTP synchronization, the screen stays on.
- Network and time processing continue while the OLED is off.
- Showing a PIN wakes it for 30 seconds; Test display wakes it for three seconds.

### WiFi passwords and backup

The saved WiFi password is never returned to the page. Leave the field blank to
keep it; use **Open network → Clear saved password** only when you intend to clear it.

**Export JSON** downloads `clock-config.json`, including the night schedule.
It omits the WiFi password and maintenance PIN, so it is not a complete credential
backup for another device. JSON import is available through the
[configuration API](API.md#configuration); there is no file-import button in the UI.

## Update

The normal update is one `.bin` file. The web interface is bundled into the firmware,
so there is no separate web/filesystem upload to perform.

1. Open **Update** (`/update`) and choose the firmware file for this clock.
2. Press **Show PIN on clock**. Read the six digits on the physical OLED.
3. Enter the PIN, with or without the grouping dash, and press **Upload & restart**.
4. Keep power connected and wait for the result. A successful update restarts the
   clock; return to the Clock page after it reconnects and check the version in
   the header beside the hostname.

There is no username field. Missing/empty files cannot start an update; the browser
checks the PIN before sending the firmware. The advanced **Filesystem** option is
for a deliberately supplied filesystem image, not the normal release binary.

The current installed firmware handles the first upload. When upgrading an older
version, use that version's existing login or code; the PIN-only interface appears
after the update. See the [upgrade notes](VALIDATION.md#upgrade-from-1910).

### About the PIN

The clock generates a random six-digit PIN and keeps it across restarts and OTA
updates. It is not derived from a MAC address. The button displays `xxx-xxx` for
30 seconds; HTTP responses and logs do not reveal it. Startup never pauses to show
the PIN. ArduinoOTA uses the six digits without the dash.

The PIN authorizes firmware updates, restart and full settings reset. Ordinary
settings remain open on the local network. These are local HTTP/OTA interfaces;
keep them on a trusted network.

## Restart and reset

In **Settings → Restart & reset**, choose the action, show/read the PIN, and confirm.

| Action | Result |
| --- | --- |
| Restart clock | Reboot; settings and PIN are kept |
| Reset all settings | Clear EEPROM and SDK WiFi credentials; next boot uses defaults and generates a new PIN |
| Three quick power cycles | Recover WiFi access; other settings and the PIN are kept |

The physical recovery screen says `WiFi reset`; the first v1.10.0 build and older builds
say `FACTORY RESET!`. That path resets WiFi only.
For timing and setup instructions, see [Recovery and reset](INSTALLATION.md#recovery-and-reset).
