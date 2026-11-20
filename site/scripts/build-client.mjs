// Builds the whole client (src/client/main.ts with GSAP) into one classic script, public/assets/site.js.
// A classic IIFE instead of ES modules: modules don't load from file://, and the built site must also
// open from disk. `--watch` rebuilds on change (scripts/dev.mjs uses it).
import * as esbuild from 'esbuild';

const watch = process.argv.includes('--watch');
const options = {
  entryPoints: ['src/client/main.ts'],
  outfile: 'public/assets/site.js',
  bundle: true,
  format: 'iife',
  target: ['chrome111', 'firefox113', 'safari16.4'],
  minify: !watch,
  sourcemap: watch ? 'inline' : false,
  legalComments: 'none',
  logLevel: 'info',
};

if (watch) {
  const ctx = await esbuild.context(options);
  await ctx.watch();
} else {
  await esbuild.build(options);
}
