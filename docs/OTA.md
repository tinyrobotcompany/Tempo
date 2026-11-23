# Signed updates and device credentials

## Status and ownership

The implementation uses one **production** release channel. It is not a development channel. The normal interval is **720 hours (30 days)** plus up to one hour of random spreading; the explicit **OTA testing** setting is one hour plus up to five minutes. Manual checks are immediate when Wi-Fi and trusted time are available, except server rate limits. Startup/reconnection checks only when due, after 30 seconds of stable connectivity. Failed checks back off 15 minutes, one hour, then six hours. Server Retry-After/rate-reset deadlines take precedence. Successful check timestamps and due dates survive restart in NVS.

Automatic installation is enabled by default in the 03:00–04:00 local-time window. Users can disable it or change the window on the paired phone page. Manual Install and automatic installation both provide a 60-second cancellation opportunity. Postpone defers installation for 24 hours and persists that choice. Changing check frequency does not immediately install an image. No forced critical-update override is implemented.

Screen: Settings → Software updates. Phone: Software updates. Both show status and progress. The phone also shows release notes, schedule settings and transport credential provisioning. The device continues normal rendering/network services while downloading; restart is the only intentional interruption.

## Trust and recovery

`ota_public.pem` is a public ECDSA P-256 key compiled into the bootstrap firmware. The maintainer’s owner-only local backup is `.secrets/ota-signing/private.pem` in the primary checkout (ignored locally and by Git); retain a secure independent backup before deleting a checkout. Its private counterpart is stored in the GitHub `production-ota` environment as `OTA_SIGNING_PRIVATE_KEY`, never in source or device binaries. The device verifies a detached SHA-256/ECDSA signature over the exact manifest bytes before trusting its hash, version, hardware identifier, slot size and artifact URL. It downloads the application into the inactive 8 MB slot, verifies the full image SHA-256 and ESP application descriptor, then selects the boot slot. This authenticates the image through the signed manifest; it does **not** enable hardware Secure Boot or burn eFuses.

Release metadata never follows redirects. Asset downloads start at this repository's github.com release URL and may then redirect only to GitHub's asset CDN hosts (never back to github.com or elsewhere); URLs with spaces, control characters or backslashes are rejected and certificates remain verified. Downloads, redirects, metadata size, image size and elapsed time are bounded. Bad signatures, corrupt/incompatible images and failed writes do not select a new boot partition. Existing ESP-IDF boot-health/rollback checks confirm local UI startup; internet/provider availability is not a condition for marking firmware healthy. Failed/interrupted activation is remembered, and that version is not automatically retried. Physical cut-power testing is still required.

The first bootstrap must be installed over USB with a rollback-enabled bootloader and the provisioned `ab-8mb-https-v1` partition layout. Existing `ab-8mb-v1` devices require the [controlled USB migration](SECURE_PHONE_ACCESS.md); they reject the new OTA layout. Ordinary OTA never updates the bootloader, partitions or ESP32-C6 coprocessor. Hardware target is the current Waveshare 10.1-inch P4 revision-3, 32 MB assembly. Different boards/layouts need separately identified packages.

Signing-key rotation requires a dedicated transition release signed by the old key that installs the next public key before changing the publication secret. Do not replace the GitHub secret/public key independently. Do not remove old signed releases needed for diagnosis/recovery.

## PRIM credential migration — required for the first bootstrap

The application no longer reads `.env` at compile time. `transport_config.h` and its generator are removed. All firmware builds are credential-free, including local builds and GitHub artifacts. Transport reads `tempo_secret/prim` from NVS for each request; empty/missing credentials produce the setup-required state. Replacement takes effect on refresh without reflashing. Normal settings, OTA and rollback do not erase this namespace.

After installing the bootstrap, scan its current setup QR and save the key in **Transport API access**, or use the existing private `.env` once:

```sh
python3 scripts/provision-transport-key.py --env /absolute/path/to/private/.env
```

The script prompts for the current QR URL without echo, requires HTTPS with certificate verification, rejects redirects and never prints the key or pairing token. The phone endpoint requires the current paired bearer token, accepts bounded JSON, commits before confirming, never returns the key, and clears the input after a successful save. Empty key via the provisioning API removes it. A successful save means persistence, not provider acceptance; the transport refresh tests provider access and shows missing/denied status.

**Security boundary:** phone control now requires a unique provisioned HTTPS identity and trusted certificate. See [HTTPS migration and limitations](SECURE_PHONE_ACCESS.md). NVS is not claimed encrypted or physically extraction-resistant. A sales credential model (customer-owned PRIM account or a credential-holding backend) is required before shipping; never preinstall a shared company key across customers. No old flashed binary should be distributed: it may still contain the formerly compiled credential.

## Automated releases

A firmware-related merge to main triggers **Publish OTA Release**. CI, minimum-80%-per-file coverage and security gates run before publication. The release job uses a main-only GitHub environment for signing. It allocates the next semantic version from the latest release and new change notes, builds that exact version through `TEMPO_RELEASE_VERSION`, verifies the image descriptor, signs and locally verifies the manifest, then uploads a complete draft before publishing it. Tags point to the reviewed source commit; no generated commit bypasses main's PR rules. `firmware/CMakeLists.txt` remains the local development baseline; published image/tag/manifest versions must agree.

Assets: `tempo.bin`, signed manifest and detached signature, checksums, dependency lock, release source metadata, and `notes.md` (also displayed as the GitHub release body). Versioned releases are immutable once published; draft retries clear existing assets, upload the complete set, then download and verify every file against its local hash before publication. Releases are serialised and older divergent source cannot publish over a newer release. No production API keys are available to the build. The package gate rejects invalid descriptors and known secret/build markers; runtime-only provisioning is the primary guarantee against credential embedding.

GitHub-hosted assets require no separate domain or web hosting. Devices use public downloads and contain no GitHub PAT. There is no fleet telemetry or instant server push; publication does not mean every offline device has installed it. GitHub availability affects update discovery, not normal Tempo operation.

## Acceptance and activation checklist

1. Run host suites and build the bootstrap with the production public key.
2. Install bootstrap over USB; provision the existing PRIM credential and verify transport.
3. Select hourly testing or press Check now. Never set customer defaults to hourly.
4. Publish two successive versions through the reviewed main workflow; check matching device versions and visible release notes.
5. Exercise invalid signature/hash, wrong target/layout, truncated download, provider timeout, redirected host rejection and NVS save errors.
6. Cut power during download, verification, activation and the trial boot. Confirm a valid old/new image and retained settings/credential, with no repeat rollback loop.
7. Verify dim/off/phone interaction during download, visible countdown and persistent postponement, then restore 30-day checks.
8. Record real hardware outcomes before treating OTA as production-qualified. Host tests do not establish electrical power-loss resilience.

Never flash concurrently from another checkout. The active display-idle-controls worktree has device ownership until that task completes; integration must preserve both branches' UI changes.
