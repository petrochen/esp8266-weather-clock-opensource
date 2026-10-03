// SHA-256 is required on insecure LAN origins, without Web Crypto or packages.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const context = vm.createContext({});
const api = vm.runInContext(fs.readFileSync('web/releases.js', 'utf8') + '; releases', context);
for (const length of [0, 1, 3, 55, 56, 63, 64, 65, 119, 120, 127, 128, 479232]) {
  const bytes = Buffer.alloc(length);
  for (let i = 0; i < length; i++) bytes[i] = (i * 73 + 19) & 255;
  assert.equal(api.sha256(bytes), crypto.createHash('sha256').update(bytes).digest('hex'), `SHA-256 length ${length}`);
}
assert.equal(api.sha256(Buffer.from('abc')), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
for (const [older, newer] of [['1.9.10','1.10.0'], ['1.11.0-beta.2','1.11.0-beta.10'], ['1.11.0-beta.10','1.11.0'], ['1.11.0','1.12.0-beta.1']]) {
  assert.equal(api.compare(older, newer), -1);
  assert.equal(api.compare(newer, older), 1);
  assert.equal(api.compare(older, older), 0);
}
for (const value of ['', ['1.2.3'], null, 'v1.2.3', '1.2', '1.2.3-rc.1', '1.2.3-beta.01', '1.2.9007199254740992', '1.2.3/<img>']) assert.throws(() => api.compare(value, '1.0.0'));
const item = v => ({version:v, size:400000, sha256:'1'.repeat(64), revision:'0123456789ab', published:'2026-10-03T00:00:00Z', status:'available', profile:{storage:'eeprom-v1', github:true, forecast:true, version_picker:true}});
const stable = item('1.11.0'), beta = item('1.12.0-beta.2');
const catalog = {schema:2, target:'esp01s-1m64-dio-80', published:'2026-10-03T00:00:00Z', recommended:{stable:stable.version, beta:beta.version}, releases:[stable,beta]};
assert.equal(api.catalog(catalog), catalog);
for (const patch of [{schema:1}, {target:'esp32'}, {published:'broken'}, {recommended:{stable:beta.version}}, {recommended:{beta:'1.2.3'}}, {releases:[stable,stable]}, {releases:[{...stable,size:479233},beta]}, {releases:[{...stable,sha256:'broken'},beta]}, {releases:[{...stable,profile:{}},beta]}, {releases:[{...stable,status:'withdrawn'},beta]}]) assert.throws(() => api.catalog({...catalog,...patch}));
assert.throws(() => api.catalog(null));
assert.equal(api.transition(beta,'1.11.0-beta.4').action, 'Update');
assert.equal(api.transition(stable,'1.11.0').action, 'Reinstall');
assert.equal(api.transition(stable,'1.11.0').confirm, true);
assert.equal(api.transition(stable,'1.12.0-beta.2').action, 'Downgrade');
const old = item('1.10.0'); old.profile = {storage:'eeprom-v1',github:false,forecast:false,version_picker:false};
assert.match(api.transition({...stable,profile:{...stable.profile,version_picker:false}},'1.12.0-beta.4').note, /latest-release updates remain/);
const change = api.transition(old,'1.11.0-beta.4');
assert.equal(change.allowed, true);
assert.match(change.note, /GitHub updates will be unavailable/);
assert.match(change.note, /UV/);
old.profile.forecast = true;
assert.doesNotMatch(api.transition(old,'1.11.0-beta.4').note, /UV/);
old.profile.storage = 'eeprom-v2';
assert.equal(api.transition(old,'1.11.0-beta.4').allowed, false);
assert.equal(api.transition({...stable,status:'withdrawn'},'1.11.0-beta.4').allowed, false);
console.log('PASS: SHA-256 boundaries/max size; version ordering; history/recommendation validation; reinstall, downgrade, unknown storage and withdrawn builds');
