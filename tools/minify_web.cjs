// Build-time only: the device and browser do not load esbuild or other packages.
const fs = require('node:fs');
const path = require('node:path');
const esbuild = require('esbuild');
const root = path.resolve(__dirname, '..');
const result = {};
for (const [key, file, loader] of [['js', 'app.js', 'js'], ['css', 'app.css', 'css']]) {
  const prelude = key === 'js' ? ['releases.js', 'i18n.js'].map(name => fs.readFileSync(path.join(root, 'web', name), 'utf8')).join('\n') + '\n' : '';
  const source = prelude + fs.readFileSync(path.join(root, 'web', file), 'utf8');
  result[key] = esbuild.transformSync(source, {
    loader, minify: true, target: 'es2020', legalComments: 'none', charset: 'utf8'
  }).code;
}
process.stdout.write(JSON.stringify(result));
