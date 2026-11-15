#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${ROOT}/.github/scripts/changeset-check.sh"

fail() {
  echo "FAIL: $1" >&2
  exit 1
}

new_repo_with_main() {
  local dir
  dir="$(mktemp -d)"
  git -C "$dir" init -q
  git -C "$dir" config user.name "test"
  git -C "$dir" config user.email "test@example.com"
  git -C "$dir" branch -m main
  mkdir -p "$dir/.changeset" "$dir/firmware"
  cat > "$dir/.changeset/README.md" <<'EOF'
# readme
EOF
  echo "init" > "$dir/firmware/a.txt"
  git -C "$dir" add .
  git -C "$dir" commit -q -m "init"
  git -C "$dir" checkout -q -b feature/test
  echo "$dir"
}

run_check() {
  local repo="$1"
  (
    cd "$repo"
    GITHUB_EVENT_NAME="pull_request" \
    GITHUB_ACTOR="simon" \
    GITHUB_BASE_REF="main" \
    bash "$SCRIPT"
  )
}

test_non_releasable_change_passes() {
  local repo
  repo="$(new_repo_with_main)"
  mkdir -p "$repo/docs"
  echo "doc" > "$repo/docs/readme.md"
  git -C "$repo" add docs/readme.md
  git -C "$repo" commit -q -m "docs"
  run_check "$repo" || fail "non-releasable changes should pass"
}

test_releasable_without_changeset_fails() {
  local repo
  repo="$(new_repo_with_main)"
  echo "code" >> "$repo/firmware/a.txt"
  git -C "$repo" add firmware/a.txt
  git -C "$repo" commit -q -m "firmware change"
  if run_check "$repo"; then
    fail "releasable changes without changeset should fail"
  fi
}

test_releasable_with_changeset_passes() {
  local repo
  repo="$(new_repo_with_main)"
  echo "code" >> "$repo/firmware/a.txt"
  cat > "$repo/.changeset/test.md" <<'EOF'
---
"tempo": patch
---
firmware fix
EOF
  git -C "$repo" add firmware/a.txt .changeset/test.md
  git -C "$repo" commit -q -m "firmware change with changeset"
  run_check "$repo" || fail "releasable changes with changeset should pass"
}

test_non_pr_event_skips() {
  local repo
  repo="$(new_repo_with_main)"
  (
    cd "$repo"
    GITHUB_EVENT_NAME="push" \
    GITHUB_ACTOR="simon" \
    GITHUB_BASE_REF="main" \
    bash "$SCRIPT"
  ) || fail "non-PR event should skip"
}

test_release_preparation_passes_and_missing_notes_fail() {
  local repo
  repo="$(new_repo_with_main)"
  mkdir -p "$repo/scripts"
  cp "$ROOT/scripts/release.py" "$repo/scripts/release.py"
  echo 'project(weather_station VERSION 0.8.16)' > "$repo/firmware/CMakeLists.txt"
  printf '%s\n' '---' '"tempo": patch' '---' 'Test release.' > "$repo/.changeset/release.md"
  git -C "$repo" add .
  git -C "$repo" commit -q -m 'release baseline'
  git -C "$repo" branch -f main HEAD
  python3 "$repo/scripts/release.py" prepare
  git -C "$repo" add .
  git -C "$repo" commit -q -m 'prepare release'
  run_check "$repo" || fail "valid release preparation should pass"
  rm "$repo/docs/releases/0.8.17.md"
  git -C "$repo" add .
  git -C "$repo" commit -q -m 'remove required notes'
  if run_check "$repo"; then
    fail "release without notes should fail"
  fi
}

test_remote_main_advance_preserves_merge_base() {
  local remote checkout shallow
  remote="$(new_repo_with_main)"
  echo code >> "$remote/firmware/a.txt"
  printf '%s\n' '---' '"tempo": patch' '---' 'Remote history regression.' > "$remote/.changeset/remote.md"
  git -C "$remote" add .
  git -C "$remote" commit -q -m 'feature with note'
  checkout="$(mktemp -d)"
  git clone -q --branch feature/test "file://$remote" "$checkout"
  git -C "$remote" checkout -q main
  echo newer > "$remote/newer.txt"
  git -C "$remote" add .
  git -C "$remote" commit -q -m 'main advances after checkout'
  run_check "$checkout" || fail 'advanced remote main must retain its merge base'
  shallow="$(mktemp -d)"
  git clone -q --depth=1 --branch feature/test "file://$remote" "$shallow"
  run_check "$shallow" || fail 'shallow checkout must recover ancestry before comparison'
}

test_remote_main_advance_preserves_merge_base
test_release_preparation_passes_and_missing_notes_fail

test_non_releasable_change_passes
test_releasable_without_changeset_fails
test_releasable_with_changeset_passes
test_non_pr_event_skips

echo "changeset-check tests passed."
