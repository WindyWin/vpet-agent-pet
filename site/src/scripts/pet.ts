import { motion } from './motion';

export type PetMedia = Record<string, { animation: string; still: string }>;

interface Copy { label: string; picked: string; landed: string; patted: string; moving: string; reset: string; }

export interface PetOptions {
  /** Take short, occasional walks while the scene is visible and motion is enabled. */
  wander?: boolean;
  /** Pose played when the pet is tapped or petted with the keyboard. */
  tapPose?: string;
  /** Poses to fetch ahead of time so swapping between them does not flash. */
  preload?: string[];
}

export type PetEvent = 'sync' | 'interrupt' | 'temp-end';

const THRESHOLD = 6;
const STEP = 28;
const SPEED = 80; // px per second while walking
const SPEECH_MAX = 215;
const EDGE = 6;
const preloaded = new Map<string, HTMLImageElement>();

/**
 * One reusable pet. Four things are kept apart so no combination needs its own state:
 *  - pose: base (the section's selected pose), temp (a short reaction), plus
 *    derived `raised` / `walk`;
 *  - position (x, y): written only by this class, on the actor element;
 *  - facing: a flip on the image only;
 *  - speech: base text and a temporary text.
 * Sway and squash run on the nested body so they never fight the position transform.
 */
export class PetActor {
  readonly scene: HTMLElement;
  readonly initial: { pose: string; text: string };
  visible = false;

  private el: HTMLButtonElement;
  private img: HTMLImageElement;
  private speech: HTMLElement;
  private shadow: HTMLElement;
  private live: HTMLElement;
  private media: PetMedia;
  private copy: Copy;
  private opts: PetOptions;

  private base: string;
  private baseText: string;
  private temp: string | null = null;
  private tempActive = false;
  private x = 0;
  private y = 0;
  private walking = false;
  private dragging = false;
  private flipped = false;
  private hopping = false;
  private drag: { id: number; sx: number; sy: number; ox: number; oy: number; moved: boolean } | null = null;
  private token = 0;
  private speechSize = { w: 0, h: 0 };
  private speechText = '';
  private liveFlip = false;
  private preloadedOnce = false;

  private tempTimer = 0;
  private walkTimer = 0;
  private wanderTimer = 0;
  private landTimer = 0;
  private hopTimer = 0;
  private listeners = new Set<(event: PetEvent) => void>();
  private disposers: Array<() => void> = [];

  constructor(scene: HTMLElement, media: PetMedia, opts: PetOptions = {}) {
    this.scene = scene;
    this.media = media;
    this.opts = opts;
    this.copy = JSON.parse(scene.dataset.copy ?? '{}');
    this.el = scene.querySelector<HTMLButtonElement>('[data-pet]')!;
    this.img = scene.querySelector<HTMLImageElement>('[data-pet-image]')!;
    this.speech = scene.querySelector<HTMLElement>('[data-speech]')!;
    this.shadow = scene.querySelector<HTMLElement>('.pet-shadow')!;
    this.live = scene.querySelector<HTMLElement>('[data-pet-live]')!;
    this.base = scene.dataset.initial ?? 'idle';
    this.speechText = this.speech.textContent ?? '';
    this.baseText = this.speechText;
    this.initial = { pose: this.base, text: this.baseText };

    this.listen(this.el, 'pointerdown', this.onDown as EventListener);
    this.listen(this.el, 'pointermove', this.onMove as EventListener);
    this.listen(this.el, 'pointerup', this.onUp as EventListener);
    this.listen(this.el, 'pointercancel', this.onCancel as EventListener);
    this.listen(this.el, 'lostpointercapture', this.onCancel as EventListener);
    this.listen(this.el, 'keydown', this.onKey as EventListener);
    this.listen(this.el, 'click', this.onClick as EventListener);
    this.listen(window, 'blur', () => this.cancelDrag());
    this.listen(document, 'visibilitychange', () => { this.cancelDrag(); this.sync(); });

    const resize = new ResizeObserver(() => this.layout());
    resize.observe(scene);
    this.disposers.push(() => resize.disconnect());

    const visibility = new IntersectionObserver(entries => {
      this.visible = entries[entries.length - 1].isIntersecting;
      scene.classList.toggle('is-offscreen', !this.visible);
      if (!this.visible) this.cancelDrag();
      this.sync();
    }, { rootMargin: '80px' });
    visibility.observe(scene);
    this.disposers.push(() => visibility.disconnect());

    this.disposers.push(motion.subscribe(() => { this.cancelDrag(); this.sync(); }));
    this.layout();
    this.render();
  }

  private listen(target: EventTarget, type: string, handler: EventListener) {
    target.addEventListener(type, handler);
    this.disposers.push(() => target.removeEventListener(type, handler));
  }

