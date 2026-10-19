# Third-party notices

## Qt runtime

Agent Pet uses Qt under LGPLv3. The full [LGPLv3](licenses/LGPL-3.0.txt),
[GPLv3](licenses/GPL-3.0.txt), and [Qt notice and replacement instructions](licenses/QT-NOTICE.txt)
are included in the application and packages. Bundled releases also include
`runtime-licenses/qt/SOURCES.txt` with version-specific corresponding source
download directions; official releases attach matching Qt source archives.
Qt's own third-party components retain their respective terms. See
[Qt release licensing](docs/qt-licensing.md) for publisher instructions.

## VPet artwork

The PNG animation files in `assets/vpet/vup/` come from the default VPet character in [LorisYounger/VPet](https://github.com/LorisYounger/VPet). The upstream README attributes their copyright to the **VUP-Simulator team**.

The files are an unmodified selection from a local VPet checkout at commit `6358445aa54587f9cd524264eaf17ef97bc826a8`. The manifest records the exact copied bytes, including any local differences from that commit. Folder names and frame timings are preserved.

The [artwork notice and authorization terms](licenses/VPET-ARTWORK-TERMS.md) are reproduced from the upstream README. The [full upstream README](licenses/VPET-UPSTREAM-README.md) is also included. Its relative links refer to the original VPet layout; the corresponding selected art here is under `assets/vpet/vup/`.

Preserve this notice, the artwork terms, and the upstream link when distributing this repository or an application package containing its art. The [upstream code license](licenses/VPET-CODE-LICENSE.txt) is included for provenance only; no upstream renderer code has been imported.

## Agent Pet application icon

`packaging/agent-pet.png` (installed as the `agent-pet` icon) is a cropped, resized copy of the idle frame `Default/Nomal/1/_000_250.png` and is covered by the artwork terms above, not by the application code license. `assets/vpet/preview.png`, VPet's tile in Settings, is the same image under the same terms.

## Agent Pet application code

Agent Pet's original application code is under the Apache License 2.0 (`LICENSE`, with `NOTICE`). That license does not apply to the artwork, the icon, or the upstream documents in `licenses/`.
