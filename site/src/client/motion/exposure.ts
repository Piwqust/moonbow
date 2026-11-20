// A long exposure for the moonbow mark in the footer: white to the eye at first, its pale spectrum
// develops once the mark has stayed in view for a moment (the colour change itself is CSS, 2.4 s).
// "Если долго ждать, она станет цветной." Reduced motion: CSS shows the colours from the start.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { onPage } from '../engine/core';
import { prefersReducedMotion } from '../engine/env';

export const exposure = () => onPage(() => {
  if (prefersReducedMotion()) return;
  document.querySelectorAll<HTMLElement>('[data-exposure]').forEach((el) => {
    let call: gsap.core.Tween | null = null;
    const st = ScrollTrigger.create({
      trigger: el, start: 'top 90%', end: 'bottom top',
      onToggle: (self) => {
        call?.kill();
        if (self.isActive) call = gsap.delayedCall(1.2, () => { el.classList.add('is-exposed'); st.kill(); });
      },
    });
  });
});
