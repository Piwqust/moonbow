// Home page: the live hero clock and the evening driver.
// The driver turns one variable (the fractional stop index under the reading line) into
// everything that carries the concept: the timecode, the ruler head, the unit, the active
// stop and the window on the right. All of it runs on GSAP's ticker via the engine's onScroll.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { onPage, onScroll, ease, prefersReducedMotion } from '../engine';
import { stops } from '../data/site';
import { fmt, logLerp, LOG_MIN, LOG_MAX } from './time';

const pos = (t: number) => (Math.log10(t) - LOG_MIN) / (LOG_MAX - LOG_MIN);

let started = false;
export function startHome() {
  if (started) return;
  started = true;
  onPage(({ intro }) => {
    const offs: (() => void)[] = [];
    const clock = document.querySelector<HTMLElement>('[data-clock]');
    const ev = document.querySelector<HTMLElement>('[data-evening]');
    if (clock) offs.push(heroClock(clock, intro));
    if (ev) offs.push(evening(ev));
    return () => offs.forEach((f) => f());
  });
}

// Idle state: the hero timecode counts the visitor's time on the site, like a playing file.
function heroClock(el: HTMLElement, intro: gsap.core.Timeline) {
  if (prefersReducedMotion()) return () => {};
  const dark = document.querySelector<HTMLElement>('.hero__shot .shot__dark');
  // The picture develops from black in 200 ms, exactly as Lumen does it.
  if (dark) intro.add(gsap.to(dark, { opacity: 0, duration: 0.2, ease: 'none' }), 0.35);
  document.querySelector<HTMLElement>('[data-clock-static]')!.hidden = true;
  document.querySelector<HTMLElement>('[data-clock-live]')!.hidden = false;
  const count = document.querySelector<HTMLElement>('[data-clock-count]')!;
  let last = -1;
  const tick = () => {
    const t = performance.now() / 1000;
    el.textContent = fmt(t);
    const n = Math.floor(t / 0.085);
    if (n !== last) { count.textContent = n.toLocaleString('ru-RU'); last = n; }
  };
  tick();
  const st = ScrollTrigger.create({
    trigger: el, start: 'top bottom', end: 'bottom top',
    onToggle: (s) => (s.isActive ? gsap.ticker.add(tick) : gsap.ticker.remove(tick)),
  });
  if (st.isActive) gsap.ticker.add(tick);
  return () => { gsap.ticker.remove(tick); st.kill(); };
}

function evening(root: HTMLElement) {
  const items = [...root.querySelectorAll<HTMLElement>('[data-stop]')];
  const titles = items.map((li) => li.querySelector<HTMLElement>('.stop__title')!);
  const timeEl = root.querySelector<HTMLElement>('[data-ev-time]')!;
  const unitEl = root.querySelector<HTMLElement>('[data-ev-unit]')!;
  const head = root.querySelector<HTMLElement>('[data-ev-head]')!;
  const track = head.parentElement!;
  const screens = new Map([...root.querySelectorAll<HTMLElement>('[data-screen]')].map((s) => [s.dataset.screen!, s]));
  const darkOf = (name: string) => screens.get(name)?.querySelector<HTMLElement>('.shot__dark');
  const reduced = prefersReducedMotion();
  const T = stops.map((s) => s.t);
  let width = track.clientWidth;
  let active = 0;
  let shown = stops[0].shot;
  let tl: gsap.core.Timeline | null = null;

  head.style.left = '0';
  const setActive = (i: number) => {
    items[active]?.classList.remove('is-active');
    items[i].classList.add('is-active');
    active = i;
    unitEl.textContent = stops[i].unit;
    const name = stops[i].shot;
    const dark = darkOf(name);
    const darkTo = stops[i].dark ? 1 : 0;
    tl?.kill();
    tl = gsap.timeline();
    if (name !== shown) {
      // One thing at a time: the old window goes to black, then the new one develops.
      const old = screens.get(shown)!;
      const next = screens.get(name)!;
      tl.to(old, { autoAlpha: 0, duration: reduced ? 0 : 0.16, ease: 'power1.in' })
        .set(dark ?? {}, { opacity: darkTo })
        .fromTo(next, { autoAlpha: 0 }, { autoAlpha: 1, duration: reduced ? 0 : 0.3, ease: ease.soft });
      shown = name;
    } else if (dark) {
      tl.to(dark, { opacity: darkTo, duration: reduced ? 0 : 0.2, ease: 'none' });
    }
  };

  const update = () => {
    const r = root.getBoundingClientRect();
    if (r.bottom < 0 || r.top > innerHeight) return;
    const ref = innerHeight * 0.6;
    const a = titles.map((t) => t.getBoundingClientRect().top);
    let f = 0;
    if (a[a.length - 1] <= ref) f = a.length - 1;
    else for (let i = 0; i < a.length - 1; i++) {
      if (a[i] <= ref && a[i + 1] > ref) { f = i + (ref - a[i]) / (a[i + 1] - a[i]); break; }
    }
    const i = Math.floor(f);
    const t = i >= T.length - 1 ? T[T.length - 1] : logLerp(T[i], T[i + 1], f - i);
    timeEl.textContent = fmt(t);
    head.style.translate = `${pos(t) * width}px 0`;
    const near = Math.min(items.length - 1, Math.round(f));
    if (near !== active || !items[near].classList.contains('is-active')) setActive(near);
  };

  const resize = () => { width = track.clientWidth; update(); };
  addEventListener('resize', resize);
  const off = onScroll(update);
  update();
  return () => { off(); removeEventListener('resize', resize); tl?.kill(); };
}
