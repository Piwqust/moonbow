// @ts-check
import { defineConfig } from 'astro/config';

// The address the site is published at. Canonical links, OG tags, the sitemap and robots.txt use it
// (the last two are written by scripts/postbuild.mjs, so a sub-path like /moonbow works).
// GitHub Pages of this repository; change it here if the site moves to a domain of its own.
export const SITE_URL = 'https://piwqust.github.io/moonbow';

export default defineConfig({
  site: SITE_URL,
  trailingSlash: 'always',
  build: { inlineStylesheets: 'always', format: 'directory' },
  // The client code is one classic script built by scripts/build-client.mjs, not Astro's module
  // scripts: the built site must also work when opened from disk (file://), where modules can't load.
  devToolbar: { enabled: false },
});
