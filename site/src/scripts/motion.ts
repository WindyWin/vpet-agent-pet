// The one source of truth for "may things move right now?".
// Pet media, autonomous walking, break timers and CSS all follow it.
const KEY = 'agent-pet-motion';
const root = document.documentElement;
const reduce = window.matchMedia('(prefers-reduced-motion: reduce)');
const listeners = new Set<() => void>();

type Saved = 'paused' | 'running' | null;
const read = (): Saved => {
  try {
    const value = localStorage.getItem(KEY);
    return value === 'paused' || value === 'running' ? value : null;
  } catch { return null; }
};

let saved = read();

export const motion = {
  /** False when the visitor paused motion or prefers reduced motion (unless they opted back in). */
  get enabled() { return saved ? saved === 'running' : !reduce.matches; },
  get hidden() { return document.hidden; },
  set(running: boolean) {
    saved = running ? 'running' : 'paused';
    try { localStorage.setItem(KEY, saved); } catch { /* storage unavailable: keep it for this visit */ }
    apply();
  },
  subscribe(listener: () => void) {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
  },
};

function apply() {
  root.dataset.motion = motion.enabled ? 'running' : 'paused';
  document.body.classList.toggle('is-hidden', document.hidden);
  listeners.forEach(listener => listener());
}

reduce.addEventListener('change', apply);
document.addEventListener('visibilitychange', apply);

/** Wire the nav pause/resume button. */
export function bindMotionToggle() {
  const button = document.querySelector<HTMLButtonElement>('[data-motion-toggle]');
  if (!button) return;
  const icon = button.querySelector('[data-motion-icon]');
  const label = button.querySelector('[data-motion-label]');
  const sync = () => {
    const paused = !motion.enabled;
    button.setAttribute('aria-pressed', String(paused));
    if (icon) icon.textContent = paused ? '▶' : 'Ⅱ';
    if (label) label.textContent = paused ? button.dataset.resume! : button.dataset.pause!;
  };
  button.addEventListener('click', () => motion.set(!motion.enabled));
  motion.subscribe(sync);
  apply();
}
