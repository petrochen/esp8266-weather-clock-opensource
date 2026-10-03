(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const page = location.pathname === '/config' ? 'settings' : location.pathname === '/update' ? 'update' : 'clock';
  $(page + '-page').hidden = false;
  document.querySelector(`[data-page="${page}"]`).setAttribute('aria-current', 'page');
  document.title = `${page === 'settings' ? 'Settings' : page === 'update' ? 'Update' : 'Time & weather'} · Weather Clock`;
  if (location.pathname === '/debug') $('diagnostics').open = true;
  let state, receivedAt = 0, polling = false, busy = false, savedSettings, settingsDirty = false;
  let maintenanceAction = '';
  const settingsForm = $('settings-form');
  function message(element, text, error = false) {
    element.textContent = text;
    element.classList.toggle('error', error);
  }
  async function request(path, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 12000);
    try {
      const response = await fetch(path, {...options, signal: controller.signal, cache: 'no-store'});
      const text = await response.text();
      let data;
      try { data = JSON.parse(text); } catch { data = null; }
      if (!response.ok) throw new Error(data?.error || text || `Request failed (${response.status}).`);
      return data;
    } catch (error) {
      if (error.name === 'AbortError') throw new Error('The clock took too long to respond. Try again.');
      if (error instanceof TypeError) throw new Error('Cannot reach the clock. Check its power and Wi-Fi connection.');
      throw error;
    } finally { clearTimeout(timeout); }
  }
  const pad = n => String(n).padStart(2, '0');
  function tick() {
    if (document.hidden || page !== 'clock' || !state?.time?.ntp_synced) return;
    const date = new Date((state.time.epoch + state.time.offset + (performance.now() - receivedAt) / 1000) * 1000);
    const hour = date.getUTCHours();
    $('clock-time').textContent = `${pad(state.time.hour_format_24 ? hour : hour % 12 || 12)}:${pad(date.getUTCMinutes())}`;
    $('clock-seconds').textContent = pad(date.getUTCSeconds());
    $('clock-period').textContent = state.time.hour_format_24 ? '24H' : hour < 12 ? 'AM' : 'PM';
    $('clock-date').textContent = date.toLocaleDateString('en-GB', {weekday: 'long', day: 'numeric', month: 'long', timeZone: 'UTC'});
  }
  function condition(code) {
    if (code === 0) return ['Clear sky', '☀'];
    if (code === 1) return ['Mainly clear', '☀'];
    if (code === 2) return ['Partly cloudy', '☁'];
    if (code === 3) return ['Overcast', '☁'];
    if ([45, 48].includes(code)) return ['Fog', '≋'];
    if ([51, 53, 55, 56, 57].includes(code)) return ['Drizzle', '☂'];
    if ([61, 63, 65, 66, 67, 80, 81, 82].includes(code)) return ['Rain', '☂'];
    if ([71, 73, 75, 77, 85, 86].includes(code)) return ['Snow', '❄'];
    if ([95, 96, 97, 99].includes(code)) return ['Thunderstorm', 'ϟ'];
    return ['Weather conditions unavailable', '—'];
  }
  function renderStatus() {
    const weather = state.weather;
    $('device-name').textContent = state.wifi.hostname;
    $('firmware-version').textContent = 'v' + state.system.firmware_version;
    if (page !== 'clock') return;
    $('sync-status').textContent = state.time.ntp_synced ? 'Time synchronized' : 'Waiting for time';
    if (!state.time.ntp_synced) {
      $('clock-time').textContent = '--:--'; $('clock-seconds').textContent = '--';
      $('clock-date').textContent = 'Waiting for synchronization'; $('clock-period').textContent = '';
    }
    $('weather-city').textContent = weather.city || 'Your location';
    const valid = weather.enabled && weather.valid;
    const [description, symbol] = condition(weather.code);
    $('weather-temp').textContent = valid ? Number(weather.temperature).toFixed(1) : '—';
    $('weather-description').textContent = !weather.enabled ? 'Weather is switched off' : valid ? description : 'Waiting for weather';
    $('weather-symbol').textContent = valid ? symbol : '—';
    $('weather-freshness').textContent = valid ? `${weather.stale ? 'Saved reading · ' : ''}Updated ${Math.floor(weather.age_seconds / 60)} min ago` : '';
    $('weather-wind').textContent = valid ? `${Math.round(weather.windspeed)} km/h` : '—';
    $('sunrise').textContent = weather.sunrise; $('sunset').textContent = weather.sunset;
    $('display-note').textContent = state.display.night_active ? 'Night mode: display off. Showing a PIN wakes it temporarily.' : 'Display active';
    $('device-ip').textContent = state.wifi.ip;
    $('device-rssi').textContent = state.wifi.rssi + ' dBm';
    $('device-uptime').textContent = `${Math.floor(state.system.uptime / 3600)} h ${Math.floor(state.system.uptime / 60) % 60} min`;
    $('device-memory').textContent = (state.system.free_heap / 1024).toFixed(1) + ' KB';
    tick();
  }
  async function refresh() {
    if (polling || busy || document.hidden) return;
    polling = true; $('refresh').disabled = true;
    try {
      state = await request('/api/status'); receivedAt = performance.now(); renderStatus();
      message($('connection'), `Connected · ${state.wifi.ip}`);
    } catch (error) { message($('connection'), error.message, true); }
    finally { polling = false; $('refresh').disabled = false; }
  }
  $('refresh').addEventListener('click', refresh);
  if (page === 'clock') {
    setInterval(tick, 1000);
    setInterval(refresh, 60000);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) { tick(); if (performance.now() - receivedAt >= 60000) refresh(); }
    });
  }
  refresh();

  const formControl = name => settingsForm.elements.namedItem(name);
  function settingsChanges() {
    const values = {};
    if (!savedSettings) return values;
    for (const [key, old] of Object.entries(savedSettings)) {
      const input = formControl(key);
      if (!input) continue;
      const value = input.type === 'checkbox' ? input.checked : typeof old === 'number' ? Number(input.value) : typeof old === 'boolean' ? input.value === 'true' : input.value;
      if (value !== old) values[key] = value;
    }
    for (const edge of ['start', 'end']) {
      const [hour, minute] = formControl('night_' + edge).value.split(':').map(Number);
      for (const [part, value] of [['hour', hour], ['minute', minute]]) {
        const key = 'night_' + edge + '_' + part;
        if (value !== savedSettings[key]) values[key] = value;
      }
    }
    if (formControl('password').value) values.password = formControl('password').value;
    if (formControl('clear_password').checked) values.clear_password = true;
    return values;
  }
  function settingsState(showMessage = false) {
    settingsDirty = Object.keys(settingsChanges()).length > 0;
    $('save-settings').disabled = $('discard-settings').disabled = !settingsDirty || busy;
    document.querySelector('.save-bar').classList.toggle('dirty', settingsDirty);
    if (showMessage) message($('settings-result'), settingsDirty ? 'Unsaved changes.' : 'No unsaved changes.');
  }
  function fillSettings(config) {
    savedSettings = config;
    for (const [key, value] of Object.entries(config)) {
      const input = formControl(key);
      if (!input) continue;
      if (input.type === 'checkbox') input.checked = !!value;
      else input.value = String(value);
    }
    for (const edge of ['start', 'end']) formControl('night_' + edge).value = `${pad(config['night_' + edge + '_hour'])}:${pad(config['night_' + edge + '_minute'])}`;
    formControl('password').value = ''; formControl('clear_password').checked = false;
    $('brightness-value').textContent = config.brightness + ' / 7';
    $('settings-fields').disabled = busy;
    settingsState(true);
  }
  if (page === 'settings') {
    for (let offset = -43200; offset <= 50400; offset += 900) {
      const minutes = Math.abs(offset / 60);
      $('timezone').add(new Option(`UTC${offset < 0 ? '−' : '+'}${pad(Math.floor(minutes / 60))}:${pad(minutes % 60)}`, String(offset)));
    }
    request('/api/config').then(config => {
      // Retain offsets imported through the API that are not quarter hours.
      if (![...$('timezone').options].some(option => Number(option.value) === config.timezone_offset)) $('timezone').add(new Option(`UTC offset ${config.timezone_offset} seconds`, config.timezone_offset));
      fillSettings(config);
    }).catch(error => message($('settings-result'), error.message + ' Reload this page to retry.', true));
  }
  formControl('brightness').addEventListener('input', event => { $('brightness-value').textContent = event.target.value + ' / 7'; });
  settingsForm.addEventListener('input', () => settingsState(true));
  settingsForm.addEventListener('change', () => settingsState(true));
  $('discard-settings').addEventListener('click', () => { if (savedSettings && !busy) fillSettings(savedSettings); });
  settingsForm.addEventListener('submit', async event => {
    event.preventDefault();
    if (!savedSettings || busy || !settingsForm.reportValidity()) return;
    const values = settingsChanges();
    if (!Object.keys(values).length) return;
    busy = true; $('settings-fields').disabled = true; settingsState();
    message($('settings-result'), 'Saving…');
    try {
      const result = await request('/api/config', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(values)});
      delete values.password; delete values.clear_password;
      fillSettings({...savedSettings, ...values});
      message($('settings-result'), result.restart ? 'Saved. Clock restarting; reconnect using its new network details.' : 'Saved.');
    } catch (error) { message($('settings-result'), error.message + (values.password ? ' Re-enter the Wi-Fi password before retrying.' : ''), true); }
    finally { busy = false; formControl('password').value = ''; $('settings-fields').disabled = false; settingsState(); }
  });

  document.querySelectorAll('[data-show-pin]').forEach(button => button.addEventListener('click', async () => {
    button.disabled = true;
    const result = button.parentElement.querySelector('.pin-result');
    try {
      await request('/maintenance/pin', {method: 'POST'});
      message(result, 'Read the PIN on your clock. It is visible for 30 seconds.');
    } catch (error) { message(result, error.message, true); }
    finally { button.disabled = false; }
  }));
  const pinHeader = input => ({Authorization: 'Bearer ' + input.value.replace('-', '')});
  const validPIN = input => /^[0-9]{3}-?[0-9]{3}$/.test(input.value);
  function validateFile() {
    const file = $('firmware-file').files[0];
    const error = !file ? 'Choose a non-empty file to continue.' : !file.size ? 'This file is empty. Choose another file.' : !/\.bin(\.gz)?$/i.test(file.name) ? 'Choose a .bin or .bin.gz file.' : '';
    $('firmware-file').setCustomValidity(error);
    $('upload-button').disabled = !!error || busy;
    message($('file-info'), file ? `${file.name} · ${(file.size / 1024).toFixed(1)} KB` : 'No file selected.');
    message($('upload-result'), error || 'Ready when you have entered your PIN.', !!file && !!error);
    return !error;
  }
  $('firmware-file').addEventListener('change', validateFile);
  function upload(file, headers) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update'); xhr.timeout = 180000;
      xhr.setRequestHeader('Authorization', headers.Authorization);
      xhr.upload.onprogress = event => {
        if (event.lengthComputable) {
          const percent = Math.floor(event.loaded / event.total * 100);
          $('upload-progress').value = percent;
          message($('upload-result'), percent < 100 ? `Uploading… ${percent}%` : 'Upload sent. Waiting for the clock to verify it…');
        }
      };
      xhr.onerror = xhr.ontimeout = () => reject(new Error('Connection lost. The update result is unknown. Check the clock before retrying.'));
      xhr.onabort = () => reject(new Error('Upload cancelled. Check the clock before retrying.'));
      xhr.onload = () => {
        if (xhr.status === 200 && xhr.responseText.includes('Update Success!')) resolve();
        else {
          // Never render response HTML. The core reports validation errors with HTTP 200.
          const error = xhr.responseText.startsWith('Update error:') ? xhr.responseText : xhr.status === 401 ? 'Incorrect PIN. Show the PIN and try again.' : `The clock rejected this upload (${xhr.status}).`;
          reject(new Error(error));
        }
      };
      const form = new FormData(); form.append($('image-type').value, file);
      xhr.send(form);
    });
  }
  $('update-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || !validateFile() || !validPIN($('update-pin')) || !$('update-form').reportValidity()) return;
    const headers = pinHeader($('update-pin'));
    const file = $('firmware-file').files[0];
    busy = true; $('upload-button').disabled = true; $('firmware-file').disabled = true;
    $('image-type').disabled = true; $('update-pin').disabled = true;
    $('update-form').querySelector('[data-show-pin]').disabled = true;
    let success = false;
    try {
      message($('upload-result'), 'Checking PIN…');
      await request('/api/maintenance/verify', {method: 'POST', headers});
      $('upload-progress').hidden = false; $('upload-progress').value = 0;
      await upload(file, headers); success = true;
      message($('upload-result'), 'Update complete. The clock is restarting. Return to Clock in a few seconds.');
    } catch (error) { message($('upload-result'), error.message, true); }
    finally {
      busy = false; $('update-pin').value = ''; $('update-pin').disabled = false;
      $('firmware-file').disabled = false; $('image-type').disabled = false;
      $('update-form').querySelector('[data-show-pin]').disabled = false;
      $('upload-button').disabled = success; // Prevent accidental duplicate submission.
    }
  });
  addEventListener('beforeunload', event => { if (busy || settingsDirty) { event.preventDefault(); event.returnValue = ''; } });

  document.querySelectorAll('[data-maintenance]').forEach(button => button.addEventListener('click', () => {
    maintenanceAction = button.dataset.maintenance;
    const reset = maintenanceAction === 'eeprom-clear';
    $('maintenance-title').textContent = reset ? 'Reset all settings?' : 'Restart clock?';
    $('maintenance-description').textContent = reset ? 'This removes Wi-Fi, display and time settings, and creates a new PIN. You will need to set up the clock again.' : 'The clock will restart and keep all saved settings.';
    $('confirm-maintenance').textContent = reset ? 'Confirm reset' : 'Confirm restart';
    $('maintenance-pin').value = ''; message($('dialog-result'), '');
    $('pin-dialog').showModal();
  }));
  $('cancel-maintenance').addEventListener('click', () => { $('maintenance-pin').value = ''; $('pin-dialog').close(); });
  $('pin-dialog').addEventListener('close', () => { $('maintenance-pin').value = ''; });
  $('maintenance-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || !validPIN($('maintenance-pin'))) return;
    busy = true; $('confirm-maintenance').disabled = true;
    try {
      await request('/api/' + maintenanceAction, {method: 'POST', headers: pinHeader($('maintenance-pin'))});
      $('pin-dialog').close();
      message($('maintenance-result'), maintenanceAction === 'eeprom-clear' ? 'Settings reset. Connect to TJ56654-Setup to set up the clock again.' : 'Restarting. Your settings have been kept.');
    } catch (error) { message($('dialog-result'), error.message, true); }
    finally { busy = false; $('confirm-maintenance').disabled = false; $('maintenance-pin').value = ''; }
  });
  for (const action of ['display', 'ntp']) $('test-' + action).addEventListener('click', async () => {
    try {
      await request('/test-' + action);
      message($('diagnostic-result'), action === 'display' ? '8888 is shown on the clock for 3 seconds.' : 'Time synchronization requested.');
    } catch (error) { message($('diagnostic-result'), error.message, true); }
  });
  $('diagnostics').addEventListener('toggle', async () => {
    if (!$('diagnostics').open) return;
    try {
      const debug = await request('/api/debug');
      $('ntp-details').textContent = `${debug.ntp_successes} successful / ${debug.ntp_attempts} attempts`;
      $('network-details').textContent = `${debug.gateway} / ${debug.dns}`;
      $('last-error').textContent = debug.last_error || 'None';
    } catch (error) { message($('diagnostic-result'), error.message, true); }
  });
  $('scan-i2c').addEventListener('click', async () => {
    try {
      const result = await request('/api/i2c-scan');
      message($('diagnostic-result'), `I²C devices: ${result.i2c_scan.devices.map(device => device.address).join(', ') || 'none found'}`);
    } catch (error) { message($('diagnostic-result'), error.message, true); }
  });
  addEventListener('pagehide', () => { $('update-pin').value = ''; $('maintenance-pin').value = ''; });
})();
