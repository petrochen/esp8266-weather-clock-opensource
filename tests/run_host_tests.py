#!/usr/bin/env python3
"""Run production C++ logic on the host with mocked clock/network hardware.
Usage: python3 tests/run_host_tests.py --arduinojson /path/to/ArduinoJson/src
No devices are contacted. ArduinoJson must match the firmware build version.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arduinojson', default=os.environ.get('ARDUINOJSON_DIR'))
args = parser.parse_args()
if not args.arduinojson:
    parser.error('set --arduinojson or ARDUINOJSON_DIR to ArduinoJson/src')
root = Path(__file__).resolve().parents[1]
source = root / 'firmware/weather_clock'
headers = Path(args.arduinojson).resolve()
if not (headers / 'ArduinoJson.h').is_file():
    parser.error('ArduinoJson.h not found in supplied directory')
with tempfile.TemporaryDirectory(prefix='weather-clock-host-') as work:
    stage = Path(work)
    for name in ('weather.cpp', 'ntp_client.cpp', 'maintenance.cpp', 'display.cpp', 'update_server.cpp', 'night_mode.cpp'):
        shutil.copy2(source / name, stage / name)
    common = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
              '-I' + str(root / 'tests/host'), '-I' + str(stage),
              '-I' + str(source), '-I' + str(headers)]
    for name, extra in [('settings', [str(source / 'settings.cpp')]), ('network', []), ('maintenance', []), ('display', [str(source / 'settings.cpp')]), ('night', [str(source / 'settings.cpp')]), ('features', [str(source / 'settings.cpp')]), ('update', [])]:
        binary = stage / name
        flags = ['-I' + str(root / 'tests/host/update')] if name == 'update' else []
        subprocess.run(common[:1] + flags + common[1:] + [str(root / f'tests/host/test_{name}.cpp')] + extra + ['-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
