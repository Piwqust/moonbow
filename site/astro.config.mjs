// @ts-check
import { defineConfig } from 'astro/config';
import sitemap from '@astrojs/sitemap';

// placeholder: the production URL (canonical, OG, sitemap depend on it)
export default defineConfig({
  site: 'https://lumen-player.example',
  integrations: [sitemap()],
  prefetch: { prefetchAll: true, defaultStrategy: 'hover' },
  build: { inlineStylesheets: 'auto' },
  trailingSlash: 'always',
  // 'class' lets a page's scoped styles reach child components through their class prop.
  scopedStyleStrategy: 'class',
});
