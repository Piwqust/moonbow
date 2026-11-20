// Header: solid once the page scrolls; steps aside while reading down, returns on the way up.
// The phone menu: a full-screen list, focus kept inside, Escape closes, focus goes back to the button.
import { ScrollTrigger } from 'gsap/ScrollTrigger';
import { onPage } from '../engine/core';

export const header = () => onPage(() => {
  const html = document.documentElement;
  let last = scrollY;
  ScrollTrigger.create({
    start: 0, end: 'max',
    onUpdate: () => {
      const y = scrollY;
      html.classList.toggle('hdr-solid', y > 8);
      if (Math.abs(y - last) < 6) return;
      html.classList.toggle('hdr-away', y > 240 && y > last && !html.classList.contains('menu-open'));
      last = y;
    },
  });
  html.classList.toggle('hdr-solid', scrollY > 8);
  // Keyboard users must always see where focus is: bring the header back when it gets focus.
  document.querySelector('[data-header]')?.addEventListener('focusin', () => html.classList.remove('hdr-away'));

  const menu = document.querySelector<HTMLElement>('[data-menu]');
  const open = document.querySelector<HTMLButtonElement>('[data-menu-open]');
  const close = document.querySelector<HTMLButtonElement>('[data-menu-close]');
  if (!menu || !open) return;
  const focusables = () => [...menu.querySelectorAll<HTMLElement>('a, button'), open];
  const set = (on: boolean) => {
    menu.hidden = !on;
    open.setAttribute('aria-expanded', String(on));
    html.classList.toggle('menu-open', on);
    open.querySelectorAll('.roll > span').forEach((s) => { s.textContent = on ? 'Закрыть' : 'Меню'; });
    if (on) menu.querySelector<HTMLElement>('a')?.focus();
    else open.focus();
  };
  open.addEventListener('click', () => set(menu.hidden === true));
  close?.addEventListener('click', () => set(false));
  document.addEventListener('keydown', (e) => {
    if (menu.hidden) return;
    if (e.key === 'Escape') { e.preventDefault(); set(false); return; }
    if (e.key !== 'Tab') return;
    const f = focusables();
    const i = f.indexOf(document.activeElement as HTMLElement);
    const next = e.shiftKey ? (i <= 0 ? f.length - 1 : i - 1) : (i === f.length - 1 ? 0 : i + 1);
    e.preventDefault();
    f[next].focus();
  });
  matchMedia('(min-width: 801px)').addEventListener('change', (m) => { if (m.matches && !menu.hidden) set(false); });
});
