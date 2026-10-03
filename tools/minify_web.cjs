// Build-time only: the device and browser do not load esbuild or other packages.
const fs = require('node:fs');
const path = require('node:path');
const esbuild = require('esbuild');
const root = path.resolve(__dirname, '..');
const result = {};
for (const [key, file, loader] of [['js', 'app.js', 'js'], ['css', 'app.css', 'css']]) {
  result[key] = esbuild.transformSync(fs.readFileSync(path.join(root, 'web', file), 'utf8'), {
    loader, minify: true, target: 'es2020', legalComments: 'none', charset: 'utf8'
  }).code;
}
process.stdout.write(JSON.stringify(result));
