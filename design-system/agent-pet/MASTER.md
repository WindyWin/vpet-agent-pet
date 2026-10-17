# Agent Pet — Design System Master

> **LOGIC:** When building a page, first check `design-system/agent-pet/pages/[page].md`.
> If it exists its rules **override** this file. If not, follow the rules below.

**Project:** Agent Pet
**Scope:** `site/` — the public landing page (static Astro, GitHub Pages, English and Vietnamese)
**Direction:** a playful desktop companion, with a different little scene in every section
**Revised:** 2026-10-17. This replaces the earlier dark amber "cozy terminal" system. The
plan that drove the change is `docs/design/site-redesign-plan.md`.

What carried over from the old system: accurate product claims, artwork credits and
terms, readable content, real app artwork. What did not: the brown/amber palette, the
dark-only page, and the limit of one entrance and one reveal.

---

## Concept

The page is a small, living desktop world. The pet is the main visual, not a decoration
inside a feature card. In every main section it moves, reacts and can be picked up and
dragged within its scene. A light, soft base (cloud, lilac, peach, pink) surrounds dark
terminal panels that carry literal developer content.

Two worlds still meet and both stay real:

- **The developer's world** — terminals, commands, agent statuses. Monospace, dark,
  literal. Never decoration.
- **The pet's world** — rounded shapes, plants, shelves, rugs, stickers, speech bubbles,
  and real frames from the app.

## Palette

Tokens live in `site/src/styles/global.css` (`:root`).

| Role | Hex | Variable |
|------|-----|----------|
| Page background | `#F7F5FC` | `--bg` |
| Main text | `#282238` | `--ink` |
| Secondary text | `#655C76` | `--muted` |
| Primary action | `#7050D5` | `--violet` |
| Primary hover, eyebrows | `#5B39BB` | `--violet-dark` |
| Supporting surface | `#EAE3FA` | `--lilac` |
| Warm accent | `#FFD5BD` | `--peach` |
| Playful accent | `#F3C5DF` | `--pink` |
| Terminal | `#211C30` | `--terminal` |

Each section may tint its own scene (peach for the break corner, sage for privacy, lilac
for the stage and dock). Measured contrast (WCAG): muted text is 5.0–5.8:1 on bg, lilac
and the scene tints; ink on bg 14.1:1; white on `--violet` 5.5:1; `--violet-dark` on bg
7.1:1. Check any new pair, including buttons and selected states, before shipping it.

## Typography

| Role | Family | Used for |
|------|--------|----------|
| Display | **Nunito** 500–900 | Headings, wordmark, pet speech |
| Body / UI | **Nunito Sans** 400–800 | Paragraphs, labels, buttons |
| Mono | **JetBrains Mono** | Terminal lines and commands only |

All three ship Vietnamese glyphs; the Google Fonts request serves the `vietnamese`
subset automatically. `html[lang=vi]` raises heading line-height to 1.24 and loosens
tracking so stacked diacritics are not clipped. Body text is never below 12px (`.75rem`).
Headings use `white-space: pre-line`, so a translated title controls its own line breaks.

## Composition

- Large headings, compact readable copy, layered windows, plants, shelves, rugs, stickers.
- Vary the layout per section: split hero, selectable state stage, wide playground,
  arched care scene, local-computer privacy panel, download dock.
- Secondary decoration may rotate and overlap slightly; text and controls stay level
  and readable.
- Below 900px the hero becomes one column and the section links move to a second nav row
  (nothing is hidden); the "Download" link joins that row below 700px.

## Scenes and the pet

One reusable controller (`site/src/scripts/pet.ts`) is configured per scene. Keep these
separate so no combination needs its own state: **pose** (base, temporary reaction,
derived raised/walk), **position**, **facing** (a flip on the image only) and **speech**.

| Section | Pet does | Visitor can |
|---------|----------|-------------|
| Hero | Idle, occasional short walk | Drag, tap, jump to download |
| Agent states | Eight poses | Choose a state: pose, bubble, selected control and terminal line change together |
| Playground | Walk, pat, dance, snack, yawn | Drag, action buttons, reset, keyboard |
| Care | Stretch, rest, drink | Run a labelled break preview |
| Privacy | Reads | Pet it, drag it |
| Install | Walks to the chosen platform, then waves | Choose Linux, macOS or Windows |
| Footer | Sleeps; wakes when tapped | Tap, return to top |