  // ----- Public API -------------------------------------------------------

  /** True when media may animate and the pet may move by itself. */
  get canRun() { return motion.enabled && this.visible && !motion.hidden; }

  subscribe(listener: (event: PetEvent) => void) {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }

  /** Persistent pose and speech for the section (for example the selected agent state). */
  setBase(pose: string, text: string) {
    this.clearTemp(false);
    this.base = pose;
    this.baseText = text;
    this.say(text);
    this.render();
  }

  /** A short reaction. It replaces any earlier one and then recovers to the base. */
  react(pose: string | null, ms: number, text?: string, announce = false) {
    this.clearTemp(true);
    this.temp = pose;
    this.tempActive = true;
    if (text) this.say(text, announce);
    this.render();
    this.tempTimer = window.setTimeout(() => this.endTemp(), ms);
  }

  /** Drop any short reaction and return to the base speech. */
  calm() {
    if (!this.tempActive) return;
    this.clearTemp(true);
    this.say(this.baseText);
    this.render();
  }

  /** Walk to a horizontal fraction (0 = left edge, 1 = right edge) of the scene. */
  walkToFraction(fraction: number, onDone?: () => void) {
    const b = this.bounds();
    this.walkTo(b.minX + (b.maxX - b.minX) * fraction, onDone);
  }

  /** Walk somewhere else in the scene. Used by the "take a walk" action. */
  stroll() {
    const b = this.bounds();
    const span = b.maxX - b.minX;
    const options = [b.minX + span * 0.15, b.minX + span * 0.5, b.minX + span * 0.85]
      .filter(target => Math.abs(target - this.x) > span * 0.2);
    this.walkTo(options[Math.floor(Math.random() * options.length)] ?? b.minX + span / 2);
  }

  reset() {
    this.freeze();
    this.clearTemp(true);
    this.place(0, 0, motion.enabled ? 420 : 0);
    this.react(null, 2200, this.copy.reset, true);
  }

  destroy() {
    this.cancelDrag();
    this.stopWalk();
    this.clearTemp(false);
    window.clearTimeout(this.wanderTimer);
    window.clearTimeout(this.landTimer);
    window.clearTimeout(this.hopTimer);
    this.disposers.forEach(dispose => dispose());
    this.disposers = [];
    this.listeners.clear();
  }

  // ----- Pose, media and speech ------------------------------------------

  private pose() {
    if (this.dragging) return 'raised';
    if (this.walking || this.hopping) return 'walk';
    if (this.y < -2) return 'raised';
    return this.temp ?? this.base;
  }

  private render() {
    const pose = this.pose();
    const entry = this.media[pose] ?? this.media.idle;
    const src = this.canRun ? entry.animation : entry.still;
    if (this.img.getAttribute('src') !== src) this.img.src = src;
    this.el.dataset.pose = pose;
    this.el.classList.toggle('is-flipped', this.flipped && pose === 'walk');
  }

  private say(text: string, announce = false) {
    this.speechText = text;
    this.speech.textContent = text;
    this.speechSize = { w: this.speech.offsetWidth, h: this.speech.offsetHeight };
    this.el.classList.remove('is-reacting');
    void this.el.offsetWidth;
    this.el.classList.add('is-reacting');
    this.positionSpeech();
    if (announce) {
      this.liveFlip = !this.liveFlip;
      this.live.textContent = text + (this.liveFlip ? '\u00a0' : '');
    }
  }

  private clearTemp(interrupted: boolean) {
    const active = this.tempActive;
    window.clearTimeout(this.tempTimer);
    this.tempTimer = 0;
    this.tempActive = false;
    this.temp = null;
    if (active && interrupted) this.emit('interrupt');
  }

  private endTemp() {
    this.tempTimer = 0;
    this.tempActive = false;
    this.temp = null;
    this.say(this.baseText);
    this.render();
    this.emit('temp-end');
    this.scheduleWander();
  }

  private emit(event: PetEvent) { this.listeners.forEach(listener => listener(event)); }

  /** Called whenever motion, visibility or the document state changes. */
  private sync() {
    if (!this.canRun) {
      this.stopWalk();
      window.clearTimeout(this.wanderTimer);
      this.wanderTimer = 0;
    } else {
      this.fetchPoses();
      this.scheduleWander();
    }
    this.render();
    this.emit('sync');
  }

