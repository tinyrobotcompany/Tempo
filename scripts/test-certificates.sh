#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${TEMPO_TEST_CACHE:-$root/build-host/dependencies}"
venv="$cache/certificate-service-python314"
python="${TEMPO_SERVICE_PYTHON:-$(command -v python3.14 || true)}"
if [[ -z "$python" ]]; then
  for candidate in /opt/homebrew/bin/python3.14 /usr/local/bin/python3.14; do
    if [[ -x "$candidate" ]]; then python="$candidate"; break; fi
  done
fi
[[ -n "$python" ]] || { echo 'Certificate-service tests require Python 3.14 (brew install python@3.14).' >&2; exit 1; }
if [[ ! -x "$venv/bin/python" ]]; then "$python" -m venv "$venv"; fi
lock="$root/services/certificates/requirements-test.txt"
digest="$(shasum -a 256 "$lock" | cut -d ' ' -f 1)"
if [[ ! -f "$venv/lock.sha256" || "$(cat "$venv/lock.sha256")" != "$digest" ]]; then
  "$venv/bin/python" -m pip install --disable-pip-version-check --only-binary=:all: --require-hashes -r "$lock"
  printf '%s\n' "$digest" > "$venv/lock.sha256"
fi
cd "$root/services/certificates"
"$venv/bin/python" -m pytest -q
