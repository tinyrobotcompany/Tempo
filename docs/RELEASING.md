# Tempo release strategy

## Automatic signed application releases

A firmware-related merge into main starts **Publish OTA Release** (`ota-release.yml`). Required host tests, 80% per-file coverage, firmware build/memory checks and security jobs must pass before signing/publication. PR builds do not access signing secrets or publish firmware.

There is one production channel. Devices normally check every **30 days**; hourly checking is an explicit temporary testing setting. Checking, downloading and scheduled installation are separate operations. Read [OTA operation, credential migration and physical acceptance](OTA.md) before the first bootstrap.

## Versions and release notes

Each application PR adds `.changeset/<description>.md`, with `"tempo": patch`, `minor` or `major`, and user-visible notes, compatibility and known limitations. The release job computes the next semantic version using the latest published/tagged version and the new notes since that tag. Dependency-only changes default to patch. It builds the reviewed source commit with `TEMPO_RELEASE_VERSION` and refuses an artifact whose ESP application descriptor does not match.

Tags point to reviewed source commits; generated release metadata does not bypass main's PR rules. `firmware/CMakeLists.txt` is the local-development baseline, while the GitHub release tag, signed manifest and installed image are the authority for published versions. The pipeline records the source SHA in `release.json` and release notes. GitHub Releases are the published changelog; `.changeset` retains the source notes. Never invent historical releases or move a published tag.

## Signing and publishing

The private signing key is an encrypted GitHub environment secret named `OTA_SIGNING_PRIVATE_KEY`, in the **production-ota** environment restricted to the `main` branch. Its matching public key is committed in `firmware/main/ota_public.pem`. No production PRIM or GitHub credential is supplied to the firmware build. The private key must be backed up securely by the maintainer; losing it can require a USB trust bootstrap. See OTA.md for deliberate key-rotation sequencing.

The serialised job creates a draft, attaches firmware, a signed manifest, signature, checksums, dependency lock and build/source metadata, then publishes the complete release with notes. Published packages must be immutable; reruns only complete existing drafts or exit if the release is already published. An older source commit cannot allocate a release after a newer tagged commit. Failed validation/signing leaves no installable release.

GitHub release assets provide downloads without separate website hosting or a device-side PAT. The public repository is part of this distribution assumption. A fleet service/telemetry is not implemented.

## Secure Boot v2 signed firmware (locked boards)

Locked boards run Secure Boot v2 and flash encryption in **Development** mode. They boot and install only application images signed with the Secure Boot key, so every published image is built from the production profile and signed twice:

1. **Secure Boot signature** (RSA-3072, RSA-PSS) over the application image. Espressif recommends RSA on ESP32-P4 (about 14.8 ms to verify, against about 61.1 ms for ECDSA-P256) and marks ECDSA Secure Boot v2 on this chip as not recommended. Source: [ESP32-P4 Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/security/secure-boot-v2.html).
2. **OTA manifest signature** (ECDSA P-256, `firmware/main/ota_public.pem`), unchanged and still checked separately by the device.

`scripts/secure-build.py` layers `firmware/sdkconfig.defaults`, `sdkconfig.rev3` and `sdkconfig.production` into a separate generated sdkconfig (the committed development `firmware/sdkconfig` is not used or changed). `check-config` runs after configuration and again after compiling, and fails the build unless every guard below holds. `sign` signs the app and the bootloader with `espsecure sign-data --version 2`, verifies them with the derived public key, and proves that a one-bit tampered image and an image signed by a fresh disposable key are both rejected. Only then does the workflow sign the OTA manifest; `ota-release.py package` also refuses an image without a matching Secure Boot v2 signature block.

Secrets and variables, all in the main-only **production-ota** environment:

- `SECURE_BOOT_SIGNING_KEY` (secret): the RSA-3072 Secure Boot private key in PEM form. It is written to a runner temporary file only for the signing step and deleted on exit. Losing it means locked boards can never be updated again; keep an offline backup.
- `TEMPO_IDENTITY_HMAC_KEY_ID` (variable, not secret): the eFuse key block that holds the factory HMAC_UP identity key. It must match the factory lockdown for every board.
- `OTA_SIGNING_PRIVATE_KEY` (secret): unchanged.

### Required production options

These are the options `scripts/secure-build.py check-config` enforces (`REQUIRED` and `FORBIDDEN` in the script). The factory lockdown depends on this list.

Must be set:

