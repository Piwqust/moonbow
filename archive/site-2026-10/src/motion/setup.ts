// Global motion, once on the client: the header steps aside on the way down, returns on the way up.
import { onScroll } from '../engine';

export function setupMotion() {
  let last = 0;
  onScroll(({ y }) => {
    if (Math.abs(y - last) < 4) return;
    document.documentElement.classList.toggle('frame-away', y > 160 && y > last);
    last = y;
  });
}
