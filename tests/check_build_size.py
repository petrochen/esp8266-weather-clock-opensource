#!/usr/bin/env python3
"""Check budgets for generic ESP8266, 1M64 flash and default 32KB I-cache."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('--size-tool', required=True)
args = parser.parse_args()
binary = args.build / 'weather_clock.ino.bin'
elf = args.build / 'weather_clock.ino.elf'
output = subprocess.check_output([args.size_tool, '-A', str(elf)], text=True)
sections = {}
for line in output.splitlines():
    fields = line.split()
    if len(fields) >= 2 and fields[0].startswith('.') and fields[1].isdigit():
        sections[fields[0]] = int(fields[1])
assert '.text' in sections and '.bss' in sections, 'Unrecognized ELF size output'
instruction = 32768 + sum(size for section, size in sections.items() if section.startswith('.text'))
ram = sum(sections.get(section, 0) for section in ('.data', '.rodata', '.bss'))
size = binary.stat().st_size
# FS starts at 0xEB000. Leave room for two sector-aligned firmware images and
# one additional 4KB sector; this is a build budget, not a live OTA-space probe.
assert size <= 479232, f'Firmware exceeds conservative OTA budget: {size} bytes'
assert instruction * 100 < 65536 * 95, f'Instruction region is >=95%: {instruction} bytes'
assert ram < 45000, f'Static RAM exceeds 45KB budget: {ram} bytes'
print(f'PASS budgets: binary={size}, RAM={ram}, instruction+cache={instruction}/65536')
