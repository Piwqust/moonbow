// Page lifecycle for a multi-page site with no client router: every page load boots once.
// Modules register with onPage(fn); fn runs at boot inside a gsap.context and may extend the intro.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { prefersReducedMotion } from './env';
import { initReveals } from './reveal';
import { initShutter } from './shutter';

gsap.registerPlugin(ScrollTrigger);

export type PageFn = (page: { intro: gsap.core.Timeline; path: string }) => void;
const pageFns: PageFn[] = [];
export const onPage = (fn: PageFn) => { pageFns.push(fn); };

export function syncFlags() {
  const h = document.documentElement.classList;
  h.add('js', 'is-booted');
  h.toggle('motion', !prefersReducedMotion());
}

export function boot() {
  syncFlags();
  // Times live in the same ticker as everything else; no lag smoothing: a frame counter must not lie.
  gsap.ticker.lagSmoothing(0);
  const ctx = gsap.context(() => {
    const intro = initReveals(document);
    for (const fn of pageFns) fn({ intro, path: location.pathname });
    initShutter(intro);
  });
  // Fonts and images change layout: re-measure the pinned sections once they settle.
  document.fonts?.ready.then(() => ScrollTrigger.refresh());
  addEventListener('load', () => ScrollTrigger.refresh(), { once: true });
  return ctx;
}
