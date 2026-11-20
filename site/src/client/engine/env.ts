import { gsap } from 'gsap';
import { CustomEase } from 'gsap/CustomEase';

gsap.registerPlugin(CustomEase);

export const prefersReducedMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
export const isTouch = () => matchMedia('(hover: none), (pointer: coarse)').matches;
export const isMobile = () => matchMedia('(max-width: 767px)').matches;

// One easing family (expo) plus the site's own verb: `pull`, the film pulled one frame through the
// gate: a quick start, a hard stop. Every motion here is one of these.
CustomEase.create('pull', 'M0,0 C0.6,0 0.25,1 1,1');
export const ease = {
  pull: 'pull',
  out: 'expo.out',
  inOut: 'expo.inOut',
  in: 'expo.in',
} as const;

// One second of cinema: 24 frames. The site's unit of time.
export const FPS = 24;
export const FRAME_MS = 1000 / FPS;
