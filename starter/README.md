# Agent Pet

A small starting repository for a desktop pet that reacts to Claude Code and Codex activity. The sprite files are bundled here, so development can proceed without a local VPet installation.

The first asset pack contains **180 original PNG frames in 20 animation sequences (21.85 MiB)**. They cover idle, thinking, reading, working, waiting for input, tool errors, turn completion, sleeping, startup, and closing. [animations.json](assets/vpet/animations.json) maps each state to its sequences and records frame durations. [manifest.json](assets/vpet/manifest.json) records every file's size and SHA-256 hash.

This folder is ready to become its own repository:

```bash
cd agent-pet/starter
python3 scripts/verify_assets.py
git init
```

The asset verifier uses only Python's standard library and works from any current directory. The desktop application and agent hooks are still to be built; see [PLAN.md](PLAN.md).

## Artwork and publishing

Artwork credit: **VUP-Simulator team**, via [LorisYounger/VPet](https://github.com/LorisYounger/VPet). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [upstream artwork terms](licenses/VPET-ARTWORK-TERMS.md).

The upstream terms require source attribution and a link for noncommercial use and require the authorization information to accompany distributions. They list extra conditions for commercial use, including contacting the rights holder. Git itself adds no license and does not change those terms. A public repository containing these sprites is a distribution, so keep the notices and this credit in the repository. If the app shows the sprites to users, carry the credit into its About screen and release package as well.

No license has been selected for the new Agent Pet application code. Choose one before inviting reuse or contributions, and state clearly that it applies to your original code while the bundled artwork keeps its separate terms. The upstream Apache-2.0 license copied in `licenses/` is for VPet code and does not grant an Apache-2.0 license to the sprites or future Agent Pet code.
