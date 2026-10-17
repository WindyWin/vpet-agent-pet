# Qt release licensing

Agent Pet's application code stays Apache-2.0. Its Qt runtime is used under
LGPLv3; artwork and other dependencies retain their separate terms.

## What packages include

Every build embeds `licenses/QT-NOTICE.txt`, `LGPL-3.0.txt`, and `GPL-3.0.txt`.
About explains the Qt license and displays these texts, including instructions
for replacing libraries on Linux, Windows, and macOS. Full application builds
require shared Qt; internal static Agent Pet libraries do not embed static Qt.

The three bundle scripts share `scripts/qt_licenses.py`. They always include
the license texts and replacement instructions in `runtime-licenses/qt/`, even
when the SDK has no `Licenses` folder. Existing SDK notices are also preserved.
`SOURCES.txt` records the Qt version and complete source archive URLs. On Linux
the directory is inside `share/agent-pet/`; on macOS it is inside
`Agent Pet.app/Contents/Resources/`; on Windows it is beside the executables.

Official releases use the unmodified Qt 6.5.3 SDK installed by CI. The release
job downloads QtBase, QtSvg and QtImageFormats sources, checks each archive
against Qt's published SHA-256, and attaches them plus `qt-source-SHA256SUMS.txt`
to the same release as the binaries. A failed download/checksum stops the release.
QtBase supplies the linked libraries and platform/TLS plugins. Deployment tools
may also copy QtSvg and QtImageFormats plugins, so their sources are included.

## Publishing and custom builds

Keep Qt source assets accessible at no charge for as long as the corresponding
binaries are distributed. The release download table and packaged `SOURCES.txt`
point users to the sources. When distributing binaries elsewhere, preserve these
directions and provide equivalent access to the matching sources.

When changing the SDK version, update `QT_VERSION` in
`.github/workflows/release.yml`; all three SDK installations, the source download
step and release source links use that value.
When adding a Qt runtime module or deployed plugin, check its license and update
`MODULES` in `scripts/qt_licenses.py` to include its corresponding source. Build
tools such as LinguistTools are not shipped as application libraries.

For patched/custom Qt, set `QT_SOURCE_URLS` to whitespace-separated HTTPS URLs
of the complete matching source, all patches and build scripts before packaging.
The helper requires this for system `/usr` and Homebrew Qt, whose sources can
differ from the upstream release. Publish those sources alongside your binaries;
the official CI source download step only covers the unmodified official SDK.
For a native system package that does not bundle Qt, its Qt dependency and
corresponding source are managed by the distribution; this bundle helper is not
used by `cmake --install` or the Arch PKGBUILD.

Preserve notices for bundled third-party dependencies too. The code changes here
do not grant additional rights to artwork. These measures implement the release
process; they do not certify every custom dependency or distribution channel.

References: [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations),
[LGPLv3](https://www.gnu.org/licenses/lgpl-3.0.html), and
[GPLv3 section 6](https://www.gnu.org/licenses/gpl-3.0.html#section6).
