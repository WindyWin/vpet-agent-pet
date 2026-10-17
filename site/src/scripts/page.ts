/** Reveal choreography. Content is visible unless motion is on, and only reveals once. */
export function initReveal() {
  const targets = [...document.querySelectorAll<HTMLElement>('[data-reveal]')];
  const reveal = (el: Element) => el.classList.add('is-in');
  if (!('IntersectionObserver' in window)) { targets.forEach(reveal); return; }
  const io = new IntersectionObserver(entries => {
    for (const entry of entries) {
      if (!entry.isIntersecting) continue;
      reveal(entry.target);
      io.unobserve(entry.target);
    }
  }, { threshold: 0, rootMargin: '0px 0px -10% 0px' });
  targets.forEach(el => io.observe(el));
  // Suspend decorative CSS motion in sections that are not on screen.
  const sections = [...document.querySelectorAll<HTMLElement>('.section, .footer')];
  const watch = new IntersectionObserver(entries => {
    for (const entry of entries) entry.target.classList.toggle('is-offscreen', !entry.isIntersecting);
  }, { rootMargin: '120px' });
  sections.forEach(section => watch.observe(section));
}

const SECTIONS = ['states', 'play', 'care', 'privacy', 'install'];

/** The section under the top of the viewport, or '' above the first one. */
export function currentSection() {
  const line = window.innerHeight * 0.4;
  let current = '';
  for (const id of SECTIONS) {
    const el = document.getElementById(id);
    if (el && el.getBoundingClientRect().top <= line) current = id;
  }
  return current;
}

/** Scroll-spy for the section links, and the language switch that keeps your place. */
export function initNavigation() {
  const links = [...document.querySelectorAll<HTMLAnchorElement>('.nav-links a')];
  let frame = 0;
  const mark = () => {
    frame = 0;
    const current = currentSection();
    links.forEach(link => {
      const on = link.hash === `#${current}`;
      link.classList.toggle('active', on);
      if (on) link.setAttribute('aria-current', 'location'); else link.removeAttribute('aria-current');
    });
  };
  window.addEventListener('scroll', () => { if (!frame) frame = requestAnimationFrame(mark); }, { passive: true });
  mark();

  const switches = [...document.querySelectorAll<HTMLAnchorElement>('[data-locale-link]')];
  switches.forEach(link => {
    const target = link.getAttribute('href')!;
    const update = () => {
      const section = currentSection();
      link.setAttribute('href', section ? `${target}#${section}` : target);
    };
    ['pointerdown', 'focus', 'contextmenu', 'touchstart'].forEach(type => link.addEventListener(type, update, { passive: true }));
    link.addEventListener('click', () => {
      update();
      try { localStorage.setItem('agent-pet-locale', link.lang); } catch { /* not remembered */ }
    });
  });
}
