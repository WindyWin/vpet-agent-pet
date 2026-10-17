# Agent Pet site redesign — implementation plan

Status: implemented 2026-10-17 (runtime, demos, localization, choreography, docs). Not committed or deployed. Remaining: a frame-by-frame review of each animated WebP loop. The sections below are the original plan; the checklist at the end records what was verified.

## Goal

Make the landing page feel like a small, living desktop world. The pet should move, react, and be draggable in every main section. Replace the brown/amber theme, give sections richer compositions and animation, and support complete Vietnamese and English experiences.

Keep the existing Astro site in `site/` and its static GitHub Pages deployment. No backend or connection to a visitor's coding agent is required for website demonstrations.

## Important: current working-tree state

Partial implementation already exists from the work immediately before this handoff:

- Rewritten section templates: `Hero.astro`, `States.astro`, `Play.astro`, `Care.astro`, `Install.astro`, `Footer.astro`, and `Nav.astro`.
- Added `Scene.astro`, `Privacy.astro`, and `Site.astro`.
- Added English/Vietnamese dictionaries in `site/src/i18n/index.ts` and localized routes in `site/src/pages/[lang]/index.astro`.
- Updated `Base.astro`, `Icon.astro`, `media.ts`, `pages/index.astro`, and `styles/global.css`.
- Added `site/scripts/prepare_scene_media.py`; generated WebP animations and PNG stills in `site/src/assets/pet/`.

These are unfinished drafts, not a verified implementation. In particular, `Site.astro` imports `../scripts/scenes`, but `site/src/scripts/scenes.ts` has **not been written**. Expect that import to block a build until the controller is implemented or its import is temporarily removed. Dragging, demo controls, timers, language persistence, and animation settings are not wired up.

No build, type check, browser review, or interaction tests were completed for these drafts. No commit, push, or deployment was performed.

The `site/` and `design-system/` directories were already untracked before these changes. Git cannot reconstruct their previous contents with `git restore`. Review and reuse the drafts deliberately; do not discard those directories to undo this work.

## 1. Visual direction

Concept: **a playful desktop companion, with a different little scene in every section**.

Use a light, soft base with lilac, peach, and pink. Preserve dark panels for terminal content. Let the real character artwork lead the design.

| Role | Starting color |
| --- | --- |
| Page background | Cloud `#F7F5FC` |
| Main text | Deep plum `#282238` |
| Secondary text | Muted plum `#6D647E` |
| Primary action | Violet `#7050D5` |
| Primary action hover | Deep violet `#5B39BB` |
| Supporting surfaces | Lavender `#EAE3FA` |
| Warm accent | Peach `#FFD5BD` |
| Playful accent | Pink `#F3C5DF` |
| Terminal | Midnight plum `#211C30` |

Treat these as candidate tokens, not validated contrast results. Check all text, buttons, focus outlines, and selected states on their actual backgrounds.

Typography proposal: Nunito for expressive headings, Nunito Sans for body/UI, and JetBrains Mono for real terminal content. Verify Vietnamese glyph coverage and accent clearance in the fonts actually loaded. Avoid tight line heights that clip Vietnamese marks.

Composition rules:

- Use large headings, compact readable copy, layered windows, plants, shelves, rugs, stickers, and speech bubbles.
- Give the pet substantial space; it should be the main visual, rather than a small decoration inside a feature card.
- Vary layouts: split hero, selectable state stage, wide playground, arched care scene, local-computer privacy panel, download dock.
- Use subtle rotation and overlap for secondary decorative elements. Keep text and controls easy to read.
- Animate a visitor's interaction first; use quieter ambient motion around it.

Before final implementation, revise `design-system/agent-pet/MASTER.md`. Its old brown palette, dark-only requirement, and limits of one entrance/one reveal conflict with this redesign. The user's new direction supersedes those constraints. Keep its useful rules about accurate product claims, artwork credits, and readable content.

