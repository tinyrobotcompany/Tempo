#!/usr/bin/env bash
set -euo pipefail
port="${1:?Usage: scripts/device-info.sh /dev/cu.usbmodem...}"
python_bin="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/tools/python/v6.1/venv}/bin/python"
if [[ ! -x "$python_bin" ]]; then
  echo 'Activate your ESP-IDF Python environment first (IDF_PYTHON_ENV_PATH).' >&2
  exit 1
fi
# Reads identity/capacity only. May reset the board; does not write flash.
"$python_bin" -m esptool --port "$port" chip-id
"$python_bin" -m esptool --port "$port" flash-id
