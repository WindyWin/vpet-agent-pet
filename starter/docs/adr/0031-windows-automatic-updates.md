# 0031. Windows automatic updates through Setup

- Status: Accepted
- Date: 2026-10-08

## Context

The Windows setup program owns the uninstall log and registration. Replacing its
installation with a portable ZIP would lose that bookkeeping. A running updater
also holds its executable and Qt/runtime DLLs open, preventing replacement.

## Decision

Windows selects the release's `-setup.exe` asset instead of the portable ZIP.
Only non-local builds at the current user's registered Inno Setup path, with the
uninstaller and updater present, enable automatic installation. The shared controller
keeps daily checks, modes, cancellation, verified downloads and session checkpointing.
Windows caches the package as `package.exe`; component manifests are not selected.
Session checkpoints verify a live agent by its executable name, PID and Windows
process creation time. Expired, exited, mismatched or unreadable processes are not
restored; a reused PID cannot revive a stale session.

The installed helper verifies the package, copies itself and the app-local DLLs
into a private temporary directory under the update data directory, copies and
re-verifies Setup, then starts that private helper. The private helper acquires
the installation update lock and waits for both the pet and bootstrap helper to
exit. It holds the monitor receiver lock while running Setup so another monitor
cannot start during replacement. Setup runs silently, without forcing other apps
to close or requesting a restart, and explicitly skips integration/startup tasks.
The helper checks the installed version using the console companion and relaunches
the pet with the original arguments after releasing the locks. Setup failures or failed
version probes leave the pet stopped for manual repair. Target comparison expands
Windows short path aliases; Setup and locks keep the registered path spelling.

A pending attempt is consumed before Setup starts and on reported failures, so
an unsuccessful upgrade cannot create a launch loop. The full download is retained
on failure. Setup's log and the translated result are available in the updates
directory. Inactive helper copies older than a day are removed on later updates,
with a separate lifetime lock protecting a running private helper.

## Consequences

- Setup keeps the uninstall log, installation path, hooks, startup preferences and settings.
- Portable Windows copies keep notifications and manual upgrades.
- Windows downloads the full setup program; component reuse is Linux-only.
- Windows does not offer Linux's directory exchange or startup rollback. Interrupted
  or failed upgrades may need manual repair with Setup.
- Existing Windows versions must first be upgraded manually to obtain the updater.

## Validation

Windows metadata tests cover Setup selection, architecture, asset URL validation
and notification-only behavior for missing digests. The Windows install check
runs the packaged helper, rejects a checksum mismatch, performs a real silent
upgrade, and checks hooks, login registration and uninstall-log preservation.
Native Windows execution requires the Windows release CI runner. Local Linux
checks cover shared controller regressions, translations and helper C++ syntax.
