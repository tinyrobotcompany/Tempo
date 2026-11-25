# Protected production firmware

Production requires two distinct signatures: RSA-3072 Secure Boot V2 on the application, and the existing P-256 signature on the OTA manifest. The manifest hashes the final signed application. Neither substitutes for the other.

The protected `production-ota` environment must supply:

| Input | Meaning |
|---|---|
| Secret `SECURE_BOOT_SIGNING_PRIVATE_KEY` | Approved RSA-3072 firmware signing key, distinct from device and manifest keys |
| Variable `SECURE_BOOT_PUBLIC_KEY_SHA256` | SHA-256 of its DER SubjectPublicKeyInfo public key, checked against manufacturing policy |
| Variable `TEMPO_IDENTITY_HMAC_KEY_ID` | Explicit provisioned/locked HMAC_UP slot, 0–5, separate from Secure Boot and flash-encryption keys |
| Variable `TEMPO_ENROLLMENT_URL` | Deployed public HTTPS enrollment endpoint ending `/v1/device` |
| Variable `TEMPO_SECURE_VERSION` | Explicit hardware anti-rollback security version, 0–16 |
| Secret `OTA_SIGNING_PRIVATE_KEY` | Existing P-256 manifest key matching `ota_public.pem` |

Do not generate a production key merely to make CI pass. Manufacturing must provision the corresponding Espressif Secure Boot digest and retain a controlled recovery/key-rotation procedure. The public-key fingerprint above is **not** the eFuse digest encoding; use Espressif tooling for that digest.

`scripts/production-release.py` creates an isolated configuration from the board baseline. It requires Secure Boot V2/RSA, signed binaries, flash encryption in release mode, rollback, anti-rollback, protected identity and explicit enrollment. After the SDK build it checks the resolved configuration, app descriptor security version and slot bound, then runs Espressif's signature verifier. Missing credentials/settings, unsupported Kconfig combinations, wrong keys and unsigned images stop publication. The temporary signing-key file is removed on step exit. Private keys and configuration files are not release assets.

Production firmware and manifests use `ab-8mb-protected-v1`. Development firmware retains `ab-8mb-https-v1` and rejects production packages. Ordinary OTA cannot perform the factory security migration. The signed bootloader requires the partition table at 0x10000. Production moves default NVS/OTA metadata/PHY partitions; application slots and phone_tls retain their offsets. Default NVS is HMAC-encrypted, covering Wi-Fi, pairing and transport credentials as well as settings. The locked factory key is checked before default NVS initialization can attempt to generate a key. This is a factory migration, not an automatic in-place conversion: obtain explicit approval, recover settings through controlled application interfaces or re-enter them, revoke old phone pairing, and remove legacy plaintext remnants during the approved factory reflash. Do not preserve old plaintext sectors or silently erase NVS on an initialization error. Migration requires an explicitly approved USB/factory procedure with compatible signed bootloader, partition table, encryption, HMAC slot and application. Never flash a test-key build to a production unit or burn eFuses as a side effect of CI validation.

Security version is independent of semantic version. Do not increment it on every release: accepting a higher security version advances irreversible eFuse state and can remove earlier recovery images. Test successive ordinary updates at the same security version before separately authorizing an increment.

Acceptance on a dedicated provisioned unit must record hardware revision and public-key/digest identity, locked security state, first enrollment, trusted phone HTTPS, two successive signed updates, rejection of unsigned/wrong-key images, recovery while HTTPS/enrollment is unavailable, and interrupted-write rollback. A successful test-key SDK build does not establish these hardware outcomes.

References: [Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/secure-boot-v2.html) and [flash encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/flash-encryption.html).
