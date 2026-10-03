#!/usr/bin/env python3
"""Stage a published release for browser downloads; never push or flash anything.

BUILD_INFO and SHA256SUMS identify the canonical image (a release can contain
older rebuilt images). A dedicated Git branch supplies CORS-enabled raw files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import urllib.request

REPO = 'petrochen/esp8266-weather-clock-opensource'
TARGET = 'esp01s-1m64-dio-80'
VERSION = re.compile(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-beta\.(0|[1-9]\d*))?')


def version_key(version):
    match = VERSION.fullmatch(version)
    if not match:
        raise ValueError('Only numeric stable and beta.N versions are supported')
    major, minor, patch, beta = match.groups()
    return int(major), int(minor), int(patch), beta is None, int(beta or 0)


def download(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'weather-clock-update-channel'})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Download exceeded its size limit')
    return data


def prepare(release, checksums, image, output, revision=None):
    """Validate metadata/bytes first; only then write the image and catalog."""
    tag = release['tag_name']
    if not tag.startswith('v'):
        raise ValueError('Expected a v-prefixed release tag')
    version = tag[1:]
    version_key(version)
    beta = '-beta.' in version
    if release.get('draft') is not False or not release.get('published_at') or release.get('prerelease') is not beta:
        raise ValueError('Expected a published release with matching stable/beta status')
    name, digest = canonical_image(release, checksums, revision)
    asset = next(asset for asset in release['assets'] if asset['name'] == name)
    if not 8 <= len(image) <= 479232 or len(image) != asset['size']:
        raise ValueError('Firmware size does not match the target/release')
    if image[:1] != b'\xe9' or image[2:4] != b'\x02\x20':
        raise ValueError('Expected an ESP8266 DIO / 1 MB / 40 MHz flash image')
    if hashlib.sha256(image).hexdigest() != digest or asset.get('digest') != 'sha256:' + digest:
        raise ValueError('Firmware must match both SHA256SUMS and the GitHub asset digest')
    output = Path(output)
    catalog_path = output / 'channels.json'
    catalog = json.loads(catalog_path.read_text()) if catalog_path.exists() else {'schema': 1, 'target': TARGET}
    if catalog.get('schema') != 1 or catalog.get('target') != TARGET:
        raise ValueError('Existing update catalog is incompatible')
    channel = 'beta' if beta else 'stable'
    old = catalog.get(channel)
    if old and version_key(old['version']) > version_key(version):
        raise ValueError('Refusing to move the channel to an older version')
    catalog[channel] = {'version': version, 'size': len(image), 'sha256': digest}
    firmware = output / 'firmware'
    firmware.mkdir(parents=True, exist_ok=True)
    (firmware / (digest + '.bin')).write_bytes(image)
    catalog_path.write_text(json.dumps(catalog, indent=2) + '\n')
    return name


def canonical_image(release, checksums, revision=None):
    version = release['tag_name'].removeprefix('v')
    version_key(version)
    pattern = re.compile(r'([a-f0-9]{64})  (weather_clock-v' + re.escape(version) + r'-[a-f0-9]{12}\.bin)')
    entries = [match.groups() for line in checksums.splitlines() if (match := pattern.fullmatch(line))]
    if revision is not None:
        entries = [(digest, name) for digest, name in entries if name.endswith('-' + revision + '.bin')]
    if len(entries) != 1:
        raise ValueError('SHA256SUMS must identify exactly one canonical firmware image')
    digest, name = entries[0]
    if sum(asset['name'] == name for asset in release['assets']) != 1:
        raise ValueError('Canonical firmware asset is missing or ambiguous')
    return name, digest


def build_revision(release, info):
    fields = dict(line.split(': ', 1) for line in info.splitlines() if ': ' in line)
    revision = fields.get('Revision', '')
    if (fields.get('Firmware') != release['tag_name'].removeprefix('v') or
            fields.get('Target') != 'ESP-01S, 1MB / 64KB filesystem, DIO, 80 MHz' or
            not re.fullmatch('[a-f0-9]{40}', revision)):
        raise ValueError('BUILD_INFO must identify the exact firmware revision and supported target')
    return revision[:12]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--output', type=Path, required=True, help='Existing update-channel checkout or a new staging directory')
    args = parser.parse_args()
    if not args.tag.startswith('v'):
        parser.error('tag must start with v')
    version_key(args.tag[1:])
    release = json.loads(download(f'https://api.github.com/repos/{REPO}/releases/tags/{args.tag}', 262144))
    prefix = f'https://github.com/{REPO}/releases/download/{args.tag}/'
    assets = {asset['name']: asset for asset in release['assets']}

    def asset_data(name, limit):
        asset = assets[name]
        if asset['browser_download_url'] != prefix + name:
            raise ValueError('Unexpected release asset URL')
        return download(asset['browser_download_url'], limit)

    checksums = asset_data('SHA256SUMS', 16384).decode('ascii')
    revision = build_revision(release, asset_data('BUILD_INFO.txt', 8192).decode('utf-8'))
    name, _ = canonical_image(release, checksums, revision)
    image = asset_data(name, 479232)
    prepare(release, checksums, image, args.output, revision)
    print(f'Staged {args.tag}: {len(image)} bytes; release digest, checksum and image header verified')


if __name__ == '__main__':
    main()
