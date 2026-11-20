// Page transitions as a projector shutter: a black blade drops over the page (0.22 s), the next page
// loads, and the blade keeps falling off the new page (0.3 s). Plain links underneath: no router, so
// it works over http and from disk alike. The head script covers the new page before first paint
// (html.shutter-in) when the visitor arrived through the shutter.
import { gsap } from 'gsap';
import { ease, prefersReducedMotion } from './env';
import { startIntro } from './reveal';

const KEY = 'mb-shutter';
const store = {
  set: () => { try { sessionStorage.setItem(KEY, '1'); } catch {} },
};

function internal(a: HTMLAnchorElement, e: MouseEvent) {
  if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return false;
  if (a.target && a.target !== '_self') return false;
  if (a.hasAttribute('download') || a.dataset.noShutter !== undefined) return false;
  const url = new URL(a.href, location.href);
  if (url.protocol !== location.protocol || url.host !== location.host) return false;
  // Same page with a hash: let the browser scroll.
  if (url.pathname === location.pathname && url.search === location.search && url.hash) return false;
  return true;
}

export function initShutter(intro: gsap.core.Timeline) {
  const root = document.getElementById('shutter');
  const blade = root?.querySelector<HTMLElement>('.shutter__blade');
  const html = document.documentElement;
  if (!root || !blade) { startIntro(intro); return; }

  // Arrived through the shutter: open it, then play the page intro.
  if (html.classList.contains('shutter-in')) {
    gsap.set(blade, { yPercent: 0 });
    html.classList.remove('shutter-in');
    gsap.timeline()
      .to(blade, { yPercent: 100, duration: prefersReducedMotion() ? 0 : 0.3, ease: ease.pull })
      .add(() => { startIntro(intro); }, 0.08)
      .set(blade, { yPercent: -100 });
  } else {
    gsap.set(blade, { yPercent: -100 });
    startIntro(intro);
  }

  // Back/forward from the bfcache: the page may still be covered from leaving it.
  addEventListener('pageshow', (e) => { if (e.persisted) gsap.set(blade, { yPercent: -100 }); });

  document.addEventListener('click', (e) => {
    const a = (e.target as HTMLElement).closest?.('a');
    if (!a || !internal(a, e)) return;
    if (prefersReducedMotion()) return; // plain navigation
    e.preventDefault();
    store.set();
    gsap.timeline()
      .fromTo(blade, { yPercent: -100 }, { yPercent: 0, duration: 0.22, ease: ease.in })
      .add(() => location.assign(a.href));
  });

  // Over http, fetch the next page while the pointer is on its link: by the click it's ready.
  if (location.protocol.startsWith('http')) {
    const seen = new Set<string>();
    const warm = (e: Event) => {
      const a = (e.target as HTMLElement).closest?.('a');
      if (!a || !a.href || seen.has(a.href)) return;
      const url = new URL(a.href);
      if (url.host !== location.host || url.pathname === location.pathname) return;
      seen.add(a.href);
      const l = document.createElement('link');
      l.rel = 'prefetch';
      l.href = url.pathname;
      document.head.appendChild(l);
    };
    document.addEventListener('pointerover', warm, { passive: true });
    document.addEventListener('focusin', warm);
  }
}
