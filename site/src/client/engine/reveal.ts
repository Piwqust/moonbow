// Declarative reveals, all in the site's verb (pulled down into place, or developed from black):
//   data-split="lines"      lines slide DOWN into their mask, like a frame pulled into the gate
//   data-reveal="down"      a block drops a few pixels into place and appears
//   data-reveal="develop"   an image develops from black in 200 ms, as Moonbow shows a picture
//   data-intro              play with the page intro instead of on scroll
//   data-delay="0.1"        extra delay, seconds
// Under reduced motion nothing hides and nothing moves.
import { gsap } from 'gsap';
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { SplitText } from 'gsap/SplitText';
import { ease, prefersReducedMotion } from './env';

gsap.registerPlugin(ScrollTrigger, SplitText);

// SplitText waits for the fonts before it splits, which can be after the intro has started:
// a split that arrives late plays at once instead of waiting for a cue that already passed.
const intro = { started: false };
export function startIntro(tl: gsap.core.Timeline) {
  intro.started = true;
  tl.play();
}

function splitLines(el: HTMLElement, tl: gsap.core.Timeline, isIntro: boolean) {
  const delay = parseFloat(el.dataset.delay || '0');
  const state = { done: false };
  SplitText.create(el, {
    type: 'lines',
    mask: 'lines',
    autoSplit: true,
    linesClass: 'split-line',
    onSplit(self) {
      gsap.set(el, { visibility: 'visible' });
      if (state.done) return;
      const tween = gsap.from(self.lines, {
        yPercent: -105,
        duration: 0.55,
        stagger: 0.06,
        ease: ease.pull,
        delay,
        paused: isIntro,
        onComplete: () => { state.done = true; },
        scrollTrigger: isIntro ? undefined : { trigger: el, start: 'top 90%', once: true },
      });
      if (isIntro) {
        if (intro.started) tween.play();
        else tl.add(() => { tween.play(); }, 0);
      }
      return tween;
    },
  });
}

function block(el: HTMLElement, tl: gsap.core.Timeline, isIntro: boolean) {
  const delay = parseFloat(el.dataset.delay || '0');
  gsap.set(el, { visibility: 'visible' });
  const tween = gsap.fromTo(el, { y: -18, autoAlpha: 0 }, {
    y: 0, autoAlpha: 1, duration: 0.5, ease: ease.pull, delay, paused: isIntro,
    scrollTrigger: isIntro ? undefined : { trigger: el, start: 'top 92%', once: true },
  });
  if (isIntro) tl.add(() => { tween.play(); }, 0);
}

// The black layer over the image is CSS (::after); the class lifts it. The image itself never moves.
// Like Moonbow's own cover, the black stays until the picture has actually arrived.
function develop(el: HTMLElement) {
  // The visible one: a screenshot may carry a phone crop next to the full window.
  const img = [...el.querySelectorAll('img')].find((i) => i.checkVisibility?.() ?? i.offsetParent !== null) ?? el.querySelector('img');
  const lift = () => el.classList.add('is-in');
  ScrollTrigger.create({ trigger: el, start: 'top bottom+=60%', once: true, onEnter: () => { if (img) img.loading = 'eager'; } });
  ScrollTrigger.create({ trigger: el, start: 'top 92%', once: true, onEnter: () => {
    if (!img || (img.complete && img.naturalWidth > 0)) lift();
    else { img.addEventListener('load', lift, { once: true }); img.addEventListener('error', lift, { once: true }); }
  } });
}

export function initReveals(root: ParentNode = document) {
  const intro = gsap.timeline({ paused: true });
  if (prefersReducedMotion()) return intro;
  root.querySelectorAll<HTMLElement>('[data-split]').forEach((el) => splitLines(el, intro, el.hasAttribute('data-intro')));
  root.querySelectorAll<HTMLElement>('[data-reveal="down"]').forEach((el) => block(el, intro, el.hasAttribute('data-intro')));
  root.querySelectorAll<HTMLElement>('[data-reveal="develop"]:not([data-intro])').forEach(develop);
  return intro;
}
