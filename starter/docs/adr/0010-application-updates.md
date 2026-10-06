# 0010. Verified component updates

- Status: Accepted
- Date: 2026-10-04

## Context

Releases are large and mostly unchanged between versions, and the pet runs while
agents work. An update must be authenticated and must never leave a broken
installation.

## Decision

`src/updates/release.*` validates stable release metadata, exact repository asset
URLs and SHA-256 digests, using `platform/contracts/update_layout.h` for asset names. `controller.*` owns asynchronous
Qt Network requests, bounded daily checks, persisted update preferences, UI and
pending downloads. Hook, emit, integration and autostart commands return before
constructing this service. Requests use TLS verification, size limits and idle
timeouts; package downloads permit HTTPS redirects to GitHub's asset CDN.

`scripts/package.py` publishes the full legacy tarball plus independent
archives (`app`, `runtime`, `artwork`, and `artwork-<sequence-hash>`) and a version/architecture-specific
`-components.json`. Archives use sorted paths and fixed tar/gzip metadata. The
manifest records archive names, sizes and SHA-256 hashes, plus each target file's
path, size, hash and executable flag. Runtime libraries/plugins/notices form the
runtime component; the catalog RCC, pack index and artwork notices form artwork;
each PNG sequence directory becomes its own RCC and component, named by the SHA-256
of its source directory path. Remaining files form the app component. Manifest
format 2 supports the variable pack list; the updater also accepts format 1.
Release CI publishes the full archive, manifest and every component. Full archives preserve first installation
and upgrade compatibility with older clients; upload storage is not reduced.

`components.*` validates the manifest after checking its GitHub asset digest,
including target version/architecture, safe unique paths and size bounds. The
controller compares actual installed files with the target file list, so no
previous manifest or intermediate version is needed. Matching components are
reused; missing or altered files cause their component to download. Completed
archives survive a retry. Superseded component downloads are removed when a new
manifest is accepted, and successful installation removes its downloads.
An unsupported manifest or failed component request can fall back once to the full
archive with its independently verified GitHub digest. User cancellation does not
trigger a fallback download.

`--apply-components` rechecks local files, copies matching components into a fresh
stage, and extracts verified archives for the others. It verifies all staged
files and rejects undeclared files before probing or launching the executable.
Copies are independent of the previous installation to preserve rollback. A
component that changes locally after downloading can cause installation to fail
safely; retrying the download fetches the required replacement. Both package
formats share the same transaction, startup health check and recovery logic.

`agent-pet-updater` uses libarchive for bounded extraction, rejects links and
traversal, probes the staged executable, preserves the install receipt, and uses
Linux directory exchange for replacement. The update lock excludes competing
GUI starts; the existing IPC receiver lock excludes a running monitor during
replacement. A journal and per-transaction marker distinguish crashes before
and after the exchange. The newly launched GUI acknowledges readiness after its
window and monitor have started; failure restores the old directory. The helper
waits for the child to exit after committing so QProcess ownership does not kill
the updated app. Settings and external integrations are never rewritten by the
helper. Filesystem/power-loss guarantees remain those of the host filesystem;
the recovery tests exercise process interruption around the directory exchange.

Release bundles include the helper, libarchive, Qt TLS plugins and OpenSSL's
runtime closure, including Qt distributions that dynamically load OpenSSL.
`--check-update-runtime` checks TLS backend availability without a display or
network request and is included in isolated-package validation. CI installs
libarchive/OpenSSL development packages. Publishing the existing draft release
makes it discoverable; the app accepts only matching stable-version assets with
GitHub-provided SHA-256 digests for installation.

## Consequences

- Only changed components download; the full archive remains for first installs and
  older clients.
- A failed start rolls back to the previous directory.
- Installation in place is Linux-only; macOS only announces releases
  ([0020](0020-macos-port.md)).

## Validation

Validation: `update-tests` covers version ordering, metadata/URL/architecture
validation, checksum mismatch, offline checks, throttling, skipped versions,
cancellation and failed download writes,
automatic downloads, active-session restart blocking, and interrupted-exchange
recovery. `scripts/check_update.py` runs the real helper against disposable
packages to test unsafe archives, failed startup rollback, successful replacement
and receipt/settings preservation. A disposable release-style build also passed automatic installation at the next
launch and readiness acknowledgement from the actual updated GUI (offscreen).
Isolated-package HTTPS runtime, install/upgrade/autostart/uninstall checks, and all
seven CTest suites passed locally. Actual GitHub release rollout and desktop
interaction remain manual acceptance checks.

Component validation adds target-version/path rejection, application-only network
requests, cached retry, cancellation, corruption repair and readiness after restart.
The helper integration check uses the real component publisher to cover reused
files without cached archives, modified or symlinked installed files, corrupt
archives, final file hashes, removal of obsolete files and failed-start rollback.
