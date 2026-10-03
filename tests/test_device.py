#!/usr/bin/env python3
"""Hardware-in-the-loop checks. Read-only unless --fuzz is explicitly provided.

Usage: python3 tests/test_device.py <device-ip> [--fuzz]
Fuzzing changes non-network settings temporarily and restores an actual backup.
Never runs firmware updates, resets, or changes WiFi credentials.
"""
import argparse
import gzip
import json
import time
import urllib.error
import urllib.parse
import urllib.request


def request(base, path, data=None, form=False):
    if data is None:
        req = urllib.request.Request(base + path)
    else:
        body = urllib.parse.urlencode(data).encode() if form else (data if isinstance(data, bytes) else json.dumps(data).encode())
        req = urllib.request.Request(base + path, data=body, method='POST')
        req.add_header('Content-Type', 'application/x-www-form-urlencoded' if form else 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            body = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                body = gzip.decompress(body)
            return response.status, body.decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def get_json(base, path):
    status, body = request(base, path)
    assert status == 200, f'{path}: HTTP {status}'
    return json.loads(body)


def snapshot(base):
    data = get_json(base, '/api/config')
    assert isinstance(data, dict) and 'brightness' in data, 'Config backup unavailable'
    assert 'password' not in data, 'Config exposes WiFi password'
    return {key: value for key, value in data.items() if key not in ('firmware_version', 'magic')}


def readonly_checks(base):
    status = get_json(base, '/api/status')
    clock = get_json(base, '/api/time')
    weather = get_json(base, '/api/weather')
    debug = get_json(base, '/api/debug')
    config = snapshot(base)
    assert 'synced' in clock and 'ntp_synced' in status['time']
    if clock['synced']:
        assert clock['epoch'] > 1700000000
        assert len(clock['time']) == 8 and clock['time'][2] == ':'
    assert status['system']['free_heap'] > 8192
    assert status['system']['max_free_block'] > 4096
    assert isinstance(debug['last_error'], str)
    assert isinstance(weather['stale'], bool)
    if weather['valid']:
        assert -100 <= weather['temperature'] <= 70
        assert weather['windspeed'] >= 0
    code, html = request(base, '/config')
    assert code == 200 and ('name="password"' in html or "name='password' value=''" in html), 'Settings UI unavailable'
    assert config['weather_interval'] >= 60
    print('PASS: API schema, time, weather cache, heap, secret-free configuration')
    heaps, blocks = [], []
    for _ in range(30):
        current = get_json(base, '/api/status')['system']
        heaps.append(current['free_heap'])
        blocks.append(current['max_free_block'])
        time.sleep(0.5)
    assert min(heaps) > 8192 and min(blocks) > 4096, 'Low heap or contiguous block'
    # A short smoke test, not evidence of multi-day stability.
    assert heaps[-1] >= heaps[0] - 2048, 'Heap declined during polling'
    print('PASS: 30-request heap smoke test (not a long-duration test)')


def fuzz_checks(base):
    backup = snapshot(base)
    try:
        for field, value, stored in [
            ('ntp_interval', '0', 60), ('weather_interval', '86401', 86400),
            ('brightness', '-1', 0), ('brightness', '255', 7),
            ('latitude', '999', 90), ('longitude', '-999', -180),
            ('timezone_offset', '999999', 50400),
        ]:
            code, _ = request(base, '/config', {field: value}, form=True)
            assert code == 200, f'{field}: HTTP {code}'
            assert snapshot(base)[field] == stored, f'{field}: incorrect stored value'
        for update in [
            {'ssid': 'A' * 100}, {'password': 'B' * 200}, {'hostname': ''},
            {'latitude': 'NaN'}, {'ntp_interval': '60garbage'},
            {'brightness': 3, 'hostname': ''},
        ]:
            before = snapshot(base)
            code, _ = request(base, '/config', update, form=True)
            assert code == 400, f'Invalid form accepted: {list(update)}'
            assert snapshot(base) == before, 'Rejected form changed configuration'
        for body in [b'', b'not JSON', b'{"ssid":', b'null', b'[]',
                     b'{"brightness":null}', b'{"latitude":"Infinity"}',
                     b'{"city_name":"a\\u0000b"}', b'{"brightness":3,"hostname":""}']:
            before = snapshot(base)
            code, _ = request(base, '/api/config', body)
            assert code == 400, 'Invalid JSON settings accepted'
            assert snapshot(base) == before, 'Rejected import changed configuration'
        # Whitespace/escaping and exported fields must round-trip.
        update = {'city_name': 'Quote " and \\', 'display_orientation': 2, 'dst_enabled': False}
        code, _ = request(base, '/api/config', update)
        assert code == 200
        after = snapshot(base)
        assert all(after[key] == value for key, value in update.items())
        print('PASS: form/import boundaries, atomic rejection, whitespace and escaping')
    finally:
        # Never replace unknown credentials. Omitted password is preserved.
        restore = dict(backup)
        if not restore.get('ssid'):
            restore.pop('ssid', None)
        code, _ = request(base, '/api/config', restore)
        assert code == 200, 'Config restoration failed'
        assert snapshot(base) == backup, 'Restored configuration differs from backup'
        print('PASS: original configuration restored')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device_ip')
    parser.add_argument('--fuzz', action='store_true', help='Temporarily change non-network settings, then restore')
    args = parser.parse_args()
    base = 'http://' + args.device_ip
    readonly_checks(base)
    if args.fuzz:
        fuzz_checks(base)


if __name__ == '__main__':
    main()
