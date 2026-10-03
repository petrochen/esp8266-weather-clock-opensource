# Code quality and optimization audit

Measured on 2026-10-03 against `42cf8d8` (the prepared beta.2 screen-settings
build), using the same pinned ESP8266 3.1.2 toolchain and ESP-01S target.

## Results

| Measure | Before | After | Change |
| --- | ---: | ---: | ---: |
| Firmware image | 478,672 B | 476,496 B | −2,176 B |
| Static RAM | 38,832 B | 38,464 B | −368 B |
| Room below the 479,232 B OTA budget | 560 B | 2,736 B | +2,176 B |
| Instructions including the 32 KB cache | 62,007 B | 62,007 B | unchanged |
| Embedded web bundle, gzip | 18,329 B | 18,340 B | +11 B |

These are linked-image/static-memory measurements, not runtime heap or speed
benchmarks. The OTA budget and hardware profile were not changed.

## Changes kept

- **One JSON reader type across HTTP inputs.** The display/external-card endpoint
  used an Arduino `String` reader while settings and weather used `const char*`.
  This instantiated a second parser. Reusing the existing reader removed that
  specialization from the final ELF. Input lifetime, size/nesting limits and
  ArduinoJson 7's copied strings remain intact. Each settings/control request
  now retrieves its body once instead of creating successive temporary copies.
- **Weather validation.** Convert a numeric field once, retain type/finite/range
  validation, avoid checking already validated values again, and resolve the
  hourly/daily objects outside their loops. The weather callback's symbol went
  from 2,778 to 2,666 bytes; the numeric helper from 128 to 113 bytes.
- **Sun times.** Reuse the converted sunrise timestamp and format labels only
  when the selected day or event minutes change. This avoids two `snprintf`
  calls on unchanged loop iterations. Timezone changes, replacement forecasts
  and expired events still apply immediately. The symbol shrank from 365 to
  338 bytes; no new persistent cache or timer was added.
- **Flash constants.** The long weather URL format lives in flash via `PSTR`
  and `snprintf_P`. Moving a constant out of RAM does not itself remove its bytes
  from the firmware; the final build measures both resources separately.
- **Maintainability.** ESLint and Ruff now run in CI. Host C++ tests retain strict
  compiler warnings and can all run with ASan/UBSan. EditorConfig and a pinned
  clang-format configuration support consistent edits without a full-tree rewrite.
  Comments explain ownership/storage/timing constraints; the misleading
  “day of year” comment now describes local days since the Unix epoch correctly.
  Browser errors retain their original cause for debugging.

## Why no assembly

The pinned platform already uses `-Os`, function/data sections and linker garbage
collection. The dissolve kernel, a plausible hand-assembly candidate, is only
192 bytes in the linked image. Its graphics transfer still goes through the OLED
driver and I²C; replacing arithmetic does not remove those operations.

No measured bottleneck justifies architecture-specific assembly in this revision.
It would need an ESP8266 benchmark, exact behavior tests and a demonstrated
size/speed gain over compiled C++. Assembly is not inherently smaller, and its
register/side-effect constraints also restrict what the compiler can optimize.
Keep compiler-generated code and portable host coverage for now.

References: the pinned core's `platform.txt`, the linked ELF/map,
[GCC optimization options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html),
[GCC extended assembly](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html) and
the [ESP8266 flash-string guide](https://arduino-esp8266.readthedocs.io/en/3.1.2/PROGMEM.html).

## Experiments and next priorities

1. A 64-byte HTTP reader removed the large response `String`, but instantiated a
   different JSON parser: the experiment reached 479,472 bytes and failed the
   unchanged OTA budget. It was discarded. Revisit bounded streaming only if
   hardware heap measurements justify sharing a reader across all JSON inputs.
   ArduinoJson 7 has no zero-copy mode; changing to a mutable buffer alone would
   not solve this ([upstream explanation](https://arduinojson.org/news/2024/01/03/arduinojson-7/)).
2. Measure minimum free heap, largest block and loop latency during weather refresh,
   WiFi reconnection and OTA on hardware before larger architectural changes.
3. If future features require more flash, first inspect duplicated templates and
   optional dependency features. Treat LTO, a different cache split, replacement
   number parsers or fixed-point arithmetic as separate compatibility experiments.
   Removing float formatting alone cannot remove math routines still used by
   other linked libraries.
4. Continue formatting only the functions being edited. Full-tree formatting is
   intentionally not a CI gate yet; generated assets and pixel rows are excluded.
   Neither source comments nor whitespace are a firmware-size optimization.

## Validation

The pinned firmware build and memory budgets passed. Host regressions cover the
response-size boundary, truncated JSON, invalid/out-of-range weather numbers,
coordinate limits, changed/unchanged/expired sun events and JSON reader
compatibility. The browser suite, release hashing/catalog tests, linters and
embedded-asset reproducibility also passed. See [validation](VALIDATION.md) for
the current sanitizer and hardware status.

At the time of this local audit, the updated CI workflow had not run on GitHub.
No firmware was flashed, and no
claim about reduced on-device loop latency or runtime heap has been measured.
