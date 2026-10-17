# agent-pet site

The landing page for Agent Pet: a static [Astro](https://astro.build) site,
deployed to GitHub Pages at <https://windywin.github.io/vpet-agent-pet/>.

It is self-contained. Its only dependency outside this folder is one image,
`docs/design/agent-pet-desktop.png`, used as the `og:image`.

## Commands

```bash
npm install
npm run dev        # dev server
npm run build      # -> dist/
npm run preview    # serve dist/
npm run check      # astro check (types + a11y diagnostics)
```

## Deployment

`.github/workflows/deploy-site.yml` builds `site/` with `withastro/action` and
publishes it with `actions/deploy-pages`. Two things have to line up:

- `site` and `base` in `astro.config.mjs`. The site is served from a project
  path, so every internal link and asset URL must go through `base`'s prefix;
  Astro does that automatically for imports, `src` attributes and `href`s it
  sees, but a hardcoded absolute path would break.
- The repository's Pages source must be set to **GitHub Actions** once, under
  Settings → Pages.

The workflow checks out only `site/` and `docs/design/`: the repository also
tracks a 1.2 GiB artwork archive that the build never reads.

## Structure

```text
src/components/   Site (page + runtime entry), Scene (pet actor), Nav, Hero, States,
                  Play, Care, Privacy, Install, Footer
src/i18n/         locale types, en/vi dictionaries, base-aware URL helpers
src/scripts/      motion.ts (pause / reduced motion), pet.ts (one reusable pet),
                  demos.ts (state, play, care, install demos), page.ts (reveal,
                  scroll-spy, language switch), scenes.ts (entry point)
src/pages/        index.astro (English entry), [lang]/index.astro (/en/, /vi/)
src/media.ts      pet animation + still manifest, passed to the runtime as JSON
```

Routes under the project base: `/vpet-agent-pet/en/`, `/vpet-agent-pet/vi/`, and
`/vpet-agent-pet/` (English, canonical `/en/`). Only the entry page redirects, and only
for a visitor who previously picked Vietnamese. Never hard-code `/vi/` or `/assets/...`;
use `localeUrl()` and imported assets so the base path is applied.

Add or change copy in `src/i18n/index.ts`: `Messages` is derived from the English
dictionary, so `npm run check` fails if Vietnamese is missing a key.

### Pet runtime

`PetActor` (`src/scripts/pet.ts`) is configured per scene in `scenes.ts`. It handles
dragging (Pointer Events), tap vs drag, landing, wandering, keyboard movement (arrows move,
Enter/Space pet, Escape resets), speech clamping and media swapping. The demos in
`demos.ts` only call its small API (`setBase`, `react`, `walkToFraction`, `stroll`,
`reset`, `calm`). Pause and reduced motion come from `motion.ts`; paused pages show still
PNGs and stay fully interactive. Everything off screen or in a hidden tab suspends.

## Media

`src/assets/pet/` holds, for each pose, an animated `.webp` and a `-still.png`. They are
committed, so building the site never needs ImageMagick or the artwork archive.

```bash
python3 scripts/prepare_scene_media.py           # WebP + stills; needs `magick` (ImageMagick 7)
python3 scripts/derive_transparent_media.py      # the older GIF derivation (inputs for the above)
```

`prepare_scene_media.py` converts the transparent GIFs in `src/assets/pet/` to WebP and
stills, and builds `idle`, `walk`, `raised` and `water` from the source frames in
`assets/vpet/pet/vup/`, with one shared crop per sequence so feet and scale stay stable.
Only the `.webp` and `-still.png` files are imported (`src/media.ts`); the `.gif` files
are inputs. Inspect any new sequence for transparency on the light scenes, scale,
foot position and a clean loop before committing it.

`derive_transparent_media.py` exists because the `docs/media` previews are exported on an
opaque pale-blue backdrop baked into every frame. It flood-fills from each frame's border
(which keeps enclosed light shapes such as silver hair), writes full-size frames with
`dispose: background`, and crops each animation to the union of its frames. `break-buddy`
is rebuilt from the source frames because `docs/media/break-buddy.gif` uses
`dispose: none` and leaves a scribble of rope positions in a compliant viewer.

## Design

`../design-system/agent-pet/MASTER.md` is the source of truth for colour, type, motion and
component rules. `src/styles/global.css` carries the tokens as CSS custom properties.

## Copy

Text is taken from `../README.md` and `../starter/README.md`; release asset names and
platform requirements come from the release workflow (`../.github/workflows/release.yml`)
and `../starter/docs/install.md`. The download buttons open the latest GitHub release page
(they do not resolve an OS-specific asset). Break demos are labelled website previews and
use their own short durations.
