# Third-party notices

The PNG animation files in `assets/vpet/vup/` come from the default VPet character in [LorisYounger/VPet](https://github.com/LorisYounger/VPet). The upstream README attributes their copyright to the **VUP-Simulator team**.

The files are an unmodified selection from a local VPet checkout at commit `6358445aa54587f9cd524264eaf17ef97bc826a8`. The manifest records the exact copied bytes, including any local differences from that commit. Folder names and frame timings are preserved.

The [artwork notice and authorization terms](licenses/VPET-ARTWORK-TERMS.md) are reproduced from the upstream README. The [full upstream README](licenses/VPET-UPSTREAM-README.md) is also included. Its relative links refer to the original VPet layout; the corresponding selected art here is under `assets/vpet/vup/`.

Preserve this notice, the artwork terms, and the upstream link when distributing this repository or an application package containing its art. The [upstream code license](licenses/VPET-CODE-LICENSE.txt) is included for provenance only; no upstream renderer code has been imported.

## Fat Cat pet artwork

The PNG frames in `assets/fat-cat/` are the **Orange Fat Cat** pack by **shuaa** of [megupets](https://www.megupets.com), published on [OpenGameArt](https://opengameart.org/content/orange-fat-cat) under [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) (public domain dedication). CC0 requires no credit; Agent Pet gives it anyway, in About. The pack's Idle, Walk, Jump (a wave), Dead and Extra frames were placed in the idle pose's position on a shared 1000 × 1000 canvas and scaled by one factor; the drawings are unchanged. The pack was downloaded as `Orange Fat Cat.zip` (SHA-256 `4926ef27df2736a7cc90b5713731c591f9962b6fa237ac3cb1c5c89277a71918`); `assets/fat-cat/manifest.json` records the exact bytes shipped. The [terms](licenses/FAT-CAT-ARTWORK-TERMS.md) are in `licenses/`. Fat Cat is not bundled: it downloads when chosen in Settings.

## Kuro and Yun pet artwork

The PNG frames in `assets/kuro/` and `assets/yun/` are original Agent Pet artwork, not third-party material. They are drawn from vector shapes by `scripts/draw_kuro.py` and `scripts/draw_yun.py` (with `scripts/pet_art.py`) and dedicated to the public domain under [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/); see [Kuro's terms](licenses/KURO-ARTWORK-TERMS.md) and [Yun's terms](licenses/YUN-ARTWORK-TERMS.md). Neither pet is bundled: each downloads when chosen in Settings.

## Agent Pet application icon

`packaging/agent-pet.png` (installed as the `agent-pet` icon) is a cropped, resized copy of the idle frame `Default/Nomal/1/_000_250.png` and is covered by the artwork terms above, not by the application code license. `assets/vpet/preview.png`, VPet's tile in Settings, is the same image under the same terms.

## Agent Pet application code

Agent Pet's original application code is under the Apache License 2.0 (`LICENSE`, with `NOTICE`). That license does not apply to the artwork, the icon, or the upstream documents in `licenses/`.