  private fetchPoses() {
    if (this.preloadedOnce) return;
    this.preloadedOnce = true;
    const poses = new Set(['raised', 'touch-head', this.base, ...(this.opts.preload ?? [])]);
    if (this.opts.wander) poses.add('walk');
    const load = () => poses.forEach(pose => {
      const src = this.media[pose]?.animation;
      if (!src || preloaded.has(src)) return;
      const image = new Image();
      image.src = src;
      preloaded.set(src, image);
    });
    const idle = (window as Window & { requestIdleCallback?: (cb: () => void, o?: { timeout: number }) => number }).requestIdleCallback;
    if (idle) idle.call(window, load, { timeout: 2500 });
    else setTimeout(load, 600);
  }

  // ----- Position ---------------------------------------------------------

  private bounds() {
    const w = this.scene.clientWidth;
    const slack = this.el.offsetWidth * 0.08; // sprites have transparent margins
    return {
      minX: -this.el.offsetLeft - slack,
      maxX: w - this.el.offsetWidth - this.el.offsetLeft + slack,
      minY: -this.el.offsetTop,
      maxY: 0,
    };
  }

  /** The only writer of the actor transform. A zero duration moves immediately. */
  private place(x: number, y: number, ms = 0, easing = 'cubic-bezier(.34,1.4,.64,1)') {
    const b = this.bounds();
    this.x = Math.min(b.maxX, Math.max(b.minX, x));
    this.y = Math.min(b.maxY, Math.max(b.minY, y));
    const transition = ms > 0 ? `transform ${ms}ms ${easing}, opacity ${ms}ms ${easing}` : 'none';
    this.el.style.transition = transition;
    this.el.style.transform = `translate3d(${this.x}px, ${this.y}px, 0)`;
    const lift = Math.min(1, -this.y / Math.max(1, this.scene.clientHeight * 0.8));
    this.shadow.style.transition = transition;
    this.shadow.style.transform = `translate3d(${this.x}px, 0, 0) scale(${1 - lift * 0.45})`;
    this.shadow.style.opacity = String(1 - lift * 0.5);
    this.positionSpeech();
    this.render();
  }

  /** Read where the actor is right now (mid-transition included) and stop there. */
  private freeze() {
    this.token++;
    window.clearTimeout(this.walkTimer);
    window.clearTimeout(this.landTimer);
    window.clearTimeout(this.hopTimer);
    this.walking = false;
    this.hopping = false;
    this.el.classList.remove('is-landing');
    const matrix = new DOMMatrixReadOnly(getComputedStyle(this.el).transform);
    this.place(matrix.m41, matrix.m42, 0);
  }

  private layout() {
    this.speech.style.maxWidth = `${Math.max(120, Math.min(SPEECH_MAX, this.scene.clientWidth - EDGE * 2))}px`;
    this.speechSize = { w: this.speech.offsetWidth, h: this.speech.offsetHeight };
    const b = this.bounds();
    if (this.x < b.minX || this.x > b.maxX || this.y < b.minY) this.place(this.x, this.y, 0);
    else this.positionSpeech();
  }

  /** The bubble is clamped on its own so it stays legible near scene edges. */
  private positionSpeech() {
    const { w, h } = this.speechSize;
    if (!w) return;
    const width = this.scene.clientWidth;
    const centre = this.el.offsetLeft + this.el.offsetWidth / 2 + this.x;
    const left = centre - w / 2;
    let shiftX = 0;
    if (left < EDGE) shiftX = EDGE - left;
    else if (left + w > width - EDGE) shiftX = width - EDGE - (left + w);
    const top = this.el.offsetTop + this.y - 5 - h;
    const shiftY = top < EDGE ? EDGE - top : 0;
    this.speech.style.translate = `${shiftX}px ${shiftY}px`;
  }

  // ----- Walking ----------------------------------------------------------

  private walkTo(target: number, onDone?: () => void) {
    this.freeze();
    const b = this.bounds();
    target = Math.min(b.maxX, Math.max(b.minX, target));
    const dx = target - this.x;
    if (Math.abs(dx) < 4) { onDone?.(); return; }
    if (!this.canRun) {
      // Paused, hidden or offscreen: positioning still works, but it is immediate.
      this.place(target, this.y, 0);
      onDone?.();
      return;
    }
    const token = this.token;
    this.flipped = dx < 0;
    this.walking = true;
    const ms = Math.min(4200, Math.max(500, Math.abs(dx) / SPEED * 1000));
    this.place(target, this.y, ms, 'ease-in-out');
    this.walkTimer = window.setTimeout(() => {
      if (token !== this.token) return;
      this.walking = false;
      this.render();
      this.scheduleWander();
      onDone?.();
    }, ms);
  }

  /** End a walk in place at its destination instead of freezing mid-stride. */
  private stopWalk() {
    if (!this.walking) return;
    this.token++;
    window.clearTimeout(this.walkTimer);
    this.walking = false;
    this.place(this.x, this.y, 0);
  }

