#!/usr/bin/env python3
"""Package a verified binary with a version/revision name and SHA-256 checksum."""
import argparse
import hashlib
from pathlib import Path
import re
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--revision', required=True, help='Git commit or local-<source-digest> for unpublished changes')
parser.add_argument('--output', type=Path, default=Path('build/artifacts'))
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9-]{7,64}', args.revision):
    parser.error('revision must be 7-64 letters, digits or hyphens')
source = Path(__file__).resolve().parents[1] / 'firmware/weather_clock/config.h'
version = re.search(r'#define FIRMWARE_VERSION "([\w.-]+)"', source.read_text()).group(1)
if not args.binary.is_file() or not args.binary.stat().st_size:
    parser.error('binary is missing or empty')
revision = args.revision if args.revision.startswith('local-') else args.revision[:12]
name = f'weather_clock-v{version}-{revision}.bin'
args.output.mkdir(parents=True, exist_ok=True)
target = args.output / name
shutil.copyfile(args.binary, target)
digest = hashlib.sha256(target.read_bytes()).hexdigest()
(args.output / 'SHA256SUMS').write_text(f'{digest}  {name}\n')
(args.output / 'BUILD_INFO.txt').write_text(f'Firmware: {version}\nRevision: {args.revision}\nTarget: ESP-01S, 1MB / 64KB filesystem, DIO, 80 MHz\nUse the .bin in the Firmware upload field. No filesystem image is needed.\n')
print(target)
