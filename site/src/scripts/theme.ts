// Colour theme: the visitor's choice wins, otherwise follow the system.
// Base.astro applies the same logic before first paint so there is no flash.
const KEY = 'agent-pet-theme';
const root = document.documentElement;
const system = window.matchMedia('(prefers-color-scheme: dark)');
const COLORS = { light: '#F7F5FC', dark: '#151220' };

type Theme = 'light' | 'dark';
const read = (): Theme | null => {
  try {
    const value = localStorage.getItem(KEY);
    return value === 'light' || value === 'dark' ? value : null;
  } catch { return null; }
};

let saved = read();
const current = (): Theme => saved ?? (system.matches ? 'dark' : 'light');

function apply() {
  const theme = current();
  root.dataset.theme = theme;
  document.querySelector('[data-theme-color]')?.setAttribute('content', COLORS[theme]);
  document.querySelectorAll<HTMLButtonElement>('[data-theme-toggle]').forEach(button => {
    const dark = theme === 'dark';
    button.setAttribute('aria-pressed', String(dark));
    const icon = button.querySelector('[data-theme-icon]');
    const label = button.querySelector('[data-theme-label]');
    if (icon) icon.textContent = dark ? '☀' : '☾';
    if (label) label.textContent = dark ? button.dataset.toLight! : button.dataset.toDark!;
  });
}

/** Wire the nav light/dark button. */
export function bindThemeToggle() {
  document.querySelectorAll<HTMLButtonElement>('[data-theme-toggle]').forEach(button => {
    button.addEventListener('click', () => {
      saved = current() === 'dark' ? 'light' : 'dark';
      try { localStorage.setItem(KEY, saved); } catch { /* storage unavailable: keep it for this visit */ }
      apply();
    });
  });
  system.addEventListener('change', () => { if (!saved) apply(); });
  apply();
}
