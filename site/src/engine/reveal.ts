// Declarative motion through data attributes:
//   data-split="lines|words"   masked text reveal (SplitText), on scroll or on intro
//   data-reveal="fade|up"      block reveal; "fade" develops from black like Lumen's picture
//   data-intro                 play with the page intro instead of on scroll
//   data-delay="0.2"           extra delay (seconds)
// Skipped under reduced motion; CSS keeps content visible without JS.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { SplitText } from 'gsap/SplitText';
import { ease, prefersReducedMotion } from './env';

gsap.registerPlugin(ScrollTrigger, SplitText);

const durations = { lines: 1.1, words: 1 } as const;
const staggers = { lines: 0.08, words: 0.03 } as const;

// autoSplit re-splits after font load / resize; a text that already revealed must stay revealed.
function splitTween(el: HTMLElement, isIntro: boolean) {
  const type = (el.dataset.split || 'lines') as keyof typeof durations;
  const delay = parseFloat(el.dataset.delay || '0');
  const s = { tween: undefined as gsap.core.Tween | undefined, done: false, requested: false };
  SplitText.create(el, {
    type: type === 'lines' ? 'lines' : 'lines,words',
    mask: 'lines',
    autoSplit: true,
    linesClass: 'split-line',
    onSplit(self) {
      gsap.set(el, { visibility: 'visible' });
      if (s.done) return;
      s.tween = gsap.from(type === 'lines' ? self.lines : self.words, {
        yPercent: 110,
        duration: durations[type],
        stagger: staggers[type],
        ease: ease.out,
        delay,
        paused: isIntro && !s.requested,
        onComplete: () => { s.done = true; },
        scrollTrigger: isIntro ? undefined : { trigger: el, start: 'top 88%', once: true },
      });
      return s.tween;
    },
  });
  return () => { s.requested = true; s.tween?.play(); };
}

function blockTween(el: HTMLElement, paused: boolean) {
  const kind = el.dataset.reveal || 'fade';
  const delay = parseFloat(el.dataset.delay || '0');
  gsap.set(el, { visibility: 'visible' });
  return gsap.fromTo(el,
    kind === 'up' ? { y: 32, autoAlpha: 0 } : { autoAlpha: 0, filter: 'brightness(0)' },
    {
      y: 0, autoAlpha: 1, filter: 'brightness(1)', duration: kind === 'up' ? 1.1 : 0.6, ease: ease.soft, delay, paused,
      clearProps: 'filter,transform',
      scrollTrigger: paused ? undefined : { trigger: el, start: 'top 90%', once: true },
    });
}

/** Wire every reveal inside `root`. Returns the intro timeline (paused) for the page entrance. */
export function initReveals(root: ParentNode = document) {
  const intro = gsap.timeline({ paused: true });
  if (prefersReducedMotion()) return intro;
  root.querySelectorAll<HTMLElement>('[data-split]').forEach((el) => {
    const isIntro = el.hasAttribute('data-intro');
    const play = splitTween(el, isIntro);
    if (isIntro) intro.add(play, 0);
  });
  root.querySelectorAll<HTMLElement>('[data-reveal]').forEach((el) => {
    const isIntro = el.hasAttribute('data-intro');
    const t = blockTween(el, isIntro);
    if (isIntro) intro.add(() => t.play(), 0);
  });
  return intro;
}
