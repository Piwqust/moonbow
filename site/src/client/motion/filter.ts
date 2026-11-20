// Search fields that filter long lists in place (all settings, all keys), like the search in
// Moonbow's own settings: words in any order, case and ё/е don't matter.
//   <input data-filter="#list">  items: [data-item] with [data-text]; groups: [data-group] hide when empty
//   [data-filter-count] shows how many are visible; [data-filter-empty] shows when none
import { onPage } from '../engine/core';

const norm = (s: string) => s.toLowerCase().replace(/ё/g, 'е');

export const filters = () => onPage(() => {
  document.querySelectorAll<HTMLInputElement>('input[data-filter]').forEach((input) => {
    const list = document.querySelector<HTMLElement>(input.dataset.filter!);
    if (!list) return;
    // An item can stand for several entries (data-count), e.g. one row for ten similar commands.
    const items = [...list.querySelectorAll<HTMLElement>('[data-item]')].map((el) => ({ el, text: norm(el.dataset.text || el.textContent || ''), n: Number(el.dataset.count || 1) }));
    const groups = [...list.querySelectorAll<HTMLElement>('[data-group]')];
    const count = document.querySelector<HTMLElement>(`[data-filter-count="${input.id}"]`);
    const empty = document.querySelector<HTMLElement>(`[data-filter-empty="${input.id}"]`);
    const total = items.reduce((a, i) => a + i.n, 0);
    const run = () => {
      const words = norm(input.value).split(/\s+/).filter(Boolean);
      let shown = 0;
      for (const { el, text, n } of items) {
        const ok = words.every((w) => text.includes(w));
        el.hidden = !ok;
        if (ok) shown += n;
      }
      for (const g of groups) {
        g.hidden = !g.querySelector('[data-item]:not([hidden])');
        // Collapsed sections open while a search finds something in them, and close again after.
        if (g instanceof HTMLDetailsElement) {
          if (words.length) { if (!g.dataset.wasOpen) g.dataset.wasOpen = String(g.open); g.open = !g.hidden; }
          else if (g.dataset.wasOpen) { g.open = g.dataset.wasOpen === 'true'; delete g.dataset.wasOpen; }
        }
      }
      if (count) count.textContent = words.length ? `${shown} из ${total}` : String(total);
      if (empty) empty.hidden = shown > 0;
    };
    input.addEventListener('input', run);
    input.addEventListener('keydown', (e) => { if (e.key === 'Escape' && input.value) { input.value = ''; run(); } });
    run();
  });
});
