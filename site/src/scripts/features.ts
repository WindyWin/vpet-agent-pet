import type { PetActor } from './pet';

/** Local illustrations only: these controls never connect to an agent. */
export function initFeatures(root: HTMLElement, actor: PetActor) {
  const buttons = [...root.querySelectorAll<HTMLButtonElement>('[data-feature-choice]')];
  const desktop = root.querySelector<HTMLElement>('[data-feature-desktop]')!;
  const chrome = desktop.querySelector('.desktop-chrome > span:nth-child(2)')!;
  const poses = ['reading', 'needs-input', 'thinking', 'dance'];
  buttons.forEach((button, index) => button.addEventListener('click', () => {
    buttons.forEach(other => other.setAttribute('aria-pressed', String(other === button)));
    root.querySelectorAll<HTMLElement>('[data-feature-panel]').forEach(panel => { panel.hidden = panel.dataset.featurePanel !== String(index); });
    root.querySelectorAll<HTMLElement>('[data-feature-story]').forEach(story => { story.hidden = story.dataset.featureStory !== String(index); });
    desktop.dataset.mode = String(index);
    chrome.textContent = `agent-pet / ${button.lastChild?.textContent ?? ''}`;
    actor.setBase(poses[index], button.textContent?.trim() ?? '');
    if (index === 0) root.querySelector<HTMLButtonElement>('[data-feature-command][aria-pressed=true]')?.click();
    if (index === 1 && !reply.hidden) actor.setBase('turn-finished', reply.textContent ?? '');
    if (index === 2) root.querySelector<HTMLButtonElement>('[data-feature-agent][aria-pressed=true]')?.click();
  }));
  const commands = [...root.querySelectorAll<HTMLButtonElement>('[data-feature-command]')];
  commands.forEach(button => button.addEventListener('click', () => {
    commands.forEach(other => other.setAttribute('aria-pressed', String(other === button)));
    root.querySelector('[data-command-result]')!.textContent = button.dataset.message!;
    actor.setBase(button.dataset.pose!, button.dataset.message!);
  }));
  const answers = [...root.querySelectorAll<HTMLButtonElement>('[data-feature-answer]')];
  const reply = root.querySelector<HTMLElement>('[data-feature-reply]')!;
  const reset = root.querySelector<HTMLButtonElement>('[data-chat-reset]')!;
  answers.forEach(button => button.addEventListener('click', () => {
    reply.hidden = false;
    reply.textContent = button.dataset.message!;
    answers.forEach(answer => { answer.disabled = true; });
    reset.hidden = false;
    actor.setBase('turn-finished', button.dataset.message!);
    reset.focus({ preventScroll: true });
  }));
  reset.addEventListener('click', () => {
    reply.hidden = true;
    reply.textContent = '';
    reset.hidden = true;
    answers.forEach(answer => { answer.disabled = false; });
    actor.setBase('needs-input', root.querySelector('.chat-question p')!.textContent!);
    answers[0].focus({ preventScroll: true });
  });
  const agents = [...root.querySelectorAll<HTMLButtonElement>('[data-feature-agent]')];
  agents.forEach(button => button.addEventListener('click', () => {
    agents.forEach(other => other.setAttribute('aria-pressed', String(other === button)));
    actor.setBase('thinking', `${button.dataset.featureAgent}: ${button.dataset.message}`);
  }));
}
