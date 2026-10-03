# Using Your Weather Clock

## Trying the 1.11 beta

The source tree now targets **1.11.0-beta.2**; the sections below also describe the
compatible stable controls. Start with Settings → **Combined clock + UV** → Save for a
large clock with a weather footer, alternating with the UV daytime peak. **Clock only** hides weather pages;
**Detailed weather** enables the extra pages. Presets stage changes before Save
and keep unrelated unsaved settings, but replace individual screen choices and
durations. Choose a preset first, then adjust individual screens and save.
The combined preset was called **Time & weather** in beta.1.
Discard restores the saved configuration.

Expand **More display options** for per-page durations (0 inherits the common
interval), feels-like/humidity, rain probability, daily min/max/UV, wind, units,
sun countdown, dissolve and external cards. Each forecast value comes from the
weather provider; humidity is outdoors, not a measurement inside your room.
Night mode can now dim to a separate brightness instead of turning the OLED off.
All of this is optional: an upgrade preserves the previous display choices.

On the Clock page, choose an available screen to hold it, or use Next / Hold /
Resume. A held external card expires normally. Night schedules and temporary PIN
screens override these controls. Hold is not saved across restart.
Unavailable options are disabled in Settings or waiting for usable data. Enable
the required pages in Settings and save; external cards also need a data source.

**Import settings** reads a local JSON export (at most 8 KB), previews known fields
and stages them in the form. Click Save to apply. Password/PIN fields from the file
are ignored; a password you have already typed into the form is preserved.
**Find city** searches Open-Meteo from your browser and stages coordinates. Pick
the timezone explicitly. A place name longer than the existing 31-byte UTF-8
storage limit needs a shorter display label; it is never silently saved truncated.

Expand weather details for six hourly slots and two daily forecasts. The rain OLED
page shows the next three available slots; it does not predict minute-by-minute
rain. “Fetched” age and source reading age are separate. Missing fields use a
placeholder; stale readings remain marked. Download diagnostics from Device
details to share a report without network identifiers, location or credentials.
For room readings, see [Home Assistant](HOME_ASSISTANT.md).


Once the clock is on WiFi, open its IP address or `http://tj56654-clock.local/`
where mDNS is available. The base controls are shared with 1.10.0. For the first
flash or recovery, see [Installation](INSTALLATION.md).

## UV daytime peak

Enable **UV daytime peak** in More display options, or select the Combined clock + UV
preset and Save. The screen shows today's predicted daily maximum, its level and
tomorrow's maximum. It has its own duration setting and direct screen selection.
The web Clock page shows the same summary without expanding forecast details.

Levels use the rounded whole index: 0–2 Low, 3–5 Moderate, 6–7 High, 8–10 Very high,
11+ Extreme. The API and forecast table retain the provider's decimal value.
This is a daytime peak forecast, even when viewed at night; it is not current UV.
Missing/expired days are not replaced by tomorrow, and stale data is marked.
The dedicated page requires a synchronized clock and today's available UV value.
New screen settings default off on upgrade; selecting the preset enables them.

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
Starting with beta.2, non-restarting saves read the settings back from the clock
before showing **Saved**. A mismatch or failed read keeps your edits in the form
with an error so you can retry. Network changes that restart the clock use its
save acknowledgement without waiting for a readback.

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

1. Open **Update** (`/update`). From 1.11.0-beta.2, **GitHub release** checks for a
   newer **Stable** or **Stable + Beta** version. The initial channel follows your
   installed version; changing it does not install anything. Read the release
   notes, or select **Local file** to use a downloaded image.
2. Press **Show PIN on clock**. Read the six digits on the physical OLED.
3. Enter the PIN, with or without the grouping dash, and press **Install & restart**
   (or **Upload & restart** for a local file).
4. Keep power connected and wait for the result. A successful update restarts the
   clock; return to the Clock page after it reconnects and check the version in
   the header beside the hostname.

There is no username field. Missing/empty files cannot start an update; the browser
checks the PIN before downloading/sending the firmware. GitHub downloads are checked
for the target, size and SHA-256 before any firmware bytes reach the clock. Keep the
browser page open until completion. Checks occur on opening Update, changing the
channel or pressing **Check GitHub**; there are no unattended background installs.
An older/equal version is never offered automatically. Downgrades and reinstalling
the same version require a deliberate local-file upload.

The advanced **Filesystem** option is
for a deliberately supplied filesystem image, not the normal release binary.
The device's PIN is sent only to the clock and is not stored in the browser.
GitHub being unavailable does not prevent local-file updates. The online catalog
must first be published by the repository maintainer; see [release updates](UPDATES.md).

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
