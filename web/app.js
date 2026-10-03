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
  let selectedRelease = null, releaseChecked = false, checkingRelease = false;
  const settingsForm = $('settings-form');
  const screenNames = ['Time', 'Weather', 'Sunrise / sunset', 'Outdoor comfort', 'Hourly rain', 'Daily forecast', 'Wind', 'External card', 'UV daytime peak'];
  const durationKeys = ['clock', 'weather', 'sun', 'comfort', 'rain', 'daily', 'wind', 'uv'];
  durationKeys.forEach((key, index) => {
    const row = document.createElement('div'); row.className = 'form-row';
    const label = document.createElement('label'); label.htmlFor = 'duration-' + key; label.textContent = key === 'uv' ? 'UV daytime peak' : screenNames[index];
    const input = document.createElement('input'); input.id = label.htmlFor; input.name = 'screen_' + key + '_sec';
    input.type = 'number'; input.min = 0; input.max = 120; input.value = 0; input.required = true;
    row.append(label, input); $('screen-durations').append(row);
  });
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
      if (error.name === 'AbortError') throw new Error('The clock took too long to respond. Try again.', {cause: error});
      if (error instanceof TypeError) throw new Error('Cannot reach the clock. Check its power and Wi-Fi connection.', {cause: error});
      throw error;
    } finally { clearTimeout(timeout); }
  }
  const pad = n => String(n).padStart(2, '0');
  const degrees = value => state?.units?.temperature ? value * 1.8 + 32 : value;
  const degreeUnit = () => state?.units?.temperature ? '°F' : '°C';
  function forecastDate(epoch) {
    let offset = state.time.timezone_offset ?? state.time.offset;
    if (state.time.dst_enabled) {
      const year = new Date(epoch * 1000).getUTCFullYear();
      const boundary = month => Date.UTC(year, month, 31 - new Date(Date.UTC(year, month, 31)).getUTCDay(), 1) / 1000;
      if (epoch >= boundary(2) && epoch < boundary(9)) offset += 3600;
    }
    return new Date((epoch + offset) * 1000);
  }
  function renderUV() {
    const target = $('uv-summary'), weather = state.weather;
    const dayNumber = epoch => Math.floor(forecastDate(epoch).getTime() / 86400000);
    const today = dayNumber(state.time.epoch);
    const uv = ahead => (state.forecast?.days || []).find(day => day.epoch && dayNumber(day.epoch) === today + ahead)?.uv;
    const format = value => Number.isFinite(value) && value >= 0 ? String(Math.round(value)) : '—';
    const value = uv(0), rounded = Math.round(value);
    const level = rounded < 3 ? 'Low' : rounded < 6 ? 'Moderate' : rounded < 8 ? 'High' : rounded < 11 ? 'Very high' : 'Extreme';
    target.textContent = weather.enabled && weather.valid && state.time.ntp_synced
      ? `Daytime UV peak · today ${format(value)}${format(value) === '—' ? '' : ' · ' + level} · tomorrow ${format(uv(1))}${weather.stale ? ' · Saved forecast' : ''}`
      : 'Daytime UV peak · waiting for forecast and synchronized time';
  }
  function renderForecast() {
    const weather = state.weather;
    renderUV();
    const details = [];
    if (weather.valid && weather.comfort_valid) details.push(`Feels like ${degrees(weather.feels_like).toFixed(1)}${degreeUnit()}`);
    if (weather.valid && weather.humidity >= 0) details.push(`Outdoor humidity ${weather.humidity}%`);
    $('weather-extra').textContent = details.join(' · ') || 'Waiting for outdoor details.';
    function row(target, values) {
      const tr = document.createElement('tr');
      values.forEach(value => { const td = document.createElement('td'); td.textContent = value; tr.append(td); });
      target.append(tr);
    }
    $('hourly-forecast').replaceChildren(); $('daily-forecast').replaceChildren();
    for (const hour of state.forecast?.hours || []) {
      if (state.time.ntp_synced && hour.epoch <= state.time.epoch) continue;
      row($('hourly-forecast'), [forecastDate(hour.epoch).toLocaleTimeString('en-GB', {hour: '2-digit', minute: '2-digit', timeZone: 'UTC'}), `${degrees(hour.temperature).toFixed(0)}${degreeUnit()}`, hour.rain >= 0 ? hour.rain + '%' : '—']);
    }
    for (const day of state.forecast?.days || []) if (day.valid) {
      if (state.time.ntp_synced && forecastDate(day.epoch).toISOString().slice(0, 10) < forecastDate(state.time.epoch).toISOString().slice(0, 10)) continue;
      row($('daily-forecast'), [forecastDate(day.epoch).toLocaleDateString('en-GB', {weekday: 'short', day: 'numeric', month: 'short', timeZone: 'UTC'}), `${degrees(day.low).toFixed(0)} / ${degrees(day.high).toFixed(0)}${degreeUnit()}`, day.uv >= 0 ? day.uv.toFixed(1) : '—']);
    }
    for (const id of ['hourly-forecast', 'daily-forecast']) if (!$(id).children.length) row($(id), ['No forecast available', '—', '—']);
    $('external-card').textContent = state.card?.active ? `${state.card.title}: ${state.card.value} ${state.card.unit} · expires in ${state.card.remaining_seconds}s` : '';
  }
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
    if (page === 'update' && !releaseChecked) {
      releaseChecked = true;
      $('release-channel').value = state.system.firmware_version.includes('-beta.') ? 'beta' : 'stable';
      checkRelease();
    }
    if (page !== 'clock') return;
    $('sync-status').textContent = state.time.ntp_synced ? 'Time synchronized' : 'Waiting for time';
    if (!state.time.ntp_synced) {
      $('clock-time').textContent = '--:--'; $('clock-seconds').textContent = '--';
      $('clock-date').textContent = 'Waiting for synchronization'; $('clock-period').textContent = '';
    }
    $('weather-city').textContent = weather.city || 'Your location';
    const valid = weather.enabled && weather.valid;
    const [description, symbol] = condition(weather.code);
    $('weather-temp').textContent = valid ? degrees(Number(weather.temperature)).toFixed(1) : '—';
    $('weather-unit').textContent = degreeUnit();
    $('weather-description').textContent = !weather.enabled ? 'Weather is switched off' : valid ? description : 'Waiting for weather';
    $('weather-symbol').textContent = valid ? weather.is_day === false && weather.code <= 2 ? '☾' : symbol : '—';
    $('weather-freshness').textContent = valid ? `${weather.stale ? 'Saved reading · ' : ''}Fetched ${Math.floor(weather.age_seconds / 60)} min ago · reading ${Math.floor((weather.source_age_seconds ?? weather.age_seconds) / 60)} min old` : '';
    const windUnit = state.units?.wind || 0;
    const wind = weather.windspeed * [1, 1 / 3.6, 0.621371][windUnit];
    const direction = weather.wind_direction >= 0 ? ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'][Math.floor((weather.wind_direction + 22) / 45) % 8] + ' ' : '';
    $('weather-wind').textContent = valid ? `${direction}${Math.round(wind)} ${['km/h', 'm/s', 'mph'][windUnit]}` : '—';
    $('sunrise').textContent = weather.sunrise; $('sunset').textContent = weather.sunset;
    $('display-note').textContent = state.display.night_active ? state.display.night_dim ? 'Night mode: display dimmed.' : 'Night mode: display off. Showing a PIN wakes it temporarily.' : 'Display active';
    $('active-screen').value = state.display.screen ?? 0;
    [...$('active-screen').options].forEach(option => { option.disabled = state.display.available ? !state.display.available[option.value] : Number(option.value) > 2; });
    $('screen-hold').textContent = state.display.paused ? 'Resume rotation' : 'Hold';
    renderForecast();
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

  async function displayControl(action, screen) {
    try {
      await request('/api/display', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({action, screen})});
      message($('screen-result'), action === 'show' ? 'Screen selected and held.' : 'Display updated.'); await refresh();
    } catch (error) { message($('screen-result'), error.message, true); }
  }
  $('active-screen').addEventListener('change', event => displayControl('show', Number(event.target.value)));
  $('screen-next').addEventListener('click', () => displayControl('next'));
  $('screen-hold').addEventListener('click', () => displayControl(state?.display?.paused ? 'resume' : 'hold'));
  $('download-diagnostics').addEventListener('click', () => {
    if (!state) return;
    const report = {firmware: state.system.firmware_version, uptime: state.system.uptime,
      free_heap: state.system.free_heap, max_free_block: state.system.max_free_block,
      heap_fragmentation: state.system.heap_fragmentation, ntp_synced: state.time.ntp_synced,
      wifi_rssi: state.wifi.rssi, weather_valid: state.weather.valid, weather_stale: state.weather.stale,
      weather_age_seconds: state.weather.age_seconds, source_age_seconds: state.weather.source_age_seconds,
      display: state.display};
    const url = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], {type: 'application/json'}));
    const link = document.createElement('a'); link.href = url; link.download = 'clock-diagnostics.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });

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
    const cityBytes = new TextEncoder().encode(formControl('city_name').value).length;
    $('city-bytes').textContent = cityBytes;
    formControl('city_name').setCustomValidity(cityBytes > 31 ? 'Use a shorter city label: at most 31 UTF-8 bytes. Accented and Cyrillic letters use more than one byte.' : '');
    settingsDirty = Object.keys(settingsChanges()).length > 0;
    $('save-settings').disabled = $('discard-settings').disabled = !settingsDirty || busy;
    document.querySelector('.save-bar').classList.toggle('dirty', settingsDirty);
    if (showMessage) message($('settings-result'), settingsDirty ? 'Unsaved changes.' : 'No unsaved changes.');
  }
  function fillSettings(config) {
    savedSettings = config;
    $('screen-preset').value = 'custom';
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
  settingsForm.addEventListener('input', event => {
    if (/^(show_|screen_|clock_weather$)/.test(event.target.name)) $('screen-preset').value = 'custom';
    settingsState(true);
  });
  settingsForm.addEventListener('change', () => settingsState(true));
  settingsForm.addEventListener('invalid', event => { const details = event.target.closest('details'); if (details) details.open = true; }, true);
  $('discard-settings').addEventListener('click', () => { if (savedSettings && !busy) fillSettings(savedSettings); });
  function stageSettings(values) {
    const original = savedSettings;
    const password = formControl('password').value, clear = formControl('clear_password').checked;
    fillSettings({...original, ...values}); savedSettings = original;
    formControl('password').value = password; formControl('clear_password').checked = clear; settingsState(true);
  }
  $('screen-preset').addEventListener('change', event => {
    if (!savedSettings || event.target.value === 'custom') return;
    const preset = event.target.value;
    const detailed = preset === 'weather';
    const values = {show_weather: detailed, show_sunrise_sunset: detailed, clock_weather: event.target.value !== 'clock',
      show_comfort: detailed, show_rain: detailed, show_daily: detailed, show_wind: detailed, show_uv: preset !== 'clock'};
    durationKeys.forEach(key => { values['screen_' + key + '_sec'] = key === 'clock' ? 20 : 5; });
    // A preset changes only its own controls, preserving other unsaved edits.
    stageSettings({...settingsChanges(), ...values});
    $('screen-preset').value = preset;
  });
  let importedSettings;
  $('import-settings').addEventListener('click', () => { if (savedSettings && !busy) $('import-file').click(); });
  $('import-file').addEventListener('change', async () => {
    const file = $('import-file').files[0];
    if (!file || !savedSettings || busy) return;
    try {
      if (file.size > 8192) throw new Error('Settings file must be at most 8 KB.');
      const values = JSON.parse(await file.text());
      if (!values || Array.isArray(values) || typeof values !== 'object') throw new Error('Choose a settings JSON object.');
      importedSettings = {};
      for (const [key, value] of Object.entries(values)) {
        if (key === 'firmware_version' || !Object.hasOwn(savedSettings, key)) continue;
        if (typeof value !== typeof savedSettings[key] || (typeof value === 'number' && !Number.isFinite(value))) throw new Error('Invalid type for ' + key);
        if (value !== savedSettings[key]) importedSettings[key] = value;
      }
      $('import-preview').textContent = Object.entries(importedSettings).map(([key, value]) => `${key}: ${JSON.stringify(savedSettings[key])} → ${JSON.stringify(value)}`).join('\n') || 'No changes.';
      $('apply-import').disabled = !Object.keys(importedSettings).length; $('import-dialog').showModal();
    } catch (error) { message($('settings-result'), error.message, true); }
    finally { $('import-file').value = ''; }
  });
  $('cancel-import').addEventListener('click', () => $('import-dialog').close());
  $('apply-import').addEventListener('click', () => {
    stageSettings({...settingsChanges(), ...importedSettings}); $('import-dialog').close();
    message($('settings-result'), 'Imported into the form. Review and save to apply.');
  });
  $('city-search').addEventListener('click', async () => {
    const query = $('city-query').value.trim();
    if (query.length < 2 || busy) { message($('city-search-result'), 'Enter at least two characters.', true); return; }
    $('city-search').disabled = true; $('city-results').replaceChildren();
    try {
      const data = await request('https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&name=' + encodeURIComponent(query));
      for (const city of data.results || []) {
        if (!Number.isFinite(city.latitude) || !Number.isFinite(city.longitude) || typeof city.name !== 'string') continue;
        const button = document.createElement('button'); button.type = 'button'; button.className = 'secondary';
        button.textContent = [city.name, city.admin1, city.country].filter(Boolean).join(', ');
        button.addEventListener('click', () => {
          const fits = new TextEncoder().encode(city.name).length <= 31;
          formControl('latitude').value = city.latitude; formControl('longitude').value = city.longitude;
          formControl('city_name').value = fits ? city.name : '';
          settingsState(true); message($('city-search-result'), `${button.textContent}. Coordinates selected.${fits ? '' : ' Enter a short city label.'} Check the UTC offset before saving.`);
        });
        $('city-results').append(button);
      }
      message($('city-search-result'), $('city-results').children.length ? 'Choose a location; changes are saved only with Save settings.' : 'No matching locations. Try city, country.');
    } catch { message($('city-search-result'), 'City search is unavailable. Enter coordinates manually or try again.', true); }
    finally { $('city-search').disabled = false; }
  });
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
      let actual = {...savedSettings, ...values};
      if (!result.restart) {
        try { actual = await request('/api/config'); }
        catch { throw Error('Could not verify saved settings. Reload Settings to check.'); }
        if (Object.entries(values).some(([key, value]) =>
          key === 'latitude' || key === 'longitude' ? !Number.isFinite(actual[key]) || Math.abs(actual[key] - value) > 0.00001 : actual[key] !== value))
          throw Error('Some settings were not saved. Your edits are still here; try Save again.');
      }
      fillSettings(actual);
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
  const onlineUpdate = () => $('update-source').value === 'github';
  function validateUpdate() {
    if (!onlineUpdate()) return validateFile();
    $('firmware-file').setCustomValidity('');
    $('upload-button').disabled = !selectedRelease || busy || checkingRelease;
    return !!selectedRelease && !checkingRelease;
  }
  async function checkRelease() {
    if (busy || checkingRelease) return;
    selectedRelease = null; checkingRelease = true;
    $('check-release').disabled = $('release-channel').disabled = true;
    $('release-notes').hidden = true; validateUpdate();
    message($('release-result'), 'Checking GitHub…');
    try {
      if (!state) throw Error('Connect to the clock before checking for updates.');
      const item = await releases.check($('release-channel').value);
      const newer = releases.compare(item.version, state.system.firmware_version) > 0;
      $('release-notes').href = releases.notes(item); $('release-notes').hidden = false;
      message($('release-result'), newer ? `v${item.version} available · ${(item.size / 1024).toFixed(1)} KB${item.version.includes('-') ? ' · Beta: experimental firmware' : ''}` : `No newer release. Latest in this channel: v${item.version}.`);
      if (newer) selectedRelease = item;
      if (onlineUpdate()) message($('upload-result'), newer ? 'Show the PIN and enter it to install this release.' : 'Your installed version is current or newer.');
    } catch (error) { message($('release-result'), error instanceof SyntaxError ? 'Invalid GitHub release catalog. Try again later.' : error.message, true); }
    finally { checkingRelease = false; $('check-release').disabled = $('release-channel').disabled = false; validateUpdate(); }
  }
  $('check-release').addEventListener('click', checkRelease);
  $('release-channel').addEventListener('change', checkRelease);
  $('update-source').addEventListener('change', () => {
    const online = onlineUpdate();
    $('release-options').hidden = !online; $('local-options').hidden = online;
    $('firmware-file').required = !online;
    $('upload-button').textContent = online ? 'Install & restart' : 'Upload & restart';
    message($('upload-result'), online ? 'Choose a release, then enter your PIN.' : 'Select a non-empty file to continue.');
    validateUpdate();
  });
  function validateFile() {
    const file = $('firmware-file').files[0];
    const error = !file ? 'Choose a non-empty file to continue.' : !file.size ? 'This file is empty. Choose another file.' : !/\.bin(\.gz)?$/i.test(file.name) ? 'Choose a .bin or .bin.gz file.' : '';
    $('firmware-file').setCustomValidity(error);
    $('upload-button').disabled = !!error || busy || checkingRelease;
    message($('file-info'), file ? `${file.name} · ${(file.size / 1024).toFixed(1)} KB` : 'No file selected.');
    message($('upload-result'), error || 'Ready when you have entered your PIN.', !!file && !!error);
    return !error && !checkingRelease;
  }
  $('firmware-file').addEventListener('change', validateFile);
  function upload(file, headers, type) {
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
      const form = new FormData(); form.append(type, file);
      xhr.send(form);
    });
  }
  $('update-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (busy || !validateUpdate() || !validPIN($('update-pin')) || !$('update-form').reportValidity()) return;
    const headers = pinHeader($('update-pin'));
    const item = selectedRelease, online = onlineUpdate();
    let file = $('firmware-file').files[0];
    const type = online ? 'firmware' : $('image-type').value;
    busy = true; $('upload-button').disabled = true; $('firmware-file').disabled = true;
    $('update-source').disabled = $('release-channel').disabled = $('check-release').disabled = true;
    $('image-type').disabled = true; $('update-pin').disabled = true;
    $('update-form').querySelector('[data-show-pin]').disabled = true;
    let success = false;
    try {
      message($('upload-result'), 'Checking PIN…');
      await request('/api/maintenance/verify', {method: 'POST', headers});
      if (online) {
        message($('upload-result'), 'Downloading from GitHub and checking SHA-256… Keep this page open.');
        file = await releases.download(item);
      }
      $('upload-progress').hidden = false; $('upload-progress').value = 0;
      await upload(file, headers, type); success = true;
      message($('upload-result'), 'Update complete. The clock is restarting. Return to Clock in a few seconds.');
    } catch (error) { message($('upload-result'), error.message, true); }
    finally {
      busy = false; $('update-pin').value = ''; $('update-pin').disabled = false;
      $('firmware-file').disabled = false; $('image-type').disabled = false;
      $('update-source').disabled = $('release-channel').disabled = $('check-release').disabled = success;
      $('update-form').querySelector('[data-show-pin]').disabled = false;
      $('upload-button').disabled = success || (online && !selectedRelease); // Prevent accidental duplicate submission.
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