## 2. Sections and interactions

| Section | Composition | Pet behavior | Visitor controls |
| --- | --- | --- | --- |
| Navigation | Sticky wordmark, section links, EN/VI, motion toggle, download CTA | None required in navigation | Keyboard-friendly links; accessible mobile navigation |
| Hero | Layered desktop window, dark terminal, soft desktop floor, plant, status bubble | Idle, occasional short walk, head pat, pickup and landing | Drag pet, tap pet, download CTA, jump to demo |
| Agent states | State selectors beside one large stage and a terminal status line | Thinking, reading, working, needs input, error, finished, sleeping, startled | Selecting a state updates pose, bubble, selected control, and terminal line together |
| Playground | Illustrated room with window, shelf, plant, rug, snack jar | Walk, head pat, dance, snack, yawn, pickup and landing | Drag, action buttons, reset position, keyboard movement |
| Care | Warm break corner with arch, water glass, small timer | Stretch/jump rope, quiet rest, drinking | Choose a break preview; update timer and reaction |
| Privacy | Local computer boundary, small status signals, shield | Read/idle, react to petting and dragging | Pet interaction; clear explanation of local status handling |
| Install | Pet above a platform dock, system selectors, one visible download panel | Walk toward chosen platform, then wave | Select Linux/macOS/Windows; see matching requirements and download action |
| Footer | Small resting scene alongside project links and artwork credit | Sleep, wake/react when tapped | Tap/pet and return to top |

Keep all eight agent states discoverable on mobile. Do not turn the experience into a long wall of looping GIF cards.

Break previews must be labeled as previews. Proposed short website previews: 10 seconds of movement, 20 seconds of eye rest, and 8 seconds of drinking. Clearly distinguish those from the desktop app's actual reminder intervals and 60-second movement break.

Download buttons should describe their destination accurately. The current draft opens the latest GitHub release page; it does not directly download an OS-specific asset. Do not imply automatic download unless asset resolution is implemented.

## 3. Shared pet behavior

Build one reusable controller and configure it per section. Keep animation pose, position, facing, and speech separate so every combination does not need its own state.

Suggested poses:

`idle`, `walk`, `raised`, `thinking`, `reading`, `working`, `needs-input`, `tool-error`, `turn-finished`, `sleeping`, `startled`, `touch-head`, `dance`, `snack-hungry`, `fidget-yawn`, `break-buddy`, `water`.

Suggested movement lifecycle:

1. Idle at the scene's baseline.
2. Optionally walk to a bounded horizontal destination when visible and motion is enabled.
3. On pointer down, record the pointer and original position; capture that pointer.
4. Distinguish a tap from a drag using a small movement threshold.
5. On actual drag, show the raised pose, update position, and adjust the ground shadow.
6. On release, settle toward the local baseline and play a brief landing reaction.
7. Return to the current section's selected pose, not an unrelated global idle pose.

Implementation requirements:

- Use Pointer Events for mouse, pen, and touch. Apply `touch-action: none` only to the pet drag target.
- Handle `pointercancel`, lost capture, focus loss, and page visibility changes so the pet cannot get stuck in a dragged state.
- Limit dragging to the scene bounds. Recalculate bounds on resize using `ResizeObserver`.
- Clamp speech bubbles independently so they remain legible near scene edges.
- Give position transforms to one controller. Animate sway/squash on a nested visual element so CSS and JS do not overwrite each other's transforms.
- Provide arrow-key movement, Enter/Space for petting, Escape/reset for returning home, and visible focus.
- Keep short-lived reactions separate from persistent selected states. Cancel stale timers when the visitor chooses a new action.
- Make every action available through a visible button or keyboard interaction; dragging must not be required to access content.

## 4. Motion design and performance

Use three layers of animation:

