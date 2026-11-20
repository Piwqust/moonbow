// The whole client of the site: one classic script (scripts/build-client.mjs).
import { boot } from './engine/core';
import { frameCounter } from './motion/counter';
import { header } from './motion/header';
import { gate } from './motion/gate';
import { indexPreview } from './motion/index-preview';
import { filters } from './motion/filter';
import { exposure } from './motion/exposure';

header();
frameCounter();
gate();
indexPreview();
filters();
exposure();

if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => boot(), { once: true });
else boot();
