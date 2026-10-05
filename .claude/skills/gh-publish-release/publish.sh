#!/usr/bin/env bash
# Release Agent Pet: bump version PR -> merge -> tag -> wait for release workflow -> publish draft.
# Usage: publish.sh [X.Y.Z] [--no-publish]   (default version: current + patch bump)
set -euo pipefail

root=$(git rev-parse --show-toplevel)
cmake="$root/starter/CMakeLists.txt"
publish=1; version=""
for arg in "$@"; do
  case "$arg" in
    --no-publish) publish=0 ;;
    [0-9]*.[0-9]*.[0-9]*) version=$arg ;;
    *) echo "usage: publish.sh [X.Y.Z] [--no-publish]" >&2; exit 2 ;;
  esac
done

cd "$root"
gh auth status >/dev/null 2>&1 || { echo "gh is not authenticated" >&2; exit 1; }
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "Working tree has uncommitted changes; commit or stash first." >&2; exit 1; }
git fetch -q origin --tags

# Releases are cut from main exactly as it is on the remote.
current=$(git show origin/main:starter/CMakeLists.txt | sed -n 's/^project(AgentPet VERSION \([0-9.]*\).*/\1/p')
[ -n "$current" ] || { echo "Cannot read the current version from origin/main" >&2; exit 1; }
if [ -z "$version" ]; then IFS=. read -r a b c <<<"$current"; version="$a.$b.$((c + 1))"; fi
tag="v$version"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then echo "Tag $tag already exists" >&2; exit 1; fi
newest=$(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -1)
{ [ "$newest" = "$version" ] && [ "$current" != "$version" ]; } || { echo "New version $version must be greater than $current" >&2; exit 1; }
echo "Releasing $tag (main is at $current)"

# The release PR's own checks are skipped (CI ignores release/* PRs), so main's CI is the gate.
sha=$(git rev-parse origin/main)
ci=$(gh run list --branch main --commit "$sha" --event push --limit 1 --json databaseId,status,conclusion \
  --jq '.[0] // empty | "\(.databaseId) \(.status) \(.conclusion)"')
[ -n "$ci" ] || { echo "No CI run found for main at ${sha:0:7}" >&2; exit 1; }
read -r ci_run ci_status ci_conclusion <<<"$ci"
if [ "$ci_status" != completed ]; then
  echo "Waiting for main's CI run $ci_run"
  gh run watch "$ci_run" --interval 20 --exit-status >/dev/null || { echo "main's CI failed: run $ci_run" >&2; exit 1; }
elif [ "$ci_conclusion" != success ]; then
  echo "main's CI at ${sha:0:7} is $ci_conclusion (run $ci_run); fix main before releasing" >&2; exit 1
fi

branch="release/$version"
start=$(git rev-parse --abbrev-ref HEAD)
git checkout -q -b "$branch" origin/main
trap 'git checkout -q "$start" 2>/dev/null || true' EXIT
sed -i "s/^project(AgentPet VERSION $current /project(AgentPet VERSION $version /" "$cmake"
if git diff --quiet; then echo "Version line was not updated" >&2; exit 1; fi
git commit -qam "Bump version to $version"
git push -q -u origin "$branch"

pr=$(gh pr create --base main --head "$branch" --title "Bump version to $version" \
  --body "Prepare Agent Pet $version.

The \`$tag\` tag runs the release workflow: it validates the version/tag match, builds the install archive and update components, and creates a draft GitHub release.")
echo "Opened $pr"
sleep 10  # let checks register
gh pr checks "$pr" --watch --interval 20 --fail-fast
gh pr merge "$pr" --merge

git fetch -q origin
git checkout -q main
git merge -q --ff-only origin/main
grep -q "^project(AgentPet VERSION $version " "$cmake" || { echo "main does not carry $version after merge" >&2; exit 1; }
git tag "$tag"
git push -q origin "$tag"
echo "Pushed $tag"

run=""
for _ in $(seq 1 30); do
  run=$(gh run list --branch "$tag" --limit 1 --json databaseId --jq '.[0].databaseId // empty')
  [ -n "$run" ] && break
  sleep 5
done
[ -n "$run" ] || { echo "Release workflow did not start for $tag" >&2; exit 1; }
gh run watch "$run" --interval 20 --exit-status

if [ "$publish" = 1 ]; then
  gh release edit "$tag" --draft=false --latest
  echo "Published: $(gh release view "$tag" --json url --jq .url)"
else
  echo "Draft release left unpublished: $(gh release view "$tag" --json url --jq .url)"
fi
