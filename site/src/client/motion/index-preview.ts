// The "Умеет всё" index: a small film gate follows the pointer. Each row of the index is a frame on
// its strip; moving to another row pulls the strip to that frame, the same step as the big gate.
// Touch and reduced motion: each row shows its own thumbnail (CSS), nothing follows anything.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { onPage } from '../engine/core';
import { ease, isTouch, prefersReducedMotion } from '../engine/env';

export const indexPreview = () => onPage(() => {
  const list = document.querySelector<HTMLElement>('[data-index]');
  const box = document.querySelector<HTMLElement>('[data-index-preview]');
  if (!list || !box || isTouch() || prefersReducedMotion()) return;
  const rows = [...list.querySelectorAll<HTMLAnchorElement>('a[data-row]')];
  const x = gsap.quickTo(box, 'x', { duration: 0.5, ease: 'power3.out' });
  const y = gsap.quickTo(box, 'y', { duration: 0.5, ease: 'power3.out' });
  let px = 0, py = 0, shown = false, at = -1;
  gsap.set(box, { autoAlpha: 0 });

  const go = (i: number) => {
    if (i === at) return;
    const first = at < 0;
    at = i;
    gsap.to(box, { '--pos': i, duration: first ? 0 : 0.16, ease: ease.pull, overwrite: 'auto' });
  };
  const show = (i: number) => {
    go(i);
    if (shown) return;
    shown = true;
    gsap.to(box, { autoAlpha: 1, duration: 0.18, ease: 'none', overwrite: 'auto' });
  };
  const hide = () => {
    if (!shown) return;
    shown = false;
    gsap.to(box, { autoAlpha: 0, duration: 0.16, ease: 'none', overwrite: 'auto' });
  };
  rows.forEach((a, i) => {
    a.addEventListener('pointerenter', () => show(i));
    a.addEventListener('focus', () => {
      const r = a.getBoundingClientRect();
      x(r.right - box.offsetWidth - 24); y(r.top + r.height / 2);
      show(i);
    });
    a.addEventListener('blur', hide);
  });
  list.addEventListener('pointerleave', hide);
  addEventListener('pointermove', (e) => { px = e.clientX; py = e.clientY; x(px); y(py); }, { passive: true });
  // pointerleave never fires when the page scrolls out from under a still pointer.
  ScrollTrigger.create({ trigger: list, start: 'top bottom', end: 'bottom top', onUpdate: () => {
    if (shown && !list.contains(document.elementFromPoint(px, py))) hide();
  } });
});
