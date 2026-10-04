# Agent Pet

A standalone desktop companion that animates in response to Claude Code and Codex hooks.

**For a new public repository, use [starter/](starter/).** It is a self-contained 23 MB selection of the needed sprites with its own manifest, notices, and plan. Initialize Git inside `starter/` to keep the 749 MB archive outside that repository.

Status: the M1 Qt desktop prototype and relocatable development package are implemented in [starter/](starter/README.md). Native X11 and manual desktop acceptance checks remain open. Hook adapters and release installers are planned.

This directory is the new project root and can be moved into its own repository. The bundled artwork is a real copy. Development and future releases must resolve assets relative to this project or the installed application's resources, without requiring an existing VPet installation or command.

## Included

- **5,498 original PNG frames**, across 558 sequence folders and 25 animation categories.
- Character metadata and nine sequence metadata files: **5,508 imported files**, 735.26 MiB in total.
- A manifest with relative paths, provenance, sizes, and SHA-256 checksums.
- Upstream artwork terms, original README, and code license for provenance. The code license does not replace the artwork terms.
- A portable asset verification/catalog script and a [standalone application plan](PLAN.md).

```text
agent-pet/
├── assets/vpet/
│   ├── manifest.json
│   └── pet/
│       ├── vup.lps
│       └── vup/                 # original sprites and sequence metadata
├── docs/ASSETS.md               # generated catalog
├── licenses/
├── scripts/assets.py
├── PLAN.md
├── README.md
└── THIRD_PARTY_NOTICES.md
```

## Inspect the bundled assets

Python 3.9 or newer is sufficient for these development commands:

```bash
python3 scripts/assets.py verify
python3 scripts/assets.py catalog
```

The commands locate the project from the script's location, so they also work when invoked from a different working directory. `verify` checks every imported file, rejects symlinks, and checks for unlisted asset files. `catalog` lists every sequence and preserves upstream folder names and filename timing.

No sprite conversion has been applied. Eight frames in `IDEL/Squat/C_Happy` lack duration suffixes; the catalog flags them so playback can exclude that sequence until timing is resolved.

## Distribution

The release plan bundles the renderer, hook command, selected animation resources, and required notices into an installable application. Users should not need this source checkout, Python, VPet, or a separately installed runtime to run the release.

Keep the original artwork as the source bundle; choose and optimize a smaller release pack during implementation. See [the packaging plan](PLAN.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
