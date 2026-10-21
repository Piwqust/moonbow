// Page transitions: "stop frame". The page goes to black like Stop → 0:00, the next one develops
// from black. Adapters run leave() while the next page loads, so it adds no latency.
import { gsap } from 'gsap';
import { ease, prefersReducedMotion } from './env';

const curtain = () => document.getElementById('curtain') as HTMLElement;

export const playLeave = () => Promise.resolve(gsap.timeline()
  .set(curtain(), { autoAlpha: 0 })
  .to(curtain(), { autoAlpha: 1, duration: prefersReducedMotion() ? 0.15 : 0.35, ease: ease.soft }));

export const playEnter = () => Promise.resolve(gsap.to(curtain(), {
  autoAlpha: 0, duration: prefersReducedMotion() ? 0.15 : 0.5, ease: ease.soft,
}));