Rules:

- Pointer Events for mouse, pen and touch. `touch-action: none` only on the pet.
- Dragging is never required: every action has a button or key. Arrow keys move, Enter or
  Space pets, Escape returns home; focus is visible.
- The controller is the only writer of the actor's `transform`. Sway and squash run on the
  nested `.pet-body`; entrances animate individual properties (`scale`, `translate`).
- Short reactions never overwrite the selected pose; a stale timer is cancelled whenever a
  new action starts. `pointercancel`, lost capture, blur and a hidden page all end a drag.
- Speech bubbles are clamped to their scene separately from the pet.

## Motion

Three layers: **ambient** (idling, occasional short walks, drifting clouds), **interactive**
(pickup, sway, landing, pats, state changes, platform walk) and **entrance** (specific to
each scene, in `global.css` under `html[data-motion=running]`). Interaction comes first; keep
ambient motion quiet. No scroll hijacking.

`site/src/scripts/motion.ts` is the single source of truth for whether things may move:

- The nav toggle pauses and resumes everything. The choice is stored in `localStorage`
  (`agent-pet-motion`), tolerating missing storage. Without a saved choice,
  `prefers-reduced-motion` decides.
- Paused means: stills instead of animated WebP (CSS cannot stop an animated image), no
  autonomous walking, no CSS animation, no break countdown. The page stays interactive;
  positioning is immediate.
- Scenes off screen and a hidden document suspend media, wandering and timers.
- Content must be visible without JavaScript and with motion paused: hidden-until-revealed
  styles apply only under `html.js[data-motion=running]`.

## Break previews

Break demos are website previews, labelled as such. They run 10 seconds (movement),
20 seconds (eyes) and 8 seconds (water). The desktop app's own behaviour is different and
must not be implied: eye break after 20 minutes of activity, water after 60, and a
60-second movement break after an hour. The eye and water intervals are configurable in the app.

## Languages

English and Vietnamese share one component tree with typed dictionaries in
`site/src/i18n/index.ts`. Routes: `/en/`, `/vi/`, and `/` as an English entry page that
redirects only when the visitor previously chose Vietnamese. The language switch keeps the
current section hash. Every visible and accessible string is translated; product names,
commands, filenames and package names are not. Write Vietnamese as natural product copy.
Use real HTML for demo UI, not screenshots with English text. All URLs go through the
`base` helper (`/vpet-agent-pet`).

## Content rules

- Sentence case. Plain verbs. Say what a thing does, not how good it is.
- Keep the privacy claim exact: the pet learns *what kind* of thing is happening, never
  prompts, code, file contents or commands (only a yes/no flag for risky commands).
- Download buttons describe their destination: the current ones open the latest GitHub
  release page, not an OS-specific file.
- Requirements and compatibility notes must match `starter/docs/install.md` and the
  release workflow.
- Agent activity on the page is an example; say so.
- Artwork credit and the separate artwork terms stay in the footer.

## Accessibility

- AA contrast for all text; visible `:focus-visible` outline on every control.
- Works at 200% zoom and from 320px up with no horizontal overflow.
- Controls expose state (`aria-pressed`); the pet announces its reactions through a
  polite live region; break completion is announced once, not every tick.
- Landmarks: skip link, `header`, labelled `nav`, `main`, `section[aria-labelledby]`, `footer`.
- No audio, no auto-advancing content.

## Anti-patterns

- Looping WebP/GIF cards in a wall; animation only in visible scenes.
- Two writers for the same transform.
- Generic reveal on everything; each scene has its own entrance.
- Implying an automatic download, or app timings the website previews do not match.
- Hard-coded `/vi/` or `/assets/...` URLs.

## Pre-delivery checklist

- [ ] `npm run check` and `npm run build` pass from `site/`
- [ ] 320 / 375 / 768 / 1024 / 1440px, both languages, no horizontal overflow
- [ ] Drag, release, cancel, keyboard, reduced motion and pause behave in a browser
- [ ] Break previews finish, cancel and pause offscreen
- [ ] Language switch keeps the section; assets load under the production base path
- [ ] Console clean; page usable with JavaScript disabled
- [ ] Credits, artwork terms and requirements current
