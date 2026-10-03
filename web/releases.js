// Browser-only release checks. No GitHub request or firmware buffer on the ESP.
/* exported releases */
const releases = (() => {
  const repo = 'petrochen/esp8266-weather-clock-opensource';
  const base = `https://raw.githubusercontent.com/${repo}/codex/firmware-updates/`;
  const target = 'esp01s-1m64-dio-80';
  const version = text => {
    const parts = typeof text === 'string' && /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-beta\.(0|[1-9]\d*))?$/.exec(text);
    if (!parts || parts.slice(1).some(value => value !== undefined && !Number.isSafeInteger(+value))) throw Error('Unrecognized firmware version. Use a local file.');
    return [+parts[1], +parts[2], +parts[3], parts[4] === undefined ? 1 : 0, +(parts[4] || 0)];
  };
  function compare(left, right) {
    const a = version(left), b = version(right);
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
  }
  function select(manifest, channel) {
    if (!manifest || manifest.schema !== 1 || manifest.target !== target) throw Error('The release catalog is incompatible with this clock.');
    const candidates = [manifest.stable, ...(channel === 'beta' ? [manifest.beta] : [])].filter(Boolean);
    for (const item of candidates) {
      version(item.version);
      if (typeof item.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(item.sha256) || !Number.isInteger(item.size) || item.size < 8 || item.size > 479232 ||
          (item === manifest.stable && item.version.includes('-'))) throw Error('Invalid release information. Use a local file.');
    }
    if (!candidates.length) throw Error('No release is available in this channel yet.');
    return candidates.sort((a, b) => compare(b.version, a.version))[0];
  }
  async function read(path, limit) {
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), limit <= 4096 ? 12000 : 60000);
    try {
      const response = await fetch(base + path, {signal: controller.signal, cache: 'no-store', credentials: 'omit', referrerPolicy: 'no-referrer'});
      if (!response.ok) throw Error(`GitHub download unavailable (${response.status}). Try again or use a local file.`);
      const reader = response.body.getReader(), chunks = [];
      let size = 0;
      while (true) {
        const {value, done} = await reader.read();
        if (done) break;
        size += value.length;
        if (size > limit) { controller.abort(); throw Error('GitHub returned an oversized file. Update stopped.'); }
        chunks.push(value);
      }
      const bytes = new Uint8Array(size);
      let offset = 0;
      for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
      return bytes;
    } catch (error) {
      if (error.name === 'AbortError' || error instanceof TypeError) throw Error('Cannot download from GitHub. Check your internet connection or use a local file.', {cause: error});
      throw error;
    } finally { clearTimeout(timer); }
  }
  // SHA-256 also works on the clock's plain HTTP page, where Web Crypto is absent.
  // Inputs are bounded above; the high 32 bits of the message length are zero.
  function sha256(bytes) {
    const constants = [0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
    const hash = new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
    const padded = new Uint8Array((bytes.length + 72) & ~63);
    padded.set(bytes); padded[bytes.length] = 128;
    const data = new DataView(padded.buffer), words = new Uint32Array(64);
    data.setUint32(padded.length - 4, bytes.length * 8);
    const rotate = (value, bits) => (value >>> bits) | (value << (32 - bits));
    for (let offset = 0; offset < padded.length; offset += 64) {
      let [a,b,c,d,e,f,g,h] = hash;
      for (let i = 0; i < 64; i++) {
        const x = words[i - 15], y = words[i - 2];
        words[i] = i < 16 ? data.getUint32(offset + i * 4) : words[i - 16] + (rotate(x,7) ^ rotate(x,18) ^ (x >>> 3)) + words[i - 7] + (rotate(y,17) ^ rotate(y,19) ^ (y >>> 10));
        const t1 = h + (rotate(e,6) ^ rotate(e,11) ^ rotate(e,25)) + ((e & f) ^ (~e & g)) + constants[i] + words[i];
        const t2 = (rotate(a,2) ^ rotate(a,13) ^ rotate(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
        h=g; g=f; f=e; e=(d+t1)|0; d=c; c=b; b=a; a=(t1+t2)|0;
      }
      [a,b,c,d,e,f,g,h].forEach((value, index) => { hash[index] += value; });
    }
    return Array.from(hash, value => value.toString(16).padStart(8,'0')).join('');
  }
  async function download(item) {
    const bytes = await read(`firmware/${item.sha256}.bin`, item.size);
    if (bytes.length !== item.size || sha256(bytes) !== item.sha256) throw Error('Firmware checksum mismatch. Nothing was uploaded. Try again.');
    if (bytes[0] !== 0xe9 || bytes[2] !== 2 || bytes[3] !== 0x20) throw Error('Firmware does not match the ESP-01S target. Update stopped.');
    return new File([bytes], `weather_clock-v${item.version}.bin`, {type: 'application/octet-stream'});
  }
  return {compare, select, sha256, download,
    notes: item => `https://github.com/${repo}/releases/tag/v${item.version}`,
    check: async channel => select(JSON.parse(new TextDecoder().decode(await read('channels.json', 4096))), channel)};
})();
