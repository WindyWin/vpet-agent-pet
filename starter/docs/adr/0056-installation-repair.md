# 0056. Installation repair

- Status: Accepted
- Date: 2026-10-27

## Context

A long-lived installation can contain missing, damaged or obsolete package files.
The update controller only offers newer versions, so an up-to-date installation
cannot request replacement through the app.

## Decision

Add Repair installation beside the existing update controls. Fetch release
metadata by the running version's tag and require the same stable version,
compatible official package URL and SHA-256 digest. Always download a fresh full
package so no installed files or old download cache are reused. Reuse the existing
platform installer, session checkpoint, verification, staging and Linux rollback.

Mark pending repairs explicitly and reload them for the same installed version.
Repairs require an explicit Restart and repair even in automatic install modes,
including on next launch. Discard repair clears only the pending package, leaving
skipped update preferences intact. Revalidate downloaded files before requesting
session checkpoint and restart. All new controls and messages support Vietnamese.

## Consequences

Repair consumes a full package download and requires the exact release to remain
available online. Managed Linux and Windows installations support the action;
macOS and source builds retain manual reinstallation. A broken executable or
updater still needs the external installer. User data and third-party packs are
outside package repair. Windows retains Setup's cleanup and failure guarantees.

## Validation

### Linux evidence — 2026-10-27

Built the controller tests, real updater and translation tests with Qt 6.11.2.
`ctest --test-dir starter/build -R '^(updates|update-install|i18n)$'
passes. Controller coverage includes exact-version metadata, full download,
checksum rejection, retry, cancellation, unmanaged copies, explicit restart,
session checkpoint blocking, pending repair reload, cache revalidation and discard.
The real helper test reinstalls the same version over a corrupted executable,
removes obsolete files and preserves receipt/settings, alongside existing rollback
and unsafe archive checks. Translation regeneration reports no missing strings.
Windows Setup repair and live desktop interaction remain platform acceptance checks.
