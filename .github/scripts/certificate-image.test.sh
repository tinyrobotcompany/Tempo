#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/repo/scripts/infrastructure" "$tmp/repo/services/certificates/tempo_certificates" "$tmp/bin"
touch "$tmp/repo/services/certificates/Dockerfile"
cp "$root/scripts/certificate-image.sh" "$tmp/repo/scripts/"
cp "$root/scripts/infrastructure/check-context.sh" "$tmp/repo/scripts/infrastructure/"
cat > "$tmp/bin/git" <<'MOCK'
#!/usr/bin/env bash
if [[ "$1" == rev-parse ]]; then echo "$GITHUB_SHA"; else printf '%s\trefs/heads/main\n' "$GITHUB_SHA"; fi
MOCK
cat > "$tmp/bin/az" <<'MOCK'
#!/usr/bin/env bash
case "$1 $2" in
  'account show') if [[ "$4" == id ]]; then echo subscription; else echo tenant; fi ;;
  'deployment sub') echo "${MOCK_REGISTRY:-acrfresh12345.azurecr.io}" ;;
  'acr build')
    echo build >> "$BUILD_CALLS"
    [[ "$*" == *'--registry acrfresh12345'* && "$*" == *'--no-logs'* ]] || exit 90
    # Real az checks --file relative to the working directory before uploading.
    [[ "$*" == *'--file services/certificates/Dockerfile '* && -f services/certificates/Dockerfile ]] || exit 91
    [[ "${MOCK_FAIL:-0}" == 0 ]] || exit 1
    jq -n --arg status "${MOCK_STATUS:-Succeeded}" --arg digest "${MOCK_DIGEST:-sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa}" \
      --arg tag "$GITHUB_SHA-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT" \
      '{status:$status,outputImages:[{registry:"acrfresh12345.azurecr.io",repository:"tempo-certificates",tag:$tag,digest:$digest}]}' ;;
  *) exit 99 ;;
esac
MOCK
chmod +x "$tmp/bin/az" "$tmp/bin/git"
export PATH="$tmp/bin:$PATH" BUILD_CALLS="$tmp/builds"
export GITHUB_ACTIONS=true GITHUB_EVENT_NAME=push GITHUB_REF=refs/heads/main GITHUB_REPOSITORY=tinyrobotcompany/Tempo
export GITHUB_SHA=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa GITHUB_RUN_ID=123 GITHUB_RUN_ATTEMPT=1 GITHUB_STEP_SUMMARY="$tmp/summary"
export AZURE_SUBSCRIPTION_ID=subscription AZURE_TENANT_ID=tenant
run="$tmp/repo/scripts/certificate-image.sh"
bash "$run"
jq -e '.image == "acrfresh12345.azurecr.io/tempo-certificates@sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"' "$tmp/repo/build-host/certificate-image.json" >/dev/null
for scenario in invalid-registry failed-build failed-status invalid-digest wrong-branch waveform; do
  rm -f "$tmp/repo/build-host/certificate-image.json" "$BUILD_CALLS"
  case "$scenario" in
    invalid-registry) export MOCK_REGISTRY='$(id)' ;;
    failed-build) export MOCK_FAIL=1 ;;
    failed-status) export MOCK_STATUS=Failed ;;
    invalid-digest) export MOCK_DIGEST=latest ;;
    wrong-branch) export GITHUB_REF=refs/heads/feature/test ;;
    waveform) printf WAVEFORM_DEVICE_SUFFIX > "$tmp/repo/services/certificates/tempo_certificates/app.py" ;;
  esac
  if bash "$run"; then echo "Unexpected success: $scenario" >&2; exit 1; fi
  [[ ! -e "$tmp/repo/build-host/certificate-image.json" ]]
  if [[ "$scenario" == wrong-branch || "$scenario" == waveform || "$scenario" == invalid-registry ]]; then [[ ! -e "$BUILD_CALLS" ]]; fi
  unset MOCK_REGISTRY MOCK_FAIL MOCK_STATUS MOCK_DIGEST
  export GITHUB_REF=refs/heads/main
 done
echo 'PASS: dynamic registry, immutable image, invalid metadata, build failure and publication guards'
