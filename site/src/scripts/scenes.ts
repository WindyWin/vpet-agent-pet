import { bindMotionToggle } from './motion';
import { PetActor, type PetMedia, type PetOptions } from './pet';
import { initCare, initInstall, initPlay, initStates } from './demos';
import { initNavigation, initReveal } from './page';

const media: PetMedia = JSON.parse(document.getElementById('pet-media')?.textContent ?? '{}');

const options = (scene: HTMLElement): PetOptions => {
  const has = (name: string) => scene.classList.contains(name);
  if (has('hero-scene')) return { wander: true };
  if (has('play-scene')) return { wander: true, preload: ['walk', 'dance', 'snack-hungry', 'fidget-yawn'] };
  if (has('states-scene')) return { preload: Object.keys(media).filter(key => !['walk', 'raised', 'idle'].includes(key)) };
  if (has('care-scene')) return { preload: ['break-buddy', 'idle', 'water'] };
  if (has('install-scene')) return { preload: ['walk', 'turn-finished'] };
  if (has('footer-scene')) return { tapPose: 'startled', preload: ['startled'] };
  return {};
};

const actors = [...document.querySelectorAll<HTMLElement>('[data-pet-scene]')]
  .map(scene => new PetActor(scene, media, options(scene)));
const actorIn = (root: Element) => actors.find(actor => root.contains(actor.scene))!;

const demo = (selector: string, start: (root: HTMLElement, actor: PetActor) => void) => {
  const root = document.querySelector<HTMLElement>(selector);
  if (root) start(root, actorIn(root));
};
demo('[data-state-demo]', initStates);
demo('[data-play-demo]', initPlay);
demo('[data-care-demo]', initCare);
demo('[data-install-demo]', initInstall);

bindMotionToggle();
initReveal();
initNavigation();
window.addEventListener('pagehide', () => actors.forEach(actor => actor.destroy()));
