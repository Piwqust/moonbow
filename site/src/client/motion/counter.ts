// The idle state of the whole site: time on the page counted in frames of cinema, 24 per second,
// on the one GSAP clock. It is a button: pressing it pauses the count, like the player's Pause.
import { gsap } from 'gsap';
import { onPage } from '../engine/core';
import { FPS, prefersReducedMotion } from '../engine/env';

const plural = (n: number) => {
  const a = n % 10, b = n % 100;
  if (a === 1 && b !== 11) return 'кадр';
  if (a >= 2 && a <= 4 && (b < 12 || b > 14)) return 'кадра';
  return 'кадров';
};
const fmt = (n: number) => String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');

export const frameCounter = () => onPage(() => {
  const els = [...document.querySelectorAll<HTMLButtonElement>('[data-frames]')];
  if (!els.length) return;
  const start = performance.now();
  let pausedAt = 0, pausedFor = 0, last = -1, paused = prefersReducedMotion();
  const render = (n: number) => {
    for (const el of els) {
      const num = el.querySelector('[data-frames-n]');
      const unit = el.querySelector('[data-frames-unit]');
      if (num) num.textContent = fmt(n);
      if (unit) unit.textContent = plural(n);
    }
  };
  const now = () => performance.now() - start - pausedFor;
  const tick = () => {
    if (paused) return;
    const n = Math.floor((now() / 1000) * FPS);
    if (n !== last) { last = n; render(n); }
  };
  const setPaused = (p: boolean) => {
    if (p === paused) return;
    paused = p;
    if (p) pausedAt = performance.now();
    else pausedFor += performance.now() - pausedAt;
    for (const el of els) {
      el.setAttribute('aria-pressed', String(p));
      el.classList.toggle('is-paused', p);
    }
  };
  if (paused) { pausedAt = performance.now(); for (const el of els) { el.setAttribute('aria-pressed', 'true'); el.classList.add('is-paused'); } }
  render(0);
  for (const el of els) el.addEventListener('click', () => setPaused(!paused));
  gsap.ticker.add(tick);
});
