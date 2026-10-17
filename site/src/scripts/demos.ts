import { motion } from './motion';
import type { PetActor } from './pet';

const all = <T extends Element>(root: ParentNode, selector: string) => [...root.querySelectorAll<T>(selector)];

/** Agent states: pose, bubble, selected control and terminal line change together. */
export function initStates(root: HTMLElement, actor: PetActor) {
  const choices = all<HTMLButtonElement>(root, '[data-state]');
  const log = root.querySelector('[data-demo-log]');
  const select = (choice: HTMLButtonElement) => {
    choices.forEach(other => {
      const on = other === choice;
      other.classList.toggle('selected', on);
      other.setAttribute('aria-pressed', String(on));
    });
    actor.setBase(choice.dataset.state!, choice.dataset.bubble!);
    if (log) log.textContent = choice.dataset.log ?? '';
  };
  choices.forEach(choice => choice.addEventListener('click', () => select(choice)));
}

/** Playground actions. A new action cancels the previous one; none leaves a stale callback. */
export function initPlay(root: HTMLElement, actor: PetActor) {
  const buttons = all<HTMLButtonElement>(root, '[data-pet-action]');
  const durations: Record<string, number> = { 'touch-head': 2600, dance: 4600, 'snack-hungry': 4600, 'fidget-yawn': 4800, walk: 2800 };
  let selection = 0;
  const clear = () => {
    window.clearTimeout(selection);
    selection = 0;
    buttons.forEach(button => button.classList.remove('selected'));
  };
  actor.subscribe(event => { if (event === 'interrupt' || event === 'temp-end') clear(); });
  buttons.forEach(button => button.addEventListener('click', () => {
    const key = button.dataset.petAction!;
    const ms = durations[key] ?? 3000;
    if (key === 'walk') {
      actor.react(null, ms, button.dataset.bubble, true);
      actor.stroll();
    } else {
      actor.react(key, ms, button.dataset.bubble, true);
    }
    clear();
    button.classList.add('selected');
    selection = window.setTimeout(clear, ms);
  }));
  root.querySelector('[data-reset]')?.addEventListener('click', () => { clear(); actor.reset(); });
}

/**
 * Break previews. One interval at most; it runs only while the scene is visible,
 * motion is enabled and the page is shown, and resuming never doubles it.
 */
export function initCare(root: HTMLElement, actor: PetActor) {
  const buttons = all<HTMLButtonElement>(root, '[data-break]');
  const count = root.querySelector<HTMLElement>('[data-break-count]')!;
  const label = root.querySelector<HTMLElement>('[data-break-label]')!;
  const live = root.querySelector<HTMLElement>('[data-break-live]')!;
  const done = root.dataset.done ?? '';
  let active: HTMLButtonElement | null = null;
  let remaining = 0;
  let interval = 0;
  let last = 0;

  const shouldRun = () => active !== null && actor.canRun;
  const show = () => { count.textContent = active ? String(Math.max(0, Math.ceil(remaining / 1000))) : '♡'; };
  const account = () => {
    const now = performance.now();
    remaining -= now - last;
    last = now;
  };
  const stopInterval = () => {
    if (!interval) return;
    account();
    window.clearInterval(interval);
    interval = 0;
  };
  const sync = () => {
    if (shouldRun() && !interval) {
      last = performance.now();
      interval = window.setInterval(tick, 250);
    } else if (!shouldRun()) {
      stopInterval();
    }
    if (active) label.textContent = motion.enabled ? label.dataset.active! : label.dataset.paused!;
    show();
  };
  const reset = (message: string) => {
    stopInterval();
    buttons.forEach(button => button.setAttribute('aria-pressed', 'false'));
    active = null;
    remaining = 0;
    label.textContent = message;
    show();
  };
  const tick = () => {
    account();
    show();
    if (remaining > 0) return;
    reset(done);
    live.textContent = done;
    actor.setBase(actor.initial.pose, actor.initial.text);
    actor.react(null, 3800, done);
  };

  buttons.forEach(button => button.addEventListener('click', () => {
    if (active === button) {
      // Choosing the running preview again cancels it.
      reset(label.dataset.ready!);
      live.textContent = '';
      actor.setBase(actor.initial.pose, actor.initial.text);
      return;
    }
    reset(label.dataset.ready!);
    active = button;
    remaining = Number(button.dataset.duration) * 1000;
    button.setAttribute('aria-pressed', 'true');
    actor.setBase(button.dataset.break!, button.dataset.bubble!);
    live.textContent = `${button.dataset.duration} ${label.dataset.active}`;
    sync();
  }));
  actor.subscribe(event => { if (event === 'sync') sync(); });
}

/** Platform choice: requirements, download action and the pet follow the selection. */
export function initInstall(root: HTMLElement, actor: PetActor) {
  const tabs = all<HTMLButtonElement>(root, '[data-platform]');
  const panels = all<HTMLElement>(root, '[data-platform-panel]');
  const wave = root.dataset.wave ?? '';
  const spots = [0.2, 0.5, 0.8];
  const choose = (index: number, walk: boolean) => {
    tabs.forEach((tab, i) => {
      tab.classList.toggle('selected', i === index);
      tab.setAttribute('aria-pressed', String(i === index));
    });
    panels.forEach((panel, i) => { panel.hidden = i !== index; });
    if (!walk) return;
    actor.calm();
    actor.walkToFraction(spots[index], () => actor.react('turn-finished', 3000, wave));
  };
  tabs.forEach((tab, index) => tab.addEventListener('click', () => choose(index, true)));
  // Start on the visitor's own platform.
  const agent = navigator.userAgent;
  const own = /Mac/i.test(agent) && !/iPhone|iPad/i.test(agent) ? 1 : /Win/i.test(agent) ? 2 : 0;
  choose(own, false);
  if (own !== 0) {
    // Place the pet at the chosen platform without a walk.
    actor.walkToFraction(spots[own]);
  }
}
