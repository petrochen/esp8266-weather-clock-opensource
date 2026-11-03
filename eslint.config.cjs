const js = require('@eslint/js');
const globals = require('globals');

module.exports = [
  {ignores: ['build/**', 'node_modules/**']},
  js.configs.recommended,
  {
    files: ['web/*.js'],
    languageOptions: {ecmaVersion: 2022, sourceType: 'script', globals: globals.browser},
  },
  {
    files: ['web/app.js'],
    // releases.js is concatenated before app.js by tools/minify_web.cjs.
    languageOptions: {globals: {releases: 'readonly'}},
  },
  {
    files: ['**/*.cjs'],
    languageOptions: {ecmaVersion: 2022, sourceType: 'commonjs', globals: globals.node},
  },
  {
    rules: {
      'no-unused-vars': ['error', {args: 'after-used', caughtErrors: 'none'}],
      'no-var': 'error',
      'prefer-const': 'error',
      eqeqeq: ['error', 'always'],
    },
  },
];
