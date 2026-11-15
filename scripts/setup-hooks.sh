#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
chmod +x .githooks/pre-commit
git config --local core.hooksPath .githooks
echo 'Installed mandatory full-suite pre-commit hook for this repository and its worktrees.'