  private scheduleWander() {
    window.clearTimeout(this.wanderTimer);
    this.wanderTimer = 0;
    if (!this.opts.wander || !this.canRun) return;
    this.wanderTimer = window.setTimeout(() => {
      this.wanderTimer = 0;
      if (!this.canRun) return;
      if (this.tempActive || this.dragging || this.walking || this.drag || this.y < -2 || this.el.matches(':focus-visible')) {
        this.scheduleWander();
        return;
      }
      const b = this.bounds();
      const reach = Math.min(190, (b.maxX - b.minX) * 0.3);
      let target = this.x + (Math.random() < 0.5 ? -1 : 1) * (60 + Math.random() * reach);
      if (target < b.minX || target > b.maxX) target = this.x - (target - this.x);
      this.walkTo(target);
    }, 4500 + Math.random() * 5000);
  }

  // ----- Pointer: drag, tap, cancel ---------------------------------------

  private onDown = (event: PointerEvent) => {
    if (this.drag || (event.pointerType === 'mouse' && event.button !== 0)) return;
    this.freeze();
    this.clearTemp(true);
    this.scene.classList.add('is-touched');
    this.drag = { id: event.pointerId, sx: event.clientX, sy: event.clientY, ox: this.x, oy: this.y, moved: false };
    try { this.el.setPointerCapture(event.pointerId); } catch { /* the pointer may already be gone */ }
  };

  private onMove = (event: PointerEvent) => {
    const drag = this.drag;
    if (!drag || event.pointerId !== drag.id) return;
    const dx = event.clientX - drag.sx;
    const dy = event.clientY - drag.sy;
    if (!drag.moved) {
      if (Math.hypot(dx, dy) < THRESHOLD) return;
      drag.moved = true;
      this.dragging = true;
      this.el.classList.add('is-dragging');
      this.say(this.copy.picked, true);
    }
    this.place(drag.ox + dx, drag.oy + dy, 0);
  };

  private onUp = (event: PointerEvent) => {
    const drag = this.drag;
    if (!drag || event.pointerId !== drag.id) return;
    this.endDrag(drag.moved ? 'land' : 'tap');
  };

  private onCancel = (event: PointerEvent) => {
    if (this.drag && event.pointerId === this.drag.id) this.endDrag('cancel');
  };

  private cancelDrag() { if (this.drag) this.endDrag('cancel'); }

  private endDrag(outcome: 'land' | 'tap' | 'cancel') {
    const drag = this.drag;
    if (!drag) return;
    this.drag = null;
    this.dragging = false;
    this.el.classList.remove('is-dragging');
    try { if (this.el.hasPointerCapture(drag.id)) this.el.releasePointerCapture(drag.id); } catch { /* already released */ }
    if (outcome === 'tap') { this.pet(); return; }
    if (!drag.moved) {
      if (this.speechText !== this.baseText) this.say(this.baseText);
      return;
    }
    // Settle toward the local baseline and play a brief landing reaction.
    const animate = this.canRun;
    this.place(this.x, 0, animate ? 380 : 0);
    if (outcome === 'land') {
      if (animate) {
        this.el.classList.add('is-landing');
        window.clearTimeout(this.landTimer);
        this.landTimer = window.setTimeout(() => this.el.classList.remove('is-landing'), 480);
      }
      this.react(null, 2400, this.copy.landed, true);
    } else {
      this.say(this.baseText);
    }
    this.render();
    this.scheduleWander();
  }

  private pet() {
    this.react(this.opts.tapPose ?? 'touch-head', 2600, this.copy.patted, true);
  }

  // ----- Keyboard ---------------------------------------------------------

  private onClick = (event: MouseEvent) => {
    // A pointer tap is handled on pointerup; detail 0 means Enter or Space.
    if (event.detail === 0) this.pet();
  };

  private onKey = (event: KeyboardEvent) => {
    if (event.altKey || event.ctrlKey || event.metaKey) return;
    const moves: Record<string, [number, number]> = {
      ArrowLeft: [-STEP, 0], ArrowRight: [STEP, 0], ArrowUp: [0, -STEP], ArrowDown: [0, STEP],
    };
    if (event.key === 'Escape') { event.preventDefault(); this.reset(); return; }
    const move = moves[event.key];
    if (!move) return;
    event.preventDefault();
    // Successive steps build on the logical position; only a walk in flight is read back.
    if (this.walking) this.freeze();
    if (move[0]) {
      this.flipped = move[0] < 0;
      this.hopping = true;
      window.clearTimeout(this.hopTimer);
      this.hopTimer = window.setTimeout(() => { this.hopping = false; this.render(); }, 420);
    }
    this.place(this.x + move[0], this.y + move[1], this.canRun ? 180 : 0, 'ease-out');
    this.react(null, 1800, this.copy.moving);
  };
}