- **Ambient:** pet idling, occasional short walks, a slow cloud drift, restrained decorative movement.
- **Interactive:** pickup, sway, landing, head pats, state changes, bubbles, platform selection, action feedback.
- **Entrance:** terminal lines arrive, the hero scene assembles, state controls appear in a short stagger, playground props enter, and the care scene becomes active.

Keep entrance sequences specific to each scene. The current draft provides generic reveal hooks; it still needs scene-specific choreography.

Avoid scroll hijacking. Normal scrolling and anchor navigation should work throughout.

Motion controls:

- Read `prefers-reduced-motion` before starting animation.
- Offer a visible pause/resume button, with localized label and accurate pressed state.
- Save the explicit preference in local storage; handle unavailable storage gracefully.
- Pause pet media as well as CSS/JS motion. A CSS rule cannot stop an animated GIF or WebP.
- Switch to a representative PNG still while paused or offscreen.
- Suspend actors outside the viewport with `IntersectionObserver`; stop work while the document is hidden.
- A paused page must remain interactive: state changes, language switching, platform selection, and immediate pet positioning still work.
- Pause break countdowns when their scene is offscreen, motion is paused, or the page is hidden; resume without creating duplicate intervals.

Use native CSS transitions, the Web Animations API, and a small controller initially. A large animation or physics dependency is unnecessary for these behaviors.

## 5. Artwork pipeline

Use existing licensed artwork from the repository. Preserve the footer's attribution and separate artwork terms.

Prepared assets:

- Existing transparent preview GIFs have WebP animation counterparts and static PNG frames.
- `idle` comes from `Default/Happy/1`.
- `walk` comes from `MOVE/walk.right/B_Nomal`.
- `raised` comes from `Raise/Raised_Static/B_Happy`.
- `water` comes from `Drink/Happy/back_lay`.

`site/scripts/prepare_scene_media.py` regenerates these derived assets with ImageMagick. The normal site build must use committed outputs and must not need the large source artwork archive or ImageMagick.

Visually inspect every derived sequence. Check transparency on the new light backgrounds, common scale, stable feet, correct frame timing, and a clean loop. Crops are shared within a sequence, but different poses still need scale/alignment review. Some source sequences may include an entrance/exit rather than a seamless loop.

Only load animation for visible actors. Render stills first and preserve fixed scene dimensions to avoid layout shifts. Review generated file sizes before adding more animated artwork.

## 6. English and Vietnamese

Routes beneath the existing project base:

- `/vpet-agent-pet/en/`
- `/vpet-agent-pet/vi/`
- `/vpet-agent-pet/` as a useful English entry page, canonicalized to `/en/` in the draft.

Use one shared component tree with typed translation dictionaries. Avoid parallel copies of page templates.

Translate:

- Navigation, section copy, buttons, instructions, speech bubbles, and example terminal statuses.
- Pet interaction feedback, timer labels, download requirements, notices, and footer copy.
- Accessibility labels, skip links, page titles, descriptions, and social metadata.

Keep product names, command syntax, filenames, and actual package names unchanged.

Language switch behavior:

1. Switch to the corresponding localized page.
2. Preserve the current section hash, such as `#play`.
3. Remember the chosen language for a future visit to the entry page if desired.
4. Do not redirect explicit `/en/` or `/vi/` visits based on browser language.

Set the correct `html lang`, canonical, and alternate-language links. Route and asset helpers must account for `/vpet-agent-pet`; avoid hardcoded root URLs such as `/vi/` or `/assets/...`.

Review Vietnamese as natural product copy, not word-for-word translation. Test long labels and headings at small widths and 200% zoom. Prefer HTML for translatable demo UI instead of screenshots containing English text.

## 7. Suggested architecture

