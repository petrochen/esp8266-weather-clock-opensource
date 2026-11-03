// Browser-only translation. The firmware stores one language index; the
// dictionaries live in the repository (web/i18n/<code>.json) and are fetched by
// the browser, because the 1 MB flash has no room for them. Missing keys and a
// failed download fall back to English. Values only ever become text, never HTML.
/* exported i18n */
const i18n = (() => {
  const codes = ['en', 'it', 'de', 'fr'];
  const locales = {en: 'en-GB', it: 'it-IT', de: 'de-DE', fr: 'fr-FR'};
  const source = 'https://raw.githubusercontent.com/petrochen/esp8266-weather-clock-opensource/main/web/i18n/';
  const attributes = ['placeholder', 'aria-label', 'title'];
  let code = 'en', dictionary = {}, pending = null;
  const originals = new WeakMap();
  function t(text, values) {
    let out = typeof dictionary[text] === 'string' ? dictionary[text] : text;
    if (values) for (const [key, value] of Object.entries(values)) out = out.split('{' + key + '}').join(value);
    return out;
  }
  // Translate every static text node and attribute, remembering the English original
  // so the page can switch between languages without reloading.
  function apply() {
    if (typeof document === 'undefined') return;
    document.documentElement.lang = code;
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      if (node.nodeType === Node.TEXT_NODE) {
        const parent = node.parentElement;
        if (!parent || parent.closest('script, style, [data-i18n-skip]')) continue;
        const original = originals.get(node) ?? node.data, trimmed = original.trim();
        if (!trimmed) continue;
        originals.set(node, original);
        node.data = original.replace(trimmed, t(trimmed));
      } else {
        const saved = originals.get(node) || {};
        for (const name of attributes) {
          if (!node.hasAttribute(name)) continue;
          saved[name] ??= node.getAttribute(name);
          node.setAttribute(name, t(saved[name]));
        }
        originals.set(node, saved);
      }
    }
  }
  // Switch to the firmware's language index. Resolves to '' on success or to the
  // requested code when its dictionary could not be loaded (English is shown).
  async function select(index) {
    const next = codes[index] || 'en';
    if (next === code) return '';
    if (pending) await pending.catch(() => {});
    if (next === code) return '';
    pending = (async () => {
      let loaded = {};
      if (next !== 'en') {
        const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 12000);
        try {
          const response = await fetch(source + next + '.json', {signal: controller.signal, credentials: 'omit', referrerPolicy: 'no-referrer'});
          if (!response.ok) throw Error('HTTP ' + response.status);
          const data = await response.json();
          if (!data || typeof data !== 'object' || Array.isArray(data)) throw Error('Invalid dictionary');
          for (const [key, value] of Object.entries(data)) if (typeof value === 'string') loaded[key] = value;
        } catch {
          loaded = null;
        } finally { clearTimeout(timer); }
      }
      dictionary = loaded || {};
      code = loaded ? next : 'en';
      apply();
      return loaded ? '' : next;
    })();
    try { return await pending; } finally { pending = null; }
  }
  return {t, select, apply, codes,
    code: () => code,
    locale: () => locales[code],
    names: ['English', 'Italiano', 'Deutsch', 'Français'],
    install: (next, data) => { code = next; dictionary = data; }};
})();
