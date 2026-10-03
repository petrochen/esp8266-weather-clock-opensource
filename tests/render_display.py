#!/usr/bin/env python3
"""Render production OLED screens with the actual Adafruit_GFX rasterizer/font.

No hardware, network, Pillow or browser dependency. Writes PNGs and a gallery;
fails on implicit text wrapping, off-screen glyph cells or clipped drawing.
"""
import argparse
import html
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


def png_from_ppm(path):
    magic, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    assert magic == b'P6' and maximum == b'255'
    width, height = map(int, dimensions.split())
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arduinojson', required=True, type=Path)
    parser.add_argument('--gfx', required=True, type=Path, help='Adafruit_GFX_Library directory (1.12.4)')
    parser.add_argument('--output', type=Path, default=Path('build/display-preview'))
    parser.add_argument('--sanitize', action='store_true', help='Enable address/undefined-behavior checks')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = root / 'firmware/weather_clock'
    gfx = args.gfx.resolve()
    if 'version=1.12.4' not in (gfx / 'library.properties').read_text().splitlines():
        parser.error('OLED previews require the pinned Adafruit GFX Library 1.12.4')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='weather-clock-raster-') as directory:
        stage = Path(directory)
        for name in ('display.cpp', 'maintenance.cpp', 'night_mode.cpp'):
            shutil.copy2(source / name, stage / name)
        flags = [os.environ.get('CXX', 'c++'), '-std=c++17', '-DARDUINO=10819',
                 '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
                 '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for path in (root / 'tests/host/raster', root / 'tests/host', stage, source, args.arduinojson.resolve(), gfx):
            flags.append('-I' + str(path))
        subprocess.run(flags + [str(root / 'tests/host/render_display.cpp'), str(source / 'settings.cpp'),
                               str(gfx / 'Adafruit_GFX.cpp'), '-o', str(stage / 'render')], check=True)
        result = subprocess.run([str(stage / 'render'), str(output)], capture_output=True, text=True)
    if result.stderr:
        print(result.stderr)
    rows = []
    for line in result.stdout.splitlines():
        name, width, height, wraps, cells, ink = line.split('\t')
        rows.append(dict(name=name, width=int(width), height=int(height), wraps=int(wraps),
                         clipped_cells=int(cells), clipped_ink=int(ink)))
        png_from_ppm(output / (name + '.ppm'))
        (output / (name + '.ppm')).unlink()
    (output / 'metrics.json').write_text(json.dumps(rows, indent=2) + '\n')
    cards = '\n'.join(f'<figure data-rotation="{r["name"][-1]}"><figcaption>{html.escape(r["name"][:-3])}</figcaption>'
                      f'<img src="{r["name"]}.png" width="{r["width"] * 2}" height="{r["height"] * 2}" '
                      f'alt="{html.escape(r["name"])}"><small>{r["width"]} × {r["height"]}, 2× pixels</small></figure>' for r in rows)
    (output / 'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>OLED screen gallery</title>
<style>body{font:15px system-ui;margin:24px;background:#f3f5f7;color:#243044}h1{font-size:24px}
main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0;min-width:256px}figcaption{margin-bottom:8px}
img{image-rendering:pixelated;background:black;display:block}small{display:block;margin:6px 0 18px}
select{font:inherit;padding:6px}header{margin-bottom:24px}p{max-width:80ch;line-height:1.5}[hidden]{display:none}</style>
<header><h1>OLED screen gallery</h1><p>Production drawing code and Adafruit_GFX 1.12.4, with synthetic data.
Exact pixels; the physical yellow band rotates with the panel. No optical blur, I²C or hardware timing is simulated.
The PIN screen temporarily uses landscape, as on the device.</p>
<label>Orientation <select id="rotation"><option value="2">180° · default</option><option value="0">0°</option>
<option value="1">90°</option><option value="3">270°</option></select></label></header><main>''' + cards + '''</main>
<script>const select=document.getElementById('rotation');function filter(){document.querySelectorAll('figure').forEach(x=>x.hidden=x.dataset.rotation!==select.value)}select.onchange=filter;filter()</script></html>''')
    failures = [row for row in rows if row['wraps'] or row['clipped_cells'] or row['clipped_ink']]
    print(f'{len(rows)} OLED renders; {len(failures)} layouts with wrapping/clipping. Gallery: {output / "index.html"}')
    for row in failures:
        print(row)
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
