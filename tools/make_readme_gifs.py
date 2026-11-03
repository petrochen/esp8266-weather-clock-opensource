#!/usr/bin/env python3
"""Compose lightweight README demos from verified native OLED PNGs.

Requires Pillow 12.2.0. Render the production screens with tests/render_display.py
first; this tool only adds presentation outside the 128x64 OLED pixel area.
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SIZE = (768, 464)
OLED = (128, 96, 640, 352)  # exact 4x nearest-neighbour pixels
BG = (243, 245, 247)
INK = (31, 53, 68)
MUTED = (87, 105, 117)
FRAME = (23, 62, 85)
CYAN = (139, 217, 255)
GOLD = (255, 207, 94)

SCREENS = [
    ('combined', 'Time & weather', 'The essentials, together.', 3400),
    ('uv-moderate', 'Daytime UV', "Today's peak, its level, and tomorrow's forecast.", 3800),
    ('weather', 'Weather at a glance', "Pixel-drawn icons and your city's forecast.", 2800),
    ('comfort', 'How it feels outside', 'Temperature, feels-like and outdoor humidity.', 3000),
    ('rain', 'Rain ahead', 'Hourly chances, clearly labeled.', 3000),
    ('daily', 'Today & tomorrow', 'Minimum / maximum temperatures and UV.', 3000),
    ('wind', 'Wind', 'Speed and direction at a glance.', 2400),
    ('sun-times', 'Sunrise & sunset', 'Sun times and the length of the day.', 3200),
    ('external-card', 'Your own readings', 'Optional Home Assistant cards, with automatic expiry.', 3400),
]
WIFI = [
    (name, 'Connecting to Wi-Fi', 'A simple wave animation shows connection activity.', 500)
    for name in ['connecting-start', 'connecting-middle', 'connecting'] * 2
] + [
    ('connected', 'Connected', 'The clock shows its network and local address.', 2000),
    ('combined', 'Ready for the day', 'Time and weather, straight to the display.', 3500),
]


def palette():
    # Lock the three native OLED colors exactly; never dither the pixel art.
    colors = [(0, 0, 0), CYAN, GOLD, BG, INK, MUTED, FRAME, (203, 215, 222)]
    for color in (INK, MUTED, FRAME):
        colors.extend(tuple(round(a + (b - a) * i / 63) for a, b in zip(BG, color))
                      for i in range(64))
    colors += [BG] * (256 - len(colors))
    result = Image.new('P', (1, 1))
    result.putpalette([channel for color in colors for channel in color])
    return result


def compose(native, title, caption, index, count):
    image = Image.new('RGB', SIZE, BG)
    draw = ImageDraw.Draw(image)
    heading, label, small = [ImageFont.load_default(size=size) for size in (25, 22, 15)]
    draw.text((40, 20), 'Weather Clock', fill=INK, font=heading)
    draw.text((40, 51), 'Open firmware. A little OLED. Useful every day.', fill=MUTED, font=small)
    draw.text((728, 28), '1.11 beta / rendered demo', anchor='ra', fill=MUTED, font=small)
    draw.rounded_rectangle((104, 80, 664, 368), radius=18, fill=FRAME)
    draw.rounded_rectangle((120, 88, 648, 360), radius=6, fill='black')
    # The source frame already includes the panel's physical blue/yellow bands.
    image.paste(native.resize((512, 256), Image.Resampling.NEAREST), OLED[:2])
    draw.text((104, 389), title, fill=INK, font=label)
    draw.text((104, 423), caption, fill=MUTED, font=small)
    assert draw.textbbox((104, 423), caption, font=small)[2] < 728, caption
    for i in range(count):
        x = 664 - 12 * (count - 1 - i)
        draw.ellipse((x - 3, 399, x + 3, 405), fill=FRAME if i == index else (203, 215, 222))
    return image


def save_demo(renders, output, name, scenes, metrics, wifi=False):
    frames, durations, originals = [], [], []
    fixed_palette = palette()
    for index, (source, title, caption, duration) in enumerate(scenes):
        filename = source + '-r2'
        checks = metrics[filename]
        assert not any(checks[key] for key in ('wraps', 'clipped_cells', 'clipped_ink')), filename
        native = Image.open(renders / (filename + '.png')).convert('RGB')
        assert native.size == (128, 64), filename
        stage = (0 if index < 6 else index - 5) if wifi else index
        image = compose(native, title, caption, stage, 3 if wifi else len(scenes))
        frames.append(image.quantize(palette=fixed_palette, dither=Image.Dither.NONE))
        durations.append(duration)
        originals.append(native)
    output.mkdir(parents=True, exist_ok=True)
    path = output / (name + '.gif')
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations,
                   loop=0, disposal=2, optimize=True)
    frames[0].convert('RGB').save(output / (name + '-poster.png'), optimize=True)
    # Verify the encoded GIF, not just its inputs: pixels, frame count and pacing.
    with Image.open(path) as gif:
        assert gif.n_frames == len(frames) and gif.info['loop'] == 0
        for index, (native, duration) in enumerate(zip(originals, durations)):
            gif.seek(index)
            assert gif.info['duration'] == duration
            actual = gif.convert('RGB').crop(OLED)
            expected = native.resize((512, 256), Image.Resampling.NEAREST)
            assert actual.tobytes() == expected.tobytes(), (name, index, 'OLED pixels changed')
    assert path.stat().st_size < 300_000, 'Keep README assets lightweight'
    print(f'{path}: {len(frames)} frames, {sum(durations)/1000:g}s, {path.stat().st_size:,} bytes; exact OLED pixels verified')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renders', type=Path, default=ROOT / 'build/display-preview')
    parser.add_argument('--output', type=Path, default=ROOT / 'images')
    args = parser.parse_args()
    metrics = {row['name']: row for row in json.loads((args.renders / 'metrics.json').read_text())}
    save_demo(args.renders, args.output, 'clock-demo', SCREENS, metrics)
    save_demo(args.renders, args.output, 'wifi-demo', WIFI, metrics, wifi=True)


if __name__ == '__main__':
    main()
