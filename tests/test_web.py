#!/usr/bin/env python3
"""Browser regressions against the actual embedded UI, with a simulated clock API.
No device is contacted. Requires playwright==1.58.0 and its Chromium browser.
"""
import argparse
from copy import deepcopy
import json
import gzip
import hashlib
from pathlib import Path
import re
import struct
from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--screenshots', type=Path)
args = parser.parse_args()
header = (root / 'firmware/weather_clock/web_assets.h').read_text()
html = gzip.decompress(bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', header))).decode()
version = re.search(r'FIRMWARE_VERSION "([^"]+)"', (root / 'firmware/weather_clock/config.h').read_text()).group(1)
status = dict(wifi=dict(hostname='living-room-clock', ip='192.168.2.149', rssi=-58),
              time=dict(epoch=1791031680, offset=3600, ntp_synced=True, hour_format_24=True),
              system=dict(firmware_version=version, uptime=92400, free_heap=28320),
              display=dict(night_active=False),
              weather=dict(enabled=True, valid=True, stale=False, temperature=22.4, code=1, windspeed=12.5, age_seconds=180, city='Portimão', sunrise='07:31', sunset='19:14'))
config = dict(firmware_version=version, ssid='Home', hostname='living-room-clock', timezone_offset=0, dst_enabled=True,
              brightness=4, ntp_server='pool.ntp.org', ntp_interval=3600, hour_format_24=True,
              latitude=37.19, longitude=-8.54, city_name='Portimão', weather_enabled=True, weather_interval=1800,
              display_rotation_sec=5, display_orientation=2, show_weather=True, show_sunrise_sunset=True,
              night_enabled=False, night_start_hour=23, night_start_minute=0, night_end_hour=7, night_end_minute=0)
config.update(clock_weather=False, dissolve=True, temperature_unit=0, wind_unit=0, night_action=0, night_brightness=0,
              external_enabled=False, sun_countdown=False, show_comfort=False, show_rain=False, show_daily=False, show_wind=False, show_uv=False)
config.update({'screen_' + key + '_sec': 0 for key in ['clock', 'weather', 'sun', 'comfort', 'rain', 'daily', 'wind', 'uv']})
status['display'].update(screen=0, paused=False, available=[True, True, True, False, False, False, False, False, False])
status['units'] = dict(temperature=0, wind=0)
status['weather'].update(comfort_valid=True, feels_like=21.2, humidity=68, wind_direction=270, is_day=True, source_epoch=1791031500)
status['forecast'] = dict(hours=[dict(epoch=1791036000, temperature=22, rain=60)], days=[dict(epoch=1790985600, low=16, high=23, uv=4.2, valid=True), dict(epoch=1791072000, low=15, high=22, uv=5.8, valid=True)])
calls = []
updates = []
pins = []
api_status = deepcopy(status)
upload_error = False
config_error = False
config_ignore = False
config_read_error = False
config_restart = False
config_round_coordinates = False
firmware = b'\xe9\x02\x02\x20' + (bytes(range(256)) * 1870)[:478380]
firmware_digest = hashlib.sha256(firmware).hexdigest()
catalog = dict(schema=1, target='esp01s-1m64-dio-80',
               stable=dict(version='1.11.0', size=len(firmware), sha256=firmware_digest),
               beta=dict(version='1.12.0-beta.2', size=len(firmware), sha256=firmware_digest))
github_error = 0
download_bytes = firmware
github_requests = []

def route_request(route):
    request = route.request
    path = request.url.removeprefix('http://clock.test')
    calls.append((request.method, path))
    headers = request.headers
    if request.url.startswith('https://raw.githubusercontent.com/'):
        assert 'authorization' not in headers and 'cookie' not in headers
        github_requests.append(request.url)
        if github_error:
            route.fulfill(status=github_error, body='Unavailable')
        elif request.url.endswith('/channels.json'):
            route.fulfill(json=catalog)
        else:
            assert request.url.endswith('/firmware/' + firmware_digest + '.bin')
            route.fulfill(content_type='application/octet-stream', body=download_bytes)
    elif request.url.startswith('https://geocoding-api.open-meteo.com/'):
        route.fulfill(json={'results': [dict(name='Portimão', latitude=37.14, longitude=-8.53, country='Portugal')]})
    elif path in ('/', '/config', '/debug', '/update') and request.method == 'GET':
        route.fulfill(status=200, content_type='text/html', body=html)
    elif path == '/api/status':
        route.fulfill(json=api_status)
    elif path == '/api/debug':
        route.fulfill(json=dict(ntp_attempts=4, ntp_successes=4, gateway='192.168.2.1', dns='192.168.2.1', last_error=''))
    elif path == '/api/i2c-scan':
        route.fulfill(json={'i2c_scan': {'devices': [{'address': '0x3C'}]}})
    elif path == '/api/config':
        if request.method == 'POST':
            updates.append(request.post_data_json)
            if config_error:
                route.fulfill(status=503, json={'error': 'Settings could not be saved. Try again.'})
            else:
                if not config_ignore:
                    config.update({k: v for k, v in request.post_data_json.items() if k not in ('password', 'clear_password')})
                    if config_round_coordinates:
                        for key in ('latitude', 'longitude'):
                            config[key] = struct.unpack('f', struct.pack('f', config[key]))[0]
                route.fulfill(json={'status': 'ok', 'restart': config_restart})
        elif config_read_error:
            route.fulfill(status=503, json={'error':'Readback unavailable'})
        else:
            route.fulfill(json=config)
    elif path == '/api/display':
        values = request.post_data_json
        if values['action'] == 'hold': api_status['display']['paused'] = True
        elif values['action'] == 'resume': api_status['display']['paused'] = False
        elif values['action'] == 'show': api_status['display'].update(screen=values['screen'], paused=True)
        elif values['action'] == 'next': api_status['display']['screen'] = (api_status['display']['screen'] + 1) % 3
        route.fulfill(json={'status': 'ok'})
    elif path == '/maintenance/pin':
        route.fulfill(json={'status': 'shown', 'seconds': 30})
    elif path in ('/api/maintenance/verify', '/api/reboot', '/api/eeprom-clear', '/update'):
        pins.append(headers.get('authorization'))
        if headers.get('authorization') != 'Bearer 123456':
            route.fulfill(status=401, json={'error': 'Incorrect PIN. Read the six digits on your clock.'})
        elif path == '/update':
            assert b'name="firmware"' in request.post_data_buffer
            if b'filename="weather_clock-' in request.post_data_buffer:
                assert firmware in request.post_data_buffer
            route.fulfill(body='Update error: ERROR[10]: Invalid image' if upload_error else 'Update Success! Rebooting...')
        else:
            route.fulfill(json={'status': 'ok'})
    elif path.startswith('/test-'):
        route.fulfill(body='Requested')
    else:
        raise AssertionError('Unexpected request: ' + path)

with sync_playwright() as p:
    browser = p.chromium.launch()
    context = browser.new_context(viewport={'width': 1280, 'height': 800}, device_scale_factor=1)
    context.route('**/*', route_request)
    context.add_init_script('''
      window.testTimers = {};
      const nativeInterval = window.setInterval;
      window.setInterval = (callback, delay) => { window.testTimers[delay] = callback; return nativeInterval(callback, delay); };
      window.testHidden = false;
      Object.defineProperty(document, 'hidden', {get: () => window.testHidden});
    ''')
    page = context.new_page()
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    def screenshot(name):
        if args.screenshots:
            args.screenshots.mkdir(parents=True, exist_ok=True)
            page.screenshot(path=str(args.screenshots / (name + '.png')), full_page=True)
    def no_overflow():
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    def open_page(path):
        page.goto('http://clock.test' + path)
        page.locator('#connection').filter(has_text='Connected').wait_for()
    open_page('/')
    assert page.locator('#weather-temp').inner_text() == '22.4'
    assert page.locator('#weather-city').inner_text() == 'Portimão'
    no_overflow(); screenshot('clock-desktop')
    page.locator('#diagnostics summary').click()
    page.locator('#ntp-details').filter(has_text='4 successful').wait_for()
    page.locator('#scan-i2c').click()
    page.locator('#diagnostic-result').filter(has_text='0x3C').wait_for()
    page.locator('#diagnostics summary').click()
    # Hidden tabs never poll; local seconds do not request anything.
    before = len(calls)
    page.evaluate('window.testTimers[1000]()')
    page.evaluate('window.testHidden=true; window.testTimers[60000]()')
    assert len(calls) == before
    page.evaluate('window.testHidden=false; window.testTimers[60000]()')
    page.wait_for_timeout(100)
    assert len(calls) == before + 1
    # Failed/stale data is explicit. API strings cannot create HTML.
    api_status['weather'].update(city='<img src=x onerror=alert(1)>', stale=True, code=3)
    api_status['time']['ntp_synced'] = False
    page.locator('#refresh').click()
    page.wait_for_function("document.getElementById('clock-time').textContent==='--:--'")
    assert page.locator('#weather-city img').count() == 0
    assert 'Saved reading' in page.locator('#weather-freshness').inner_text()
    assert page.locator('#weather-description').inner_text() == 'Overcast'
    api_status = deepcopy(status)

    open_page('/config')
    page.locator('#settings-fields:not([disabled])').wait_for()
    assert page.locator('#save-settings').is_disabled()
    assert page.locator('#discard-settings').is_disabled()
    # Basic controls and Save fit on a laptop; advanced controls are disclosed on demand.
    for width, height in [(1280, 800), (1366, 768)]:
        page.set_viewport_size({'width': width, 'height': height})
        assert page.evaluate('''() => [...document.querySelectorAll('#settings-fields > .settings-section input, #settings-fields > .settings-section select, #save-settings')].filter(el => !el.closest('.city-search')).every(el => {
          const rect = el.getBoundingClientRect();
          return rect.width > 0 && rect.height > 0 && rect.top >= 0 && rect.bottom <= innerHeight;
        })''')
        no_overflow()
    page.set_viewport_size({'width': 1280, 'height': 800})
    screenshot('settings-desktop')
    if args.screenshots:
        metrics = page.evaluate('''() => {
          const controls = [...document.querySelectorAll('#settings-fields input, #settings-fields select')];
          return {viewport: [innerWidth, innerHeight], documentHeight: document.documentElement.scrollHeight,
            formHeight: document.querySelector('#settings-form').getBoundingClientRect().height,
            formWidth: document.querySelector('#settings-form').getBoundingClientRect().width,
            controls: controls.length, visibleControls: controls.filter(el => el.getBoundingClientRect().bottom <= innerHeight).length,
            saveBottom: document.querySelector('#save-settings').getBoundingClientRect().bottom};
        }''')
        (args.screenshots / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    page.locator('#settings-form').evaluate('form => form.requestSubmit()')
    assert not updates  # unchanged settings do not write EEPROM
    assert page.locator('[name=password]').input_value() == ''
    assert page.locator('[name=night_start]').input_value() == '23:00'
    # Dirty state follows edits and reversions; Discard restores all controls locally.
    page.get_by_label('City label', exact=True).fill('Draft city')
    page.locator('#settings-result').filter(has_text='Unsaved changes').wait_for()
    assert page.locator('#save-settings').is_enabled()
    dialogs = []
    def keep_edits(dialog):
        dialogs.append(dialog.type)
        dialog.dismiss()
    page.once('dialog', keep_edits)
    page.locator('nav [data-page=clock]').click()
    assert dialogs == ['beforeunload'] and page.url.endswith('/config')
    assert page.get_by_label('City label', exact=True).input_value() == 'Draft city'
    page.get_by_label('City label', exact=True).fill('Portimão')
    assert page.locator('#save-settings').is_disabled()
    page.locator('[name=night_enabled]').check()
    page.locator('[name=night_start]').fill('21:15')
    page.locator('[name=password]').fill('local-test-fixture')
    page.locator('[name=clear_password]').check()
    page.get_by_label('Brightness', exact=True).focus()
    page.keyboard.press('ArrowRight')
    assert page.locator('#brightness-value').inner_text() == '5 / 7'
    page.keyboard.press('Tab')
    assert page.get_by_label('Orientation', exact=True).evaluate('el => el === document.activeElement')
    page.locator('#discard-settings').click()
    assert not page.locator('[name=night_enabled]').is_checked()
    assert page.locator('[name=night_start]').input_value() == '23:00'
    assert page.locator('[name=password]').input_value() == ''
    assert not page.locator('[name=clear_password]').is_checked()
    assert page.locator('#brightness-value').inner_text() == '4 / 7'
    assert page.locator('#save-settings').is_disabled() and not updates
    # Native validation still blocks an incomplete required setting.
    city = page.get_by_label('City label', exact=True)
    city.fill('АБВГДЕЖЗИЙКЛМНОП')  # 16 characters, 32 UTF-8 bytes
    assert page.locator('#city-bytes').inner_text() == '32'
    page.locator('#save-settings').click()
    assert not updates and page.locator('#city:invalid').count() == 1
    city.fill('Санкт-Петербург')
    assert page.locator('#city-bytes').inner_text() == '29'
    assert page.locator('#city:invalid').count() == 0
    city.fill('W' * 31)
    assert page.locator('#city-bytes').inner_text() == '31'
    assert page.locator('#city:invalid').count() == 0
    page.locator('#discard-settings').click()
    assert page.locator('#city:invalid').count() == 0
    page.get_by_label('NTP server', exact=True).fill('')
    page.locator('#save-settings').click()
    assert not updates and page.locator('#ntp-server:invalid').count() == 1
    page.locator('#discard-settings').click()
    page.locator('[name=night_enabled]').check()
    page.locator('[name=night_start]').fill('22:45')
    page.locator('[name=city_name]').fill('Portimão & coast')
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Saved.').wait_for()
    assert updates[-1]['night_enabled'] and updates[-1]['night_start_hour'] == 22 and updates[-1]['night_start_minute'] == 45
    assert updates[-1]['city_name'] == 'Portimão & coast'
    assert not any(key in updates[-1] for key in ('password', 'ssid', 'ntp_server', 'clear_password', 'night_end_hour', 'night_end_minute'))
    assert page.locator('#save-settings').is_disabled()
    # Save failure keeps edits for retry; discarding returns to the last successful save.
    config_error = True
    page.get_by_label('City label', exact=True).fill('Failed draft')
    page.locator('#save-settings').click()
    page.locator('#settings-result.error').filter(has_text='could not be saved').wait_for()
    assert page.locator('#save-settings').is_enabled()
    assert page.get_by_label('City label', exact=True).input_value() == 'Failed draft'
    page.locator('#discard-settings').click()
    assert page.get_by_label('City label', exact=True).input_value() == 'Portimão & coast'
    config_error = False
    # Restart/reset confirm using the same PIN-only dialog; closing clears it.
    page.locator('.maintenance summary').click()
    page.locator('[data-maintenance=reboot]').click()
    page.locator('#maintenance-pin').fill('123-456')
    page.locator('#confirm-maintenance').click()
    page.locator('#maintenance-result').filter(has_text='Restarting').wait_for()
    assert page.locator('#maintenance-pin').input_value() == ''
    assert pins[-1] == 'Bearer 123456'
    page.locator('[data-maintenance=eeprom-clear]').click()
    assert 'removes Wi-Fi' in page.locator('#maintenance-description').inner_text()
    page.locator('#maintenance-pin').fill('123456')
    page.locator('#cancel-maintenance').click()
    assert page.locator('#maintenance-pin').input_value() == ''
    assert ('POST', '/api/eeprom-clear') not in calls

    open_page('/update')
    page.locator('#release-result').filter(has_text='1.12.0-beta.2 available').wait_for()
    assert not page.evaluate('isSecureContext')
    assert page.locator('#release-channel').input_value() == 'beta'
    assert page.locator('#firmware-file').is_hidden()
    screenshot('update-github-desktop')
    for width in [320, 390]:
        page.set_viewport_size({'width':width, 'height':844})
        no_overflow()
        assert page.locator('#upload-button').is_visible()
    screenshot('update-github-mobile')
    page.set_viewport_size({'width':1280, 'height':800})
    page.locator('#release-channel').select_option('stable')
    page.locator('#release-result').filter(has_text='v1.11.0 available').wait_for()
    assert 'beta' not in page.locator('#release-notes').get_attribute('href')
    before = len(github_requests)
    page.locator('#update-pin').fill('999999');page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='Incorrect PIN').wait_for()
    assert len(github_requests) == before # no image download until PIN validation
    assert ('POST', '/update') not in calls
    download_bytes = firmware[:-1] + b'X'
    page.locator('#update-pin').fill('123456');page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='checksum mismatch').wait_for()
    assert ('POST', '/update') not in calls and page.locator('#update-pin').input_value() == ''
    download_bytes = firmware + b'X'
    page.locator('#update-pin').fill('123456');page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='oversized file').wait_for()
    assert ('POST', '/update') not in calls
    download_bytes = firmware
    # A leftover manual filesystem choice must never affect an online install.
    page.locator('#image-type').evaluate("el => el.value='filesystem'")
    page.locator('#update-pin').fill('123-456');page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='Update complete').wait_for()
    assert page.locator('#upload-button').is_disabled() and page.locator('#update-pin').input_value() == ''
    assert calls.count(('POST', '/update')) == 1
    calls.remove(('POST', '/update')) # subsequent manual-upload guards retain their own assertions
    open_page('/update')
    page.locator('#release-result').filter(has_text='available').wait_for()
    catalog['beta']['version'] = version
    catalog['stable']['version'] = '1.10.0'
    page.locator('#check-release').click()
    page.locator('#release-result').filter(has_text='No newer release').wait_for()
    assert page.locator('#upload-button').is_disabled()
    catalog['target'] = 'esp32'
    page.locator('#check-release').click()
    page.locator('#release-result').filter(has_text='incompatible').wait_for()
    assert page.locator('#upload-button').is_disabled()
    catalog['target'] = 'esp01s-1m64-dio-80'
    github_error = 404
    page.locator('#check-release').click()
    page.locator('#release-result').filter(has_text='unavailable (404)').wait_for()
    assert page.locator('#upload-button').is_disabled()
    github_error = 0
    page.locator('#update-source').select_option('file')
    assert page.locator('#upload-button').is_disabled()
    page.locator('#firmware-file').set_input_files({'name': 'empty.bin', 'mimeType': 'application/octet-stream', 'buffer': b''})
    assert page.locator('#upload-button').is_disabled()
    assert 'empty' in page.locator('#upload-result').inner_text()
    page.locator('#firmware-file').set_input_files({'name': 'clock.bin', 'mimeType': 'application/octet-stream', 'buffer': b'firmware fixture'})
    page.locator('#update-form [data-show-pin]').click()
    page.locator('#update-form .pin-result').filter(has_text='30 seconds').wait_for()
    page.locator('#update-pin').fill('999999')
    page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='Incorrect PIN').wait_for()
    assert ('POST', '/update') not in calls # validate PIN before sending firmware bytes
    assert page.locator('#update-pin').input_value() == ''
    screenshot('update-desktop')
    upload_error = True
    page.locator('#update-pin').fill('123456')
    page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='Invalid image').wait_for()
    assert page.locator('#upload-result').get_attribute('class') == 'error'
    upload_error = False
    page.locator('#update-pin').fill('123-456')
    page.locator('#upload-button').click()
    page.locator('#upload-result').filter(has_text='Update complete').wait_for()
    assert page.locator('#update-pin').input_value() == '' and page.locator('#upload-button').is_disabled()
    assert all(token.startswith('Bearer ') for token in pins)
    assert page.evaluate('localStorage.length + sessionStorage.length') == 0
    assert not any('123456' in url or '123-456' in url for _, url in calls)
    assert context.cookies() == []
    # New controls use explicit API actions and keep the weather's canonical units.
    api_status.update(deepcopy(status))
    open_page('/')
    page.locator('#screen-hold').click()
    page.locator('#screen-hold').filter(has_text='Resume rotation').wait_for()
    page.locator('#active-screen').select_option('1')
    page.locator('#screen-result').filter(has_text='selected and held').wait_for()
    assert api_status['display']['screen'] == 1 and api_status['display']['paused']
    page.locator('#screen-hold').click()
    page.locator('#screen-hold').filter(has_text='Hold').wait_for()
    page.locator('.forecast-panel summary').click()
    assert '60%' in page.locator('#hourly-forecast').inner_text()
    assert '4.2' in page.locator('#daily-forecast').inner_text()
    assert 'today 4 · Moderate · tomorrow 6' in page.locator('#uv-summary').inner_text()
    original_days = deepcopy(api_status['forecast']['days'])
    for value, level in [(0, 'Low'), (2.5, 'Moderate'), (6.2, 'High'), (7.8, 'Very high'), (11, 'Extreme')]:
        api_status['forecast']['days'][0]['uv'] = value
        page.locator('#refresh').click()
        page.locator('#uv-summary').filter(has_text=f'today {int(value + 0.5)} · {level}').wait_for()
    api_status['forecast']['days'][0]['uv'] = -1
    page.locator('#refresh').click();page.locator('#uv-summary').filter(has_text='today —').wait_for()
    assert 'tomorrow 6' in page.locator('#uv-summary').inner_text()
    original_epoch = api_status['time']['epoch']
    api_status['time']['epoch'] += 86400
    page.locator('#refresh').click();page.locator('#uv-summary').filter(has_text='today 6 · High · tomorrow —').wait_for()
    api_status['time']['epoch'] = original_epoch
    api_status['forecast']['days'] = original_days
    api_status['weather']['stale'] = True
    page.locator('#refresh').click();page.locator('#uv-summary').filter(has_text='Saved forecast').wait_for()
    api_status['weather']['stale'] = False

    api_status['units'] = dict(temperature=1, wind=1)
    page.locator('#refresh').click()
    page.locator('#weather-unit').filter(has_text='°F').wait_for()
    assert page.locator('#weather-temp').inner_text() == '72.3'
    assert 'm/s' in page.locator('#weather-wind').inner_text()
    screenshot('forecast-desktop')
    with page.expect_download() as download_info:
        page.locator('#download-diagnostics').evaluate('el => el.click()')
    report = json.loads(Path(download_info.value.path()).read_text())
    assert report['firmware'] == version
    assert not any(key in report for key in ('ssid', 'ip', 'hostname', 'chip_id', 'password', 'pin', 'city', 'latitude'))
    open_page('/config')
    page.locator('#settings-fields:not([disabled])').wait_for()
    before_updates = len(updates)
    page.locator('#wifi-password').fill('pending-secret')
    page.locator('#screen-preset').select_option('weather')
    assert page.locator('[name=show_uv]').is_checked()
    assert page.locator('[name=clock_weather]').is_checked()
    assert page.locator('[name=show_rain]').is_checked()
    assert page.locator('#wifi-password').input_value() == 'pending-secret'
    page.locator('#discard-settings').click()
    assert not page.locator('[name=show_rain]').is_checked()
    page.locator('#import-file').set_input_files({'name': 'settings.json', 'mimeType': 'application/json',
        'buffer': json.dumps(dict(clock_weather=True, show_rain=True, screen_clock_sec=30, password='ignore-this-secret')).encode()})
    page.locator('#import-dialog[open]').wait_for()
    assert 'ignore-this-secret' not in page.locator('#import-preview').inner_text()
    assert len(updates) == before_updates
    page.locator('#apply-import').click()
    assert page.locator('[name=clock_weather]').is_checked()
    assert page.locator('#wifi-password').input_value() == '' and len(updates) == before_updates
    page.locator('.extra-settings summary').click()
    assert page.locator('[name=screen_clock_sec]').input_value() == '30'
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Saved.').wait_for()
    assert updates[-1] == dict(clock_weather=True, show_rain=True, screen_clock_sec=30)
    screenshot('advanced-settings-desktop')
    page.locator('.city-search summary').click()
    page.locator('#city-query').fill('Portimao')
    page.locator('#city-search').click()
    page.locator('#city-results button').click()
    assert page.locator('#latitude').input_value() == '37.14'
    assert page.locator('#city').input_value() == 'Portimão'
    assert len(updates) == before_updates + 1
    page.locator('#discard-settings').click()
    api_status.update(deepcopy(status))
    # A successful POST alone must not turn unsaved screen choices into "Saved".
    config['show_wind'] = False
    open_page('/config')
    page.locator('#settings-fields:not([disabled])').wait_for()
    page.locator('#screen-preset').select_option('glance')
    page.locator('#settings-fields details').evaluate_all('items => items.forEach(el => el.open = true)')
    page.locator('[name=show_wind]').check()
    assert page.locator('#screen-preset').input_value() == 'custom'
    config_ignore = True
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Some settings were not saved').wait_for()
    assert page.locator('[name=show_wind]').is_checked() and page.locator('#save-settings').is_enabled()
    assert not config['show_wind']
    config_ignore = False; config_read_error = True
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Could not verify saved settings').wait_for()
    assert page.locator('[name=show_wind]').is_checked() and page.locator('#save-settings').is_enabled()
    config_read_error = False
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Saved.').wait_for()
    assert config['show_wind'] and page.locator('#save-settings').is_disabled()
    # Coordinates round to float32 on the ESP; this must not report a failed save.
    config_round_coordinates = True
    page.locator('#latitude').fill('89.1234567')
    page.locator('#longitude').fill('-179.1234567')
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Saved.').wait_for()
    assert config['longitude'] != -179.1234567 and page.locator('#save-settings').is_disabled()
    config_round_coordinates = False
    # Network changes reboot immediately: a readback would fail after a good save.
    config_restart = True; config_read_error = True
    page.locator('[name=hostname]').fill('bedroom-clock')
    before_reads = calls.count(('GET', '/api/config'))
    page.locator('#save-settings').click()
    page.locator('#settings-result').filter(has_text='Clock restarting').wait_for()
    assert calls.count(('GET', '/api/config')) == before_reads
    config_restart = False; config_read_error = False
    # The main page respects the availability returned after settings are saved.
    api_status['display']['available'][6] = True
    open_page('/')
    assert not page.locator('#active-screen option[value="6"]').is_disabled()
    page.locator('#active-screen').select_option('6')
    page.locator('#screen-result').filter(has_text='selected and held').wait_for()
    assert api_status['display']['screen'] == 6
    api_status.update(deepcopy(status))
    # No overflow on narrow phones, with the same real HTML/CSS/JS.
    for width in (320, 390, 640, 768, 900):
        # 640×400 also exercises the reflow viewport of a 1280×800 screen at 200% zoom.
        page.set_viewport_size({'width': width, 'height': 400 if width == 640 else 844})
        for path, name in [('/', 'clock'), ('/config', 'settings'), ('/update', 'update')]:
            open_page(path)
            no_overflow()
            if width == 390: screenshot(name + '-mobile')
            if path == '/config' and width in (320, 640):
                page.locator('#settings-fields:not([disabled])').wait_for()
                page.locator('#settings-fields details').evaluate_all('items => items.forEach(el => el.open = true)')
                page.locator('#screen-preset').focus()
                reached = set()
                for _ in range(80):
                    if not page.evaluate("document.querySelector('#settings-fields').contains(document.activeElement)"):
                        break
                    if page.evaluate('document.activeElement.matches("input, select")'):
                        reached.add(page.evaluate('document.activeElement.id || document.activeElement.name'))
                    # Focused fields must scroll above the sticky Save bar, not underneath it.
                    focus = page.evaluate('''() => {
                      const rect = document.activeElement.getBoundingClientRect();
                      const bar = document.querySelector('.save-bar').getBoundingClientRect();
                      return {name: document.activeElement.name, top: rect.top, bottom: rect.bottom, limit: Math.min(innerHeight, bar.top)};
                    }''')
                    assert focus['top'] >= 0 and focus['bottom'] <= focus['limit'], (width, focus)
                    page.keyboard.press('Tab')
                assert len(reached) == page.locator('#settings-fields input, #settings-fields select').count(), reached
    assert not errors, errors
    browser.close()
print('PASS: minified embedded UI, basic settings fit laptop, advanced controls accessible, dirty/discard/retry/import/presets, forecast/units/display control, hidden-tab polling, PIN/update guards, no credential storage or overflow')
