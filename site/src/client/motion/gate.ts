// The driver of the signature. One variable: the current frame. Scroll progress through the gate
// picks it; the strip, the perforations, the log, the ruler and the caption all follow that one value.
// The strip moves like film in a projector: intermittently. Scrolling is continuous, the film is not:
// each new frame is pulled in with a 0.16 s step and then held still.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { onPage } from '../engine/core';
import { ease, FRAME_MS, prefersReducedMotion } from '../engine/env';

gsap.registerPlugin(ScrollTrigger);

export const gate = () => onPage(() => {
  const root = document.querySelector<HTMLElement>('[data-gate]');
  if (!root || prefersReducedMotion()) return;
  const track = root.querySelector<HTMLElement>('[data-gate-track]')!;
  const aperture = root.querySelector<HTMLElement>('[data-gate-aperture]')!;
  const strip = root.querySelector<HTMLElement>('[data-gate-frames]')!;
  const texts = [...root.querySelectorAll<HTMLElement>('[data-text]')];
  const logs = [...root.querySelectorAll<HTMLElement>('[data-gate-log] li')];
  const cells = [...root.querySelectorAll<HTMLButtonElement>('[data-goto]')];
  const real = root.querySelector<HTMLButtonElement>('[data-gate-real]');
  const live = root.querySelector<HTMLElement>('[data-gate-live]');
  const N = texts.length;
  let current = -1;
  let playing = false;

  // The perforations need the frame height in px (4 holes per frame, as on 35 mm film).
  const measure = () => {
    aperture.style.setProperty('--fh', `${strip.offsetHeight}px`);
    aperture.style.setProperty('--gap-k', '0.07');
  };
  measure();
  const ro = new ResizeObserver(measure);
  ro.observe(strip);

  gsap.set(texts, { opacity: 0 });

  function show(i: number, instant = false) {
    if (i === current) return;
    const prev = current;
    current = i;
    if (instant) gsap.set(aperture, { '--pos': i });
    else gsap.to(aperture, { '--pos': i, duration: 0.16, ease: ease.pull, overwrite: true });
    const start = i * FRAME_MS, end = (i + 1) * FRAME_MS;
    for (const li of logs) {
      const ms = Number(li.dataset.ms);
      li.classList.toggle('is-past', ms < start);
      li.classList.toggle('is-now', ms >= start && ms < end);
    }
    cells.forEach((c, k) => {
      c.classList.toggle('is-now', k === i);
      c.classList.toggle('is-past', k < i);
      if (k === i) c.setAttribute('aria-current', 'step'); else c.removeAttribute('aria-current');
    });
    texts.forEach((t, k) => t.classList.toggle('is-now', k === i));
    if (prev >= 0) gsap.to(texts[prev], { opacity: 0, y: 12, duration: instant ? 0 : 0.12, ease: 'none', overwrite: true });
    gsap.fromTo(texts[i], { opacity: 0, y: -14 }, { opacity: 1, y: 0, duration: instant ? 0 : 0.22, ease: ease.pull, overwrite: true });
  }

  const st = ScrollTrigger.create({
    trigger: track,
    start: 'top top',
    end: 'bottom bottom',
    invalidateOnRefresh: true,
    onUpdate: (self) => { if (!playing) show(Math.min(N - 1, Math.floor(self.progress * N))); },
  });
  show(0, true);

  // Load every frame before the visitor gets there: at real speed there's no time to fetch.
  ScrollTrigger.create({
    trigger: root, start: 'top bottom+=100%', once: true,
    onEnter: () => root.querySelectorAll('img').forEach((img) => { img.loading = 'eager'; }),
  });

  const scrollFor = (i: number) => st.start + ((i + 0.5) / N) * (st.end - st.start);
  cells.forEach((c, i) => c.addEventListener('click', () => {
    scrollTo({ top: scrollFor(i), behavior: 'instant' });
    show(i);
    if (live) live.textContent = `Кадр ${i + 1} из ${N}: ${texts[i].querySelector('h3')?.textContent ?? ''}`;
  }));

  // Real speed: the same frames at 24 per second. Half a second, and it's over.
  real?.addEventListener('click', () => {
    if (playing) return;
    playing = true;
    real.setAttribute('aria-pressed', 'true');
    current = -1;
    const tl = gsap.timeline({
      onComplete: () => {
        playing = false;
        real.setAttribute('aria-pressed', 'false');
        const label = real.querySelectorAll('.roll > span');
        label.forEach((s) => { s.textContent = 'Ещё раз'; });
        if (live) live.textContent = `${N} кадров, ${((N * FRAME_MS) / 1000).toFixed(1).replace('.', ',')} секунды.`;
        show(Math.min(N - 1, Math.floor(st.progress * N)));
      },
    });
    for (let k = 0; k < N; k++) tl.add(() => show(k, true), k * (FRAME_MS / 1000));
    tl.add(() => {}, N * (FRAME_MS / 1000) + 0.9); // hold the last frame
  });

  return () => ro.disconnect();
});
