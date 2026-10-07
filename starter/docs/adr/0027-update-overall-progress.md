# 0027. One overall progress bar for updates

- Status: Accepted
- Date: 2026-10-07

## Context

A component update downloads a manifest and then one archive per changed
component. Download progress was computed per file, so users saw “Downloading
update… 0–100%” repeat several times with no sign of how much remained.

## Decision

- Progress is cumulative over the whole update. After the manifest is read the
  controller queues only the components that are neither installed nor already
  cached and verified, sums their archive sizes, and reports
  `(finished bytes + current file) / total`. It cannot decrease.
- With more than one file the status reads “Downloading update… 42% (file 2 of 5)”;
  a single file shows the plain percentage. The manifest stage has no known total
  and shows a busy bar.
- A fallback to the full archive starts a new total and says “Downloading full
  update…”, so the restart is explained rather than looking like a glitch.
- The updates dialog gains a `QProgressBar` shown only while downloading.
  `Controller::progress()` exposes the percentage (-1 when unknown). The pet
  menu indicator is unchanged.

## Consequences

Files already present are checked once, before downloading, instead of lazily per
file; the cost is the same hashing done earlier. The bar is per update, not per
byte rate: no speed or time estimate.

## Validation

- 2026-10-07: `update-tests` `progressSpansAllComponents`,
  `progressCountsOnlyMissingComponents` and `progressRestartsForFullPackageFallback`
  (monotonic percentage, file counts, hidden bar when done, fallback label).
