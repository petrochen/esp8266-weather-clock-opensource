# Adapted fork improvements in 1.10.0

Thanks to Stibax for the following work in
[Arduino.esp8266-weather-clock-opensource](https://github.com/Stibax/Arduino.esp8266-weather-clock-opensource).
These are selective adaptations to the current asynchronous network and PIN code.

| Improvement | Source | Adaptation |
| --- | --- | --- |
| CI binary artifacts | [workflow at 9073401](https://github.com/Stibax/Arduino.esp8266-weather-clock-opensource/blob/907340106585e0d6591dd50792f09f4fb0a9a37d/.github/workflows/build-firmware.yml) | Keep pinned dependencies, tests and budgets; package version + commit + SHA-256 only after success. No automatic release publication. |
| Night schedule | [9160f3d](https://github.com/Stibax/Arduino.esp8266-weather-clock-opensource/commit/9160f3d72d3da67cc18c3f65e0cbc946606d8441) | Independent versioned EEPROM record, disabled on upgrade, synchronized local time, midnight boundary tests, temporary PIN/test wake-up. |
| Startup orientation | [9073401](https://github.com/Stibax/Arduino.esp8266-weather-clock-opensource/commit/907340106585e0d6591dd50792f09f4fb0a9a37d) | Load and validate orientation and brightness before drawing the first frame. No startup pause. |
| 16×16 weather bitmaps | [75d794b](https://github.com/Stibax/Arduino.esp8266-weather-clock-opensource/commit/75d794be573dba439536850cf551d1e25cf10fb3) | Adapt the PROGMEM bitmap integration and preserve room for the stale marker and negative temperatures. The current pixels are redrawn locally for small-display legibility, with a separate partly-cloudy glyph. |

The initial six bitmap designs came from the MIT-licensed source above; the project
retains its [MIT license and copyright notice](../LICENSE). All eight current glyphs
are hand-drawn local replacements: a rayed sun, outline clouds, a sun/cloud combination, separate
rain strokes, a snowflake, fog bands, a lightning bolt and an unknown-weather mark.
They use 256 bytes in flash, with no scaling or extra framebuffer. Code mapping is
also maintained locally. No fork's generated binaries, old
dependency versions or mandatory login for ordinary settings are imported.
