#!/usr/bin/env python3
"""Exercise publication validation without GitHub writes or device access."""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('channel', Path(__file__).resolve().parents[1] / 'tools/prepare_update_channel.py')
channel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(channel)


class UpdateChannelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.image = b'\xe9\x02\x02\x20' + bytes(range(256)) * 3
        self.digest = hashlib.sha256(self.image).hexdigest()

    def release(self, version, **changes):
        name = f'weather_clock-v{version}-0123456789ab.bin'
        release = dict(tag_name='v'+version, draft=False, published_at='2026-10-03T00:00:00Z', prerelease='-beta.' in version,
                       assets=[dict(name=name, size=len(self.image), digest='sha256:'+self.digest)])
        release.update(changes)
        return release, f'{self.digest}  {name}\n'

    def test_channels_and_atomic_failure(self):
        for version in ['1.10.0', '1.11.0-beta.2']:
            channel.prepare(*self.release(version), self.image, self.output)
        catalog = json.loads((self.output/'channels.json').read_text())
        self.assertEqual(catalog['stable']['version'], '1.10.0')
        self.assertEqual(catalog['beta']['version'], '1.11.0-beta.2')
        before = (self.output/'channels.json').read_bytes()
        for release, sums in [self.release('1.9.9'), self.release('1.11.0-beta.1'), self.release('1.12.0', draft=True), self.release('1.12.0', prerelease=True), self.release('1.12.0', published_at=None)]:
            with self.assertRaises(ValueError): channel.prepare(release, sums, self.image, self.output)
            self.assertEqual((self.output/'channels.json').read_bytes(), before)

    def test_image_and_asset_validation(self):
        release, sums = self.release('1.11.0-beta.4')
        for image in [b'', self.image[:-1], self.image[:-1]+b'X', b'\xe9\x02\x00\x20'+self.image[4:], self.image*700]:
            with self.assertRaises(ValueError): channel.prepare(release, sums, image, self.output)
            self.assertFalse((self.output/'channels.json').exists())
        for mutate in [lambda r:r['assets'][0].update(digest='sha256:'+'0'*64), lambda r:r.update(assets=[]), lambda r:r['assets'].append(deepcopy(r['assets'][0]))]:
            bad = deepcopy(release); mutate(bad)
            with self.assertRaises(ValueError): channel.prepare(bad, sums, self.image, self.output)
        for bad_sums in ['', sums+sums, sums.replace(self.digest, 'broken')]:
            with self.assertRaises(ValueError): channel.prepare(release, bad_sums, self.image, self.output)

    def test_history_preserves_channels_and_is_idempotent(self):
        for version in ['1.10.0', '1.11.0-beta.3']:
            channel.prepare(*self.release(version), self.image, self.output)
        before = (self.output/'channels.json').read_bytes()
        channel.prepare(*self.release('1.11.0-beta.1'), self.image, self.output, history_only=True)
        self.assertEqual((self.output/'channels.json').read_bytes(), before)
        self.assertLessEqual(len(before), 4096)
        history = json.loads((self.output/'releases.json').read_text())
        self.assertEqual([r['version'] for r in history['releases']], ['1.11.0-beta.3', '1.11.0-beta.1', '1.10.0'])
        self.assertEqual(history['recommended'], {'stable':'1.10.0', 'beta':'1.11.0-beta.3'})
        self.assertFalse(history['releases'][1]['profile']['github'])
        snapshot = {p.name:p.read_bytes() for p in self.output.glob('*.json')}
        channel.prepare(*self.release('1.11.0-beta.1'), self.image, self.output, history_only=True)
        self.assertEqual(snapshot, {p.name:p.read_bytes() for p in self.output.glob('*.json')})
        with self.assertRaises(ValueError):
            channel.prepare(*self.release('1.12.0'), self.image, self.output)
        with self.assertRaises(ValueError):
            channel.prepare(*self.release('1.11.0-beta.2'), self.image[:-1], self.output, history_only=True)
        self.assertEqual(snapshot, {p.name:p.read_bytes() for p in self.output.glob('*.json')})

    def test_immutable_and_withdrawn_release(self):
        channel.prepare(*self.release('1.11.0-beta.3'), self.image, self.output)
        path = self.output/'releases.json'
        history = json.loads(path.read_text())
        history['releases'][0]['status'] = 'withdrawn'
        history['releases'][0]['reason'] = 'Test withdrawal'
        path.write_text(json.dumps(history))
        with self.assertRaises(ValueError):
            channel.prepare(*self.release('1.11.0-beta.3'), self.image, self.output)
        history['releases'][0]['status'] = 'available'
        history['releases'][0]['sha256'] = 'f'*64
        path.write_text(json.dumps(history))
        with self.assertRaises(ValueError):
            channel.prepare(*self.release('1.11.0-beta.3'), self.image, self.output)

    def test_rebuilt_release_uses_build_info(self):
        release, sums = self.release('1.10.0')
        old = deepcopy(release['assets'][0]);old['name'] = old['name'].replace('0123456789ab', 'abcdef012345')
        release['assets'].append(old)
        sums += self.digest + '  ' + old['name'] + '\n'
        with self.assertRaises(ValueError): channel.canonical_image(release, sums)
        info = 'Firmware: 1.10.0\nRevision: 0123456789ab' + '0'*28 + '\nTarget: ESP-01S, 1MB / 64KB filesystem, DIO, 80 MHz\n'
        revision = channel.build_revision(release, info)
        name = channel.prepare(release, sums, self.image, self.output, revision)
        self.assertIn('0123456789ab', name)
        for bad in [info.replace('80 MHz', '160 MHz'), info.replace('1.10.0', '1.9.0'), info.replace('Revision:', 'Unknown:')]:
            with self.assertRaises(ValueError): channel.build_revision(release, bad)


if __name__ == '__main__':
    unittest.main()
