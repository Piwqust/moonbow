// Time helpers shared by the server render and the client driver (no DOM access).

/** Player timecode H:MM:SS.mmm, as Moonbow's statistics overlay counts it. */
export function fmt(t: number) {
  const ms = Math.round(t * 1000);
  const h = Math.floor(ms / 3600000);
  const m = Math.floor(ms / 60000) % 60;
  const s = Math.floor(ms / 1000) % 60;
  const p = (n: number, w = 2) => String(n).padStart(w, '0');
  return `${h}:${p(m)}:${p(s)}.${p(ms % 1000, 3)}`;
}

// The ruler spans 10 ms to ~11 days on a log10 axis.
export const LOG_MIN = -2;
export const LOG_MAX = 6;

export const ticks = [
  { t: 0.01, label: '10 мс' },
  { t: 0.1, label: '0,1 с', minor: true },
  { t: 1, label: '1 с' },
  { t: 10, label: '10 с', minor: true },
  { t: 60, label: '1 мин' },
  { t: 600, label: '10 мин', minor: true },
  { t: 3600, label: '1 ч' },
  { t: 86400, label: '1 д', minor: true },
  { t: 604800, label: '1 нед' },
];

/** Log-interpolate between two moments: equal scroll covers equal ratios of time. */
export const logLerp = (a: number, b: number, k: number) => Math.exp(Math.log(a) + (Math.log(b) - Math.log(a)) * k);
