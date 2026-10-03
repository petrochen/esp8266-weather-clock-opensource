# Contributing to ESP8266 Weather Clock

Thank you for your interest in contributing! This project welcomes improvements, bug fixes, and new features.

## How to Contribute

### Reporting Bugs

If you find a bug, please open an issue with:

- Clear description of the problem
- Steps to reproduce
- Expected vs actual behavior
- Relevant serial output (remove credentials and private network details)
- Firmware version

### Suggesting Features

Feature requests are welcome! Please include:

- Use case description
- Why this would be useful
- Any implementation ideas

### Pull Requests

1. **Fork the repository**
2. **Create a feature branch**: `git checkout -b feature/my-new-feature`
3. **Test your changes**:
   - Compile successfully
   - Test on real hardware if possible
   - Check memory usage (IRAM must stay < 95%)
4. **Follow the code style**:
   - Use `ICACHE_FLASH_ATTR` for non-critical functions
   - Avoid String concatenation in loops
   - Document state machines with comments
5. **Commit with clear messages**: Explain what and why, not how
6. **Submit PR** with description of changes

### Code Guidelines

**Memory Safety:**

- Check IRAM usage after adding code
- Use fixed-size buffers instead of dynamic allocation where possible
- Prefer `snprintf` over String concatenation

**Async Architecture:**

- Keep loop() non-blocking (no delay() calls)
- Use state machines for multi-step operations
- Add exponential backoff to network operations

**Web interface:**

- Edit `web/index.html`, `web/app.css` and `web/app.js`; keep assets self-contained.
- Run `python3 tools/embed_web.py` after changes and include the generated header.
- Run `python3 tests/test_web.py` with Playwright 1.58.0 and Chromium installed.
- Check firmware size/static RAM and inspect desktop/mobile screenshots; keep
  polling limited to visible pages and never persist a maintenance PIN.

**Testing:**

- Test on ESP-01S hardware (1MB flash, 80KB RAM)
- Verify OTA updates work
- Check 24h stability

## Development Setup

### Requirements

- Arduino IDE 1.8.x or 2.x
- ESP8266 core **3.1.2**, Arduino CLI **1.4.1** for the reproducible build
- Pinned libraries listed in [docs/VALIDATION.md](docs/VALIDATION.md#reproducible-build)

### Building

```bash
# Arduino IDE: Sketch → Verify/Compile
# Or use arduino-cli:
python3 tools/embed_web.py --check
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=1M64,FlashMode=dio,xtal=80 \
  --warnings all --build-path build firmware/weather_clock
```

### Flashing

```bash
# OTA upload (preferred, when device is on the network)
curl -u admin -F "firmware=@build/weather_clock.ino.bin" http://192.168.x.x/update

# Initial flash via FTDI (3.3V! ESP-01S in socket — no soldering)
# 1. Pull ESP-01S from socket on TJ-56-654 PCB
# 2. Connect: FTDI 3V3→3V3, GND→GND, TX↔RX crossed, GND→GPIO0
# 3. Power on with GPIO0 grounded → bootloader mode
esptool.py --port /dev/cu.usbserial-0001 --baud 115200 write_flash \
  --flash_size 1MB --flash_mode dio 0x0 firmware.bin
```

### Running the test suite

Run the [host and browser regressions](docs/VALIDATION.md#reproducible-build), then
check firmware size. Hardware tests are a separate step:

```bash
python3 tests/test_device.py <device-ip>
```

The default suite is read-only. Add `--fuzz` to temporarily exercise non-network
settings and restore the actual configuration afterward. Host regression tests
and the pinned build command are documented in [docs/VALIDATION.md](docs/VALIDATION.md).

### Recovery (bricked device)

Three quick power cycles recover WiFi access, retaining other settings and the PIN.
The HTTP reset clears all settings; a full flash erase also removes them. See the
[recovery guide](docs/INSTALLATION.md#recovery-and-reset) before choosing a method.

## Preparing a Release

1. Reconcile the version in `firmware/weather_clock/config.h`, changelog and release
   notes under `docs/releases/`. Group unpublished development work under the new
   public version; do not describe intermediate dev builds as published releases.
2. Regenerate web assets only if `web/` changed, then run the documented build and
   checks. Record hardware results separately from host/browser results.
3. Package the verified `.bin` with `tools/package_firmware.py`, an accurate revision
   identifier, `SHA256SUMS` and build information. For uncommitted sources use a
   `local-<source-digest>` identifier; do not label them as the base Git commit.
4. Review upgrade instructions and remaining hardware checks before publishing.
   GitHub Actions attaches successful build artifacts; it does not publish releases.

A prepared release is not a published release. The firmware and source revision
must match the release assets when a tag is eventually created.

## Project Structure

```
esp8266-weather-clock-opensource/
├── firmware/               # Main firmware source (weather_clock/)
├── web/                    # HTML/CSS/JS source, embedded in firmware
├── tools/                  # Web embedding and firmware packaging
├── tests/                  # Host, browser and optional device checks
├── docs/                   # Guides, API, validation and release notes
├── images/                 # Photos and screenshots
├── README.md               # Main documentation
└── LICENSE                 # MIT License
```

## Communication

- **Issues**: Bug reports and feature requests
- **Discussions**: General questions and ideas
- **Pull Requests**: Code contributions

## Code of Conduct

Be respectful, constructive, and helpful. We're all here to learn and build cool stuff.

## Questions?

Open an issue or discussion - happy to help!
