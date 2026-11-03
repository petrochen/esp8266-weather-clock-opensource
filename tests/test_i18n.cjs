// Translation dictionaries stay complete and in sync with the UI sources.
// `node tests/test_i18n.cjs --keys` prints every translatable English string.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const html = fs.readFileSync('web/index.html', 'utf8');
const scripts = ['web/app.js', 'web/releases.js'].map(file => fs.readFileSync(file, 'utf8')).join('\n');
const entities = text => text.replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>');

// Static page text: translated attributes everywhere, text nodes outside data-i18n-skip elements.
let markup = html.replace(/<script>[\s\S]*?<\/script>|<style>[\s\S]*?<\/style>/g, '');
const keys = new Set();
for (const match of markup.matchAll(/\s(?:placeholder|aria-label|title)="([^"]+)"/g)) if (/[A-Za-z]{2}/.test(match[1])) keys.add(entities(match[1]));
markup = markup.replace(/<(\w+)([^>]*)data-i18n-skip[^>]*>[\s\S]*?<\/\1>/g, '');
for (const match of markup.matchAll(/>([^<>]+)</g)) {
  const text = entities(match[1]).trim();
  if (/[A-Za-z]{2}/.test(text)) keys.add(text);
}
// Script text: every string literal that reads like a label or a sentence.
const literals = new Set();
for (const match of scripts.matchAll(/'((?:[^'\\\n]|\\.)*)'/g)) literals.add(match[1].replace(/\\'/g, "'"));
for (const literal of literals) {
  const sentence = /^[A-Za-z{][A-Za-z0-9 ,.;:!?'’&()%{}°²…·/-]*[^ ]$/.test(literal) && (/ /.test(literal) || /^[A-Z]/.test(literal));
  if (sentence && /[A-Za-z]{2}/.test(literal) && !/^[A-Za-z]+Error$/.test(literal)) keys.add(literal);
}
for (const literal of ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW', 'today', 'tomorrow', 'none found']) keys.add(literal);
// Product names, identifiers and units are the same in every language.
const untranslated = new Set(['Weather Clock', 'TJ-56-654', 'ESP8266', 'ESP-01S / ESP8266', 'DIO / 80MHz', '1MB / 64KB FS', 'Open-Meteo',
  'PIN', 'OK', 'AM', 'PM', 'UTC', 'Bearer ', 'Update Success!', 'Update error:', 'Authorization', 'Content-Type', 'POST', 'GET', 'Portimão',
  'Cache-Control', 'Request failed', 'Accept', 'Expected a JSON object', 'Status JSON', 'km/h', 'm/s', 'mph', 'use strict']);
for (const item of untranslated) keys.delete(item);
const placeholders = text => [...text.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort().join(',');

if (process.argv.includes('--keys')) {
  console.log(JSON.stringify(Object.fromEntries([...keys].sort().map(key => [key, ''])), null, 2));
  process.exit(0);
}

const context = vm.createContext({});
const api = vm.runInContext(fs.readFileSync('web/i18n.js', 'utf8') + '; i18n', context);
assert.equal(api.codes.join(), 'en,it,de,fr'); // arrays cross the vm realm boundary
assert.equal(api.names.length, api.codes.length);
assert.equal(api.t('Missing key'), 'Missing key');
assert.equal(api.t('Fetched {minutes} min ago', {minutes: 5}), 'Fetched 5 min ago');
const expected = [...keys].sort();
for (const code of api.codes.slice(1)) {
  const dictionary = JSON.parse(fs.readFileSync(`web/i18n/${code}.json`, 'utf8'));
  const names = Object.keys(dictionary);
  const dead = names.filter(key => !keys.has(key));
  assert.deepEqual(dead, [], `${code}: translations without a source string`);
  const missing = expected.filter(key => !names.includes(key));
  assert.deepEqual(missing, [], `${code}: untranslated strings (run --keys)`);
  for (const [key, value] of Object.entries(dictionary)) {
    assert.ok(typeof value === 'string' && value.trim(), `${code}: empty translation for ${JSON.stringify(key)}`);
    assert.equal(placeholders(value), placeholders(key), `${code}: placeholders differ for ${JSON.stringify(key)}`);
    assert.ok(!/[<>]/.test(value), `${code}: markup in translation for ${JSON.stringify(key)}`);
  }
  api.install(code, dictionary);
  assert.equal(api.code(), code);
  assert.notEqual(api.t('Settings'), 'Settings', `${code}: Settings must be translated`);
  assert.equal(api.t('Install v{version}', {version: '1.2.3'}).includes('1.2.3'), true);
  assert.ok(/-/.test(api.locale()));
}
console.log(`PASS: ${expected.length} UI strings, complete it/de/fr dictionaries, placeholders preserved, no markup, t() substitution`);
