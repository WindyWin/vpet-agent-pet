# 0044. Qt notices and corresponding source in releases

- Status: Accepted
- Date: 2026-10-16

## Context

Bundle scripts relied on SDK license folders or fell back to a license link.
About mentioned the Qt version but did not display LGPL terms. Source directions
named only QtBase and pointed to a generic archive directory, although deployment
tools can include QtSvg and QtImageFormats plugins too.

## Decision

Vendor the full LGPLv3 and GPLv3 texts, embed them and replacement instructions
in About, and share one Qt notice generator between all three bundle scripts.
Always emit version-specific source URLs, preserving SDK notices when available.
Official release CI verifies and attaches the three corresponding Qt source
archives alongside the binaries. Require explicit matching source URLs for
bundled distro/Homebrew Qt. Reject static Qt for the full application.

## Consequences

SDK layout no longer determines whether the required license texts ship. Qt
upgrades must also update the release's source downloads and links. Custom Qt
publishers must supply their complete source, patches and build scripts. Source
availability and third-party license obligations remain publisher responsibilities.
Application code remains Apache-2.0 and artwork retains its separate terms.

## Validation

### Regression evidence — 2026-10-16

The Qt packaging tests cover an SDK with and without notices, system Qt source
overrides, successful source verification, and rejecting a checksum mismatch.
The existing embedded-resource test also checks the new license resources.

On Linux with shared Qt 6.11.2, built `agent-pet`, `prototype-tests` and
`i18n-tests`; CTest `qt-licenses` and `i18n` passed, as did
`prototype-tests releaseMetadataIsEmbedded` offscreen. Updating translations
reported zero new strings after translating the Qt notice. Workflow YAML parsed
and `git diff --check` passed. Downloaded all three real Qt 6.5.3 source archives
and verified their upstream SHA-256 checksums. Windows/macOS packaging remains
for their platform CI runners.
