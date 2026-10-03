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
const stable = {version:'1.11.0', size:400000, sha256:'1'.repeat(64)};
const beta = {...stable, version:'1.12.0-beta.2'};
const catalog = {schema:1, target:'esp01s-1m64-dio-80', stable, beta};
assert.equal(api.select(catalog,'stable'), stable);
assert.equal(api.select(catalog,'beta'), beta);
assert.equal(api.select({...catalog, beta:{...beta,version:'1.11.0-beta.99'}},'beta'), stable);
for (const patch of [{schema:2}, {target:'esp32'}, {stable:{...stable,size:479233}}, {stable:{...stable,sha256:'broken'}}, {stable:beta}]) assert.throws(() => api.select({...catalog,...patch},'stable'));
assert.throws(() => api.select(null, 'stable'));
console.log('PASS: SHA-256 against Node crypto through maximum image size; semantic stable/beta ordering; malformed catalog, target, size and digest rejection');