```text
site/src/
  components/
    Site.astro           Shared page composition and runtime entry
    Scene.astro          Pet actor, speech, shadow, scene decoration slot
    Nav.astro
    Hero.astro
    States.astro
    Play.astro
    Care.astro
    Privacy.astro
    Install.astro
    Footer.astro
  i18n/index.ts          Locale types, dictionaries, base-aware URL helpers
  scripts/scenes.ts     Missing: controllers and interaction wiring
  pages/index.astro     English entry page
  pages/[lang]/index.astro
  layouts/Base.astro    Language, metadata, fonts, initial motion preference
  media.ts              Animation/still manifest
  styles/global.css    Tokens, compositions, responsive styles, motion
```

The draft templates expose `data-*` hooks for state controls, playground actions, break controls, platform panels, motion settings, and locale links. Reuse them or simplify them while building the runtime.

If the runtime grows, split it into pet movement, motion settings, and section demo modules. Keep each controller responsible for cleanup of observers, animation frames, and timers.

## 8. Implementation order

1. **Review the drafts and visual direction.** Inspect templates, translation structure, CSS, generated artwork, and the old design-system conflicts. Decide which draft pieces to keep.
2. **Make a static baseline work.** Resolve the missing runtime import, verify routes and assets, and inspect desktop/mobile layouts with still images before adding behaviors.
3. **Build one complete pet scene.** Prove pickup, dragging, release, petting, reset, keyboard movement, reduced motion, and touch scrolling in the playground.
4. **Share the actor across sections.** Configure scene sizes, baseline positions, speech, and persistent poses. Fix artwork scale and bubble placement.
5. **Connect section controls.** Synchronize the agent-state demo; add action buttons, cancellable break previews, platform selection, and footer wake-up behavior.
6. **Finish localization.** Wire the language switch and hash preservation; review both languages and all dynamic feedback.
7. **Add choreography.** Introduce section-specific entrance sequences and ambient motion, with offscreen/page-hidden suspension and a global pause setting.
8. **Validate and document.** Run checks, review screenshots and interactions, update the design system and site README, and only then prepare deployment.

## 9. Acceptance checklist

- [x] Every main section has a pet that can be interacted with and dragged within its scene.
- [x] Pickup, walking, landing, petting, and selected-state recovery work without conflicting transforms.
- [x] The state demo changes pet pose, text, and selected state together.
- [x] Playground actions and reset work repeatedly without stale callbacks.
- [x] Break previews finish, cancel cleanly, and pause when inactive.
- [x] Platform selection updates requirements and the visible download action.
- [x] Every visible and accessible string is covered in English and Vietnamese.
- [x] Language switching preserves the current section and all URLs work under the Pages base path.
- [x] Motion pause and reduced-motion settings stop animated media, CSS motion, and autonomous movement.
- [x] Offscreen and hidden-page scenes suspend animation work.
- [x] Keyboard users can reach controls, move/pet/reset actors, and see focus.
- [x] Touch dragging does not prevent scrolling outside the pet.
- [x] At 375, 768, 1024, and 1440 pixels, no text, bubbles, controls, or artwork cause horizontal overflow.
- [x] Vietnamese headings and controls work at 200% zoom.
- [x] Useful copy, links, and download access remain available when JavaScript is disabled.
- [ ] Artwork is transparent, consistently sized, correctly attributed, and visually checked in motion.
- [x] Product claims and release requirements match the project documentation at implementation time.
- [x] `npm run check` and `npm run build` pass from `site/`.
- [x] Browser checks cover both languages, state switching, dragging/release/cancel, keyboard controls, motion preferences, timers, and platform selection.
- [x] Browser console is clean and all assets load from the production base path.
- [x] `design-system/agent-pet/MASTER.md` and `site/README.md` describe the final implementation.

Verified with headless Chromium (Playwright) against `astro preview` on 2026-10-17: drag/clamp/release/cancel, touch drag without scrolling, keyboard, state sync, playground actions, break timer pause/resume/cancel/finish, platform selection, language switch with hash, entry-page redirect, reduced motion, pause persistence, offscreen suspension, no-JS content, overflow at 320–1440px in both languages, and a clean console. Unchecked: the per-sequence artwork review in motion.
