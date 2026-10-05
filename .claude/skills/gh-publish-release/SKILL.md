---
name: gh-publish-release
description: Use when the user asks to release, publish or ship a new Agent Pet version — bumps the version through a PR, merges it, tags it, waits for the release workflow and publishes the GitHub release via gh.
---

# Publish an Agent Pet release

Publishing is outward-facing: it makes a public GitHub release, and installs with automatic updates will pick it up. Only run this when the user has asked for a release in this conversation.

## Before running

1. Make sure everything meant for the release is already merged to `main`. The script releases `origin/main`, not the current branch. If the user's fix is on a branch, offer to open and merge its PR first.
2. Confirm the version with the user. With none given the script bumps the patch (`0.7.4` → `0.7.5`). Say what will be released: the commits on `main` since the last tag (`git log --oneline $(git describe --tags --abbrev=0 origin/main)..origin/main`).
3. The working tree must have no uncommitted tracked changes; the script refuses otherwise.

## Run

```bash
.claude/skills/gh-publish-release/publish.sh                # next patch version
.claude/skills/gh-publish-release/publish.sh 0.8.0          # explicit version
.claude/skills/gh-publish-release/publish.sh --no-publish   # stop at the draft release
```

It takes several minutes (PR checks, then the tag build). Run it in the background and report when it finishes.

What it does, in order: refuses if the tag exists or the version is not higher; creates `release/X.Y.Z` from `origin/main` and bumps `starter/CMakeLists.txt`; opens a PR and waits for its checks; merges it with a merge commit; tags `vX.Y.Z` on `main` and pushes the tag; waits for the tag's release workflow, which builds the assets and creates a **draft** release; publishes the draft as Latest (skipped with `--no-publish`).

## After

Report the release URL and that the workflow passed. Note any other drafts left behind (`gh release list`), such as an earlier tag nobody published, without publishing them.

## If it fails

Stop and show the failing step; do not retry blindly or use `--force`. Typical states: PR checks failed (fix on a branch, the release branch can be deleted); tag pushed but workflow failed (the tag exists, so the next attempt needs a new version, or delete the tag and any draft only if the user agrees).
