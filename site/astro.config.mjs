// @ts-check
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

// PLACEHOLDER: the production address. Canonical links, OG tags and the sitemap use it.
// Change it here, in src/data/site.ts (site.url) and in public/robots.txt.
export const SITE_URL = 'https://moonbow-player.example';

export default defineConfig({
  site: SITE_URL,
  trailingSlash: 'always',
  integrations: [sitemap()],
  build: { inlineStylesheets: 'always', format: 'directory' },
  // The client code is one classic script built by scripts/build-client.mjs, not Astro's module
  // scripts: the built site must also work when opened from disk (file://), where modules can't load.
  devToolbar: { enabled: false },
});
