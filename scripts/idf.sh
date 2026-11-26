#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "$PWD/.artifacts/esp-idf/export.sh" ]]; then
    source "$PWD/.artifacts/esp-idf/export.sh"
  elif [[ -f "$HOME/.espressif/tools/activate_idf_v6.1.sh" ]]; then
    original_path="$PATH"
    while IFS='=' read -r key value; do
      case "$key" in
        SYSTEM_PATH) ;;
        PATH) export PATH="$value:$original_path" ;;
        IDF_*|ESP_*|OPENOCD_SCRIPTS) export "$key=$value" ;;
      esac
    done < <(bash "$HOME/.espressif/tools/activate_idf_v6.1.sh" -e)
  else
    echo 'Activate the pinned ESP-IDF 6.1.1 checkout before running this script.' >&2
    exit 1
  fi
fi
python3 scripts/check-idf-security.py "$IDF_PATH"
exec python "$IDF_PATH/tools/idf.py" -C firmware "$@"
