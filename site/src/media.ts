import type { ImageMetadata } from 'astro';
import desktop from '../../docs/design/agent-pet-desktop.png';

const animations = import.meta.glob<{ default: ImageMetadata }>('./assets/pet/*.webp', { eager: true });
const stills = import.meta.glob<{ default: ImageMetadata }>('./assets/pet/*-still.png', { eager: true });
export const petMedia = Object.fromEntries(Object.entries(animations).map(([path, animation]) => {
  const key = path.split('/').pop()!.replace('.webp', '');
  return [key, { animation: animation.default.src, still: stills[`./assets/pet/${key}-still.png`].default.src }];
}));
export const og = desktop;