- `CONFIG_SECURE_BOOT=y`, `CONFIG_SECURE_BOOT_V2_ENABLED=y`, `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y`
- `CONFIG_SECURE_SIGNED_ON_BOOT=y`, `CONFIG_SECURE_SIGNED_ON_UPDATE=y` (OTA images are signature-checked before the boot slot changes)
- `CONFIG_SECURE_FLASH_ENC_ENABLED=y`, with exactly one of `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y` (current profile) or `..._RELEASE=y`; the profile also sets `CONFIG_SECURE_FLASH_ENCRYPTION_KEY_SOURCE_EFUSES=y`, `CONFIG_SECURE_FLASH_ENCRYPTION_AES128=y` and `CONFIG_SECURE_INSECURE_ALLOW_DL_MODE=y` (USB download stays available in Development mode)
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y`, `CONFIG_BOOTLOADER_APP_SECURE_VERSION` between 1 and `CONFIG_BOOTLOADER_APP_SEC_VER_SIZE_EFUSE_FIELD` (profile: 1 of 16). The application descriptor's secure version must match.
- `CONFIG_NVS_ENCRYPTION=y`, `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y` (keys in the encrypted `nvs_keys` partition; the HMAC scheme is avoided because firmware would burn an eFuse key if the slot were empty)
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-secure.csv"`, `CONFIG_PARTITION_TABLE_OFFSET=0x10000`
- `CONFIG_TEMPO_PRODUCTION_IDENTITY=y`, `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID` 0-5 (never -1), `CONFIG_TEMPO_ENROLLMENT_URL` an HTTPS `.../v1/device` URL (profile: the `enrollmentUrl` output of `infra/certificates/main.bicep`)

Must not be set: `CONFIG_SECURE_BOOT_INSECURE`, `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` (the key never enters the build), `CONFIG_BOOTLOADER_EFUSE_SECURE_VERSION_EMULATE`, `CONFIG_EFUSE_VIRTUAL`, `CONFIG_SECURE_BOOT_ALLOW_JTAG`, `CONFIG_SECURE_BOOT_V2_FORCE_ENABLE_ECDSA`.

Espressif defaults kept: `CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS` and `CONFIG_SECURE_FLASH_REQUIRE_ALREADY_ENABLED` are not set. On its first boot the bootloader enables any protection not already enabled and then write-protects read-protection, so the HMAC identity key must be burned and read-protected **before** the first signed boot.

### Layout `ab-8mb-secure-v1`

`firmware/partitions-secure.csv`: the signed bootloader (0x2000) is too large for the old 0x8000 partition table, so the table moves to 0x10000 and NVS/otadata/phy follow it (0x11000, 0x17000, 0x19000). A new encrypted `nvs_keys` partition sits at 0x1a000. `ota_0` (0x20000), `ota_1` (0x820000) and `phone_tls` (0x1020000) keep their offsets and sizes. Firmware built with Secure Boot v2 and flash encryption reports layout `ab-8mb-secure-v1`; unlocked builds keep `ab-8mb-https-v1` and `firmware/partitions.csv`, so neither accepts the other's OTA packages. Moving to the new layout needs one USB flash during lockdown; NVS contents (Wi-Fi, settings, PRIM key) start empty and must be provisioned again.

### Local signed build for the one-time USB flash

On the Mac, activate the pinned ESP-IDF 6.1.1 environment (as for `scripts/idf.sh`), then:

```sh
python3 scripts/secure-build.py build --signing-key /absolute/path/outside/the/repo/secure_boot_signing_key.pem --hmac-key-id <factory HMAC slot>
```

The key is read in place from the path you pass. The command refuses a key stored inside this checkout, never copies it, and never flashes or burns eFuses. It compiles into `build-host/secure-build/` and writes the signed set to `build-host/secure-release/`: `bootloader.bin` (offset 0x2000), `partition-table.bin` (0x10000), `ota_data_initial.bin` (0x17000), `weather_station.bin` (0x20000) and `SHA256SUMS`. Flashing and eFuse steps belong to the factory runbook. `python3 scripts/secure-build.py check-config <sdkconfig>` checks any generated sdkconfig on its own.

## Initial activation

1. Configure the main-only signing environment: `OTA_SIGNING_PRIVATE_KEY`, `SECURE_BOOT_SIGNING_KEY` and the `TEMPO_IDENTITY_HMAC_KEY_ID` variable.
2. Merge the OTA implementation after review/checks and acceptance planning; its firmware changes trigger the first release automatically.
3. Verify the GitHub release contains matching notes and all six update/metadata assets.
4. Lock the board down and USB-install the signed `ab-8mb-secure-v1` bootstrap (see above) with the same OTA public key. Provision PRIM once into device NVS.
5. Validate one complete update and rollback/power-cut matrix on hardware before sale or claiming production qualification. Restore monthly checks after testing.

No release, merge or device flash is implied by local source changes. Do not distribute older binaries containing a compiled transport key.

## Legacy source-only tooling

`scripts/release.py prepare` and `release.yml` remain available for deliberately prepared `source-vX.Y.Z` source-only tags (for example `source-v0.8.16`). Automatic OTA uses `vX.Y.Z`; those tags never trigger the legacy source workflow. They are not the automatic OTA publication path. They require matching checked-in versioned notes and create a draft that a maintainer must explicitly publish. Do not mark a source-only release as the latest production OTA release: it has no signed package for devices. New firmware releases should use the automatic workflow above.

If release automation lands before the runtime prerequisite, CI/security still validate the commit and the readiness job reports publication inactive. No signing or publication runs. A later firmware merge reevaluates readiness automatically; the build retains the same prerequisite checks as defense in depth.
