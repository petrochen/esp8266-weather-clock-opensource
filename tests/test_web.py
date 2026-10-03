#!/usr/bin/env python3
"""Browser regressions against the actual embedded UI, with a simulated clock API.
No device is contacted. Requires playwright==1.58.0 and its Chromium browser.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--screenshots', type=Path)
args = parser.parse_args()
html = (root / 'web/index.html').read_text().replace('/* INLINE_CSS */', (root / 'web/app.css').read_text()).replace('/* INLINE_JS */', (root / 'web/app.js').read_text())
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
calls = []
updates = []
pins = []
api_status = deepcopy(status)
upload_error = False
config_error = False

def route_request(route):
    request = route.request
    path = request.url.removeprefix('http://clock.test')
    calls.append((request.method, path))
    headers = request.headers
    if path in ('/', '/config', '/debug', '/update') and request.method == 'GET':
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
                config.update({k: v for k, v in request.post_data_json.items() if k not in ('password', 'clear_password')})
                route.fulfill(json={'status': 'ok', 'restart': False})
        else:
            route.fulfill(json=config)
    elif path == '/maintenance/pin':
        route.fulfill(json={'status': 'shown', 'seconds': 30})
    elif path in ('/api/maintenance/verify', '/api/reboot', '/api/eeprom-clear', '/update'):
        pins.append(headers.get('authorization'))
        if headers.get('authorization') != 'Bearer 123456':
            route.fulfill(status=401, json={'error': 'Incorrect PIN. Read the six digits on your clock.'})
        elif path == '/update':
            assert b'name="firmware"' in request.post_data_buffer
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
    # Every setting and the Save action fits a common laptop viewport without scrolling.
    for width, height in [(1280, 800), (1366, 768)]:
        page.set_viewport_size({'width': width, 'height': height})
        assert page.evaluate('''() => [...document.querySelectorAll('#settings-fields input, #settings-fields select, #save-settings')].every(el => {
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
                page.locator('#brightness').focus()
                reached = set()
                for _ in range(80):
                    if not page.evaluate("document.querySelector('#settings-fields').contains(document.activeElement)"):
                        break
                    reached.add(page.evaluate('document.activeElement.name'))
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
print('PASS: compact desktop/mobile UI, all settings visible at 1280×800 and 1366×768, keyboard controls, dirty/discard/retry, settings round-trip, hidden-tab polling, stale/unsynced/escaped data, PIN-only maintenance, empty upload, preflight auth, core upload errors, no credential storage, no overflow')
