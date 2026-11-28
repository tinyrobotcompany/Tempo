# Factory runbook: lock down and register a Tempo board

This runbook takes one ESP32-P4 board (Waveshare 10.1-inch P4, rev 3, 32 MB) from
"prototype" to "locked and registered". It turns on Secure Boot v2 and flash
encryption in **Development** mode, burns the identity HMAC key, flashes the
signed firmware, reads the device's PUBLIC identity key and registers it.

Do one step at a time. Every step says what you should see. If you see
something else, stop and go to [Failed-unit recovery](#7-failed-unit-recovery).

Steps marked **⚠️ IRREVERSIBLE** change eFuses. eFuses are one-time switches in
the chip: once burned they can never be un-burned.

## What the tools do

| Tool | What it does | Touches the board? |
| --- | --- | --- |
| `scripts/factory-backup.sh` | Copies the whole flash to a file and proves the copy is complete. Can restore it. | Only with `--execute`, and only after you type the confirmation for each step. |
| `scripts/factory-lockdown.py` | Checks the build, then burns the eFuses in Espressif's order and flashes the signed firmware. Also reads the identity key from a boot log. | Only with `--execute`. Each step needs its own typed phrase. espefuse then asks you to type `BURN` again. |
| `scripts/factory-register.sh` | Adds the PUBLIC identity key to the cloud `DeviceRegistry`. | No. It talks to Azure only. |

Without `--execute`, both device tools only print what they would do (dry run).
No private key is ever printed, sent over serial or saved. The flash-encryption
and HMAC keys are made inside `factory-lockdown.py` and passed to espefuse
through a pipe. They never exist as files, so there is nothing to delete.

## Before you start

You need:

- The board, its USB cable and the power supply from the kit.
- A Mac with the pinned ESP-IDF 6.1.1 installed.
- The **signed** production build. The firmware owner makes it with
  `python3 scripts/secure-build.py build --hmac-key-id 5 --signing-key /outside/the/repo/release.pem`
  (see `docs/RELEASING.md`). This writes the compiled build to
  `build-host/secure-build/` and the signed images plus `SHA256SUMS` to
  `build-host/secure-release/`. These are the lockdown script's default folders.
  Use `--build-dir` and `--signed-dir` if yours are elsewhere.
- Two Secure Boot **public** key files (`.pem`): the release key, and the
  independent backup key from step 2. You never need either private key on this computer.
- A serial number for the unit, for example `P4-PROTO-001`.

The build must use layout `ab-8mb-secure-v1` (`firmware/partitions-secure.csv`).
Its offsets are: bootloader at `0x2000` (the signed bootloader is about `0xD000`
bytes), partition table at `0x10000`, NVS at `0x11000`, otadata at `0x17000`,
encrypted `nvs_keys` at `0x1A000`, and app at `0x20000`.

`factory-lockdown.py` checks the following and refuses to start if any is wrong:

1. Secure Boot v2 with RSA-3072 keys (`SECURE_SIGNED_APPS_RSA_SCHEME`). Espressif
   says ECDSA Secure Boot "is not functional for certain input vectors" on the ESP32-P4.
2. Flash encryption on, in Development mode, with XTS-AES-128 keys stored in eFuses.
   `SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC=y`.
3. `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=5`, the eFuse block this runbook uses for the HMAC key.
4. `CONFIG_TEMPO_PRODUCTION_IDENTITY=y`, NVS encryption with flash-encryption key
   protection, `partitions-secure.csv` and `PARTITION_TABLE_OFFSET=0x10000`.
5. ROM download mode not permanently disabled. Otherwise USB re-flashing would stop working.
6. None of these: Release-mode flash encryption, a Key Manager key source,
   `SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS`, `SECURE_BOOT_INSECURE`,
   `SECURE_BOOT_BUILD_SIGNED_BINARIES`, `SECURE_BOOT_ALLOW_JTAG`, `EFUSE_VIRTUAL`,
   or forced ECDSA.
7. Every signed image matches `SHA256SUMS`, and the bootloader and app are signed
   by the release key.
8. `encrypted_flash_args` names exactly the four signed images. The bootloader is
   at `0x2000` and the partition table at `0x10000`, and no image overlaps the next.
   The script flashes the signed copies from `build-host/secure-release/`, never
   the unsigned compiler output.
9. Both public keys are RSA-3072 and are different keys.

It prints a `NOTE` if the recommended `SECURE_FLASH_REQUIRE_ALREADY_ENABLED` is
missing. That option makes the bootloader refuse to start on a board that has
not been locked first.

### Where each key goes (ESP32-P4 eFuse layout)

The P4 has six key blocks, `BLOCK_KEY0` to `BLOCK_KEY5`. Other chips have a
different layout, so this table is for the P4 only.

| Block | Purpose | Read-protected | Source |
| --- | --- | --- | --- |
| `BLOCK_KEY0` | `XTS_AES_128_KEY` (flash encryption) | yes | random, made on the host for this unit only, never stored |
| `BLOCK_KEY2` | `SECURE_BOOT_DIGEST0` (hash of the release public key) | no (the ROM must read it) | `espsecure digest-sbv2-public-key` |
| `BLOCK_KEY3` | `SECURE_BOOT_DIGEST1` (hash of the backup public key) | no | `espsecure digest-sbv2-public-key` |
| `BLOCK_KEY5` | `HMAC_UP` (protects the identity storage) | yes | random, made on the host for this unit only, never stored |
| `BLOCK_KEY1`, `4` | left empty | | |

Secure Boot slot 3 (`SECURE_BOOT_KEY_REVOKE2`) is revoked. Slots 1 and 2 stay
usable, so firmware signed by either the release key or the backup key will boot.

The NVS encryption keys in the `nvs_keys` partition are not eFuses. The firmware
generates them on the chip on its first boot, into the empty `nvs_keys`
partition, and flash encryption protects them. This is why the erase in step 3.3 matters.

**Why the HMAC key is made on the host.** ESP-IDF's own NVS HMAC scheme can make
the key on the chip at first boot ("If no key is found, a key is generated
internally"). Tempo's firmware deliberately never burns eFuses
(`firmware/main/identity_store.c`), so using that scheme would mean changing a
reviewed security rule. This runbook keeps the firmware rule. It makes a fresh
random key for each unit and passes it to espefuse through a pipe, so the key is
never on disk. espefuse read-protects `HMAC_UP` keys by default, so software
cannot read the key back. Only the HMAC hardware can use it.

## 0. Practise first (no board needed)

Do this once before your first real board. It runs the whole eFuse sequence
against a pretend chip.

0.1. Open Terminal. Go to the repository and activate ESP-IDF:
`source ~/.espressif/tools/activate_idf_v6.1.sh`.
You should see the ESP-IDF environment message. If you see "No such file", install ESP-IDF 6.1.1 first.

0.2. Create a blank pretend P4:
`python scripts/factory-lockdown.py --virtual-efuse build-host/factory-practice/efuse.bin --init-virtual`
You should see `Created a blank virtual esp32p4 v3.0 eFuse file`.

0.3. Run the full sequence on it:
`python scripts/factory-lockdown.py --virtual-efuse build-host/factory-practice/efuse.bin --record-dir build-host/factory-practice --signing-public-key RELEASE-public.pem --backup-signing-public-key BACKUP-public.pem --execute`
Type each phrase it asks for. Esptool steps are skipped. At the end you should see `eFuses match the plan.`
If anything else appears, stop and report it. Delete `build-host/factory-practice` afterwards.

## 1. Back up the board

1.1. Connect the board to the Mac with USB. Plug in the kit power supply.

1.2. Find the port: `ls /dev/cu.usb*`. You should see one name such as
`/dev/cu.usbmodem5C940599581`. If you see none, try another cable. If you see
several, unplug the board, run the command again and note which name went away.

1.3. Dry run: `bash scripts/factory-backup.sh --port PORT --unit p4-proto-1`.
You should see `DRY RUN` and four commands. Nothing is sent to the board.

1.4. Real backup: run the same command with `--execute` added at the end.
Type `yes` at each of the four prompts. Reading 32 MB takes several minutes.
At the end you should see `Backup verified: build-host/factory-units/p4-proto-1-<time>`.
Write down this folder name. The rest of this runbook calls it `UNIT`.

1.5. Check the USB connection type. Run `cat UNIT/manifest.json`.
`"usb_mode"` must be `"USB-Serial/JTAG"` or `"UART"`. If it is `"USB-OTG"`, stop.
Turning on Secure Boot or flash encryption turns off the ROM's USB-OTG download
port, so you could lose USB flashing. Ask for review.

1.6. Copy the `UNIT` folder to a second, private place, such as an encrypted USB
stick. The backup can contain Wi-Fi and API credentials, so never commit or share it.

**How to restore it** (only works before step 3.8 turns on flash encryption):
`bash scripts/factory-backup.sh --restore UNIT --port PORT --execute`.
Type `yes`, then `RESTORE FLASH`, then `yes`. The script first checks that flash
encryption and Secure Boot are still off. It refuses to restore a backup that
has changed since it was taken. Once step 3.8 has run, the backup can no longer
boot. It is then only a private record of the old data.

## 2. Signing keys: create the backup key, confirm both backups

Once Secure Boot is on, the board only runs firmware signed by the release key
or the backup key. If both private keys are lost, no new firmware can ever be
installed on this unit. The backup key is your way back if the release key is
lost or leaked.

2.1. You only do this once for all units. Skip it if the backup key already exists.
On a computer that is not this repository's checkout (ideally offline), with
ESP-IDF activated, create the backup key:
`espsecure generate-signing-key --version 2 --scheme rsa3072 tempo-secure-boot-backup.pem`.
You should see `RSA 3072 private key in PEM format written to ...`.

2.2. Make its public half: `openssl pkey -in tempo-secure-boot-backup.pem -pubout -out tempo-secure-boot-backup-public.pem`.
Copy only the `-public.pem` file to the factory Mac.

2.3. Store the backup **private** key separately from the release key: a
different password-manager vault or a different safe. Do not put it in GitHub,
in this repository, or anywhere next to the release key. Do not use it to sign
releases.

2.4. Get the release key's public file (`RELEASE-public.pem`) from the firmware
owner. The release private key's backup is the owner's offline copy described
in `docs/RELEASING.md`.

2.5. Check both backups open, and are the keys whose fingerprints step 3.2 prints.
The script refuses to start unless both public keys are given and they are different keys.

## 3. Lock the board down (eFuse burn order)

The order follows Espressif's guide: flash encryption first, then Secure Boot.
Secure Boot's final step (write-protecting `RD_DIS`) stops any further key from
being read-protected, so all secret keys must be burned before it.

3.1. Dry run with your inputs:
`python scripts/factory-lockdown.py --signing-public-key RELEASE-public.pem --backup-signing-public-key BACKUP-public.pem --record-dir UNIT`
You should see `Host checks passed` and 12 numbered steps, nine marked `IRREVERSIBLE`.
(A build that uses Secure Download Mode adds a last 13th step, which blocks espefuse afterwards.)
If you see `REFUSED`, the message names what is wrong with the build or key. Do not continue.

3.2. Start the real run:
`python scripts/factory-lockdown.py --signing-public-key RELEASE-public.pem --backup-signing-public-key BACKUP-public.pem --record-dir UNIT --port PORT --execute`
It prints the two keys' fingerprints and asks you to type
`I HAVE VERIFIED OFFLINE BACKUPS OF <release> AND <backup>`. Type it only if step 2 is done.
It then reads the eFuses. It stops if this board is not rev 3 or newer, or if
any eFuse it needs is already used.

For each step below the script shows the exact command and the phrase to type.
For burn steps, espefuse then asks you to type `BURN`. Typing anything else
stops the step, and nothing is changed.

3.3. Step 1, **erase flash**. Type `ERASE FLASH`. You should see `Chip erase completed successfully`.
This can be undone only from the backup made in step 1.

3.4. Step 2, **⚠️ IRREVERSIBLE**. Burns the flash-encryption key into `BLOCK_KEY0`. Type `BURN BLOCK_KEY0 XTS_AES_128_KEY`, then `BURN`.

3.5. Step 3, **⚠️ IRREVERSIBLE**. Burns the HMAC identity key into `BLOCK_KEY5`. Type `BURN BLOCK_KEY5 HMAC_UP`, then `BURN`.

3.6. Step 4, **⚠️ IRREVERSIBLE**. Burns the release key's digest into `BLOCK_KEY2`. Type `BURN BLOCK_KEY2 SECURE_BOOT_DIGEST0`, then `BURN`.

3.7. Step 5, **⚠️ IRREVERSIBLE**. Burns the backup key's digest into `BLOCK_KEY3`. Type `BURN BLOCK_KEY3 SECURE_BOOT_DIGEST1`, then `BURN`.

3.8. Step 6, **⚠️ IRREVERSIBLE**. Turns on flash encryption in Development mode (`SPI_BOOT_CRYPT_CNT = 1`). Type `BURN SPI_BOOT_CRYPT_CNT 1`, then `BURN`.
From now on the old plaintext backup cannot boot.

3.9. Step 7, **⚠️ IRREVERSIBLE**. Turns off JTAG debugging, direct boot and
download-mode cache access (`DIS_DIRECT_BOOT`, `DIS_PAD_JTAG`, `DIS_USB_JTAG`,
`SOFT_DIS_JTAG`, `SPI_DOWNLOAD_MSPI_DIS`). These are the eFuses the ESP-IDF 6.1
P4 bootloader would burn itself in Development mode. The one exception is
`DIS_DOWNLOAD_MANUAL_ENCRYPT`, which this runbook leaves unburned so that USB
re-flashing keeps working. Type `BURN HARDENING`, then `BURN`.
What still works afterwards is explained in [the USB evidence below](#usb-console-and-flashing-after-step-7).

3.10. Step 8, **⚠️ IRREVERSIBLE**. Revokes Secure Boot key slot 3, which is not
used. Slots 1 and 2 (release and backup) stay valid. Type `BURN REVOKE UNUSED`, then `BURN`.

3.11. Step 9, **⚠️ IRREVERSIBLE**. Turns on Secure Boot v2. Type `BURN SECURE_BOOT_EN`, then `BURN`.

3.12. Step 10, **⚠️ IRREVERSIBLE**. Write-protects `RD_DIS`. This is the last eFuse change. Type `PROTECT RD_DIS`, then `BURN`.

3.13. Step 11, verification. Type `yes`. You should see `eFuses match the plan.`
The script checks:
- each key's purpose and its read and write protection;
- both digest values;
- every security eFuse;
- that slots 1 and 2 are not revoked;
- that `DIS_DOWNLOAD_MANUAL_ENCRYPT` is still 0.

If it lists problems, **do not flash**. Go to section 7.

### USB console and flashing after step 7

Step 7 removes JTAG debugging only. The USB serial console and USB (encrypted)
flashing keep working. The evidence:

- The ESP-IDF 6.1 ESP32-P4 eFuse table (`components/efuse/esp32p4/esp_efuse_table.csv`)
  describes `DIS_USB_JTAG` as "whether the function of usb switch to jtag is
  disabled". USB download and console have their own eFuses,
  `DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` and `DIS_USB_SERIAL_JTAG_ROM_PRINT`. This
  runbook never burns either of them.
- `SPI_DOWNLOAD_MSPI_DIS` only stops the AXI bus from reading flash or PSRAM
  while in download mode. Flash writes do not go through that path.
- Espressif's [flash encryption guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/flash-encryption.html)
  lists `DIS_PAD_JTAG`, `DIS_USB_JTAG`, `DIS_DIRECT_BOOT` and `DIS_DOWNLOAD_MSPI`
  as burned by default on the first Development-mode boot. It then documents
  re-flashing in Development mode ("the second stage bootloader allows the UART
  bootloader to re-flash encrypted binaries", using `idf.py encrypted-flash monitor`).
  It says the only eFuse that blocks this is `DIS_DOWNLOAD_MANUAL_ENCRYPT`.
- Espressif's [USB Serial/JTAG console page](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/usb-serial-jtag-console.html)
  says the controller "is able to put the ESP32-P4 into download mode automatically".
- The [security overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/security.html)
  says Secure Download Mode "gets activated if any of the security features are
  enabled". In ESP-IDF 6.1 that only happens in Release mode, or with
  `SECURE_ENABLE_SECURE_ROM_DL_MODE`. `SECURE_ROM_DL_MODE_ENABLED` defaults to
  `!SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT`, and the production profile sets
  `SECURE_INSECURE_ALLOW_DL_MODE`.

Not yet checked: the ESP32-P4 Technical Reference Manual could not be fetched.
Treat the first real flash and console output in sections 4 and 5 as the
hardware confirmation.

If any step fails or you stop, run the step 3.2 command again. Steps already
done are detected from the eFuses and skipped.

## 4. First signed flash

4.1. Step 12. Type `FLASH SIGNED`. esptool writes the four signed images from
`build-host/secure-release/` at the offsets listed in
`build-host/secure-build/encrypted_flash_args`: bootloader `0x2000`, partition
table `0x10000`, otadata `0x17000`, app `0x20000`. It encrypts them on the chip as
it writes. You should see `Hash of data verified` for each file.

4.2. Press the board's reset button, or unplug and replug the power.

4.3. **⚠️ IRREVERSIBLE, automatic.** On this first boot the firmware does two things by itself:
- it generates the NVS encryption keys into `nvs_keys`;
- the anti-rollback feature records secure version 1 in eFuse.

After this, firmware with a lower secure version will never boot on this unit.

## 5. Verify, then read the public key

5.1. Save the boot log: `python scripts/factory-lockdown.py --capture-log UNIT/boot.log --port PORT`.
Type `yes`, then press reset within a few seconds. It records 40 seconds.

5.2. Check the log: `grep -E "secure boot|flash encryption|Flash encryption mode" UNIT/boot.log`.
You should see lines saying Secure Boot v2 is already enabled, flash encryption
is enabled, and `Flash encryption mode is DEVELOPMENT`. If the log is empty or
shows `secure boot check fail` or `invalid header`, go to section 7.

5.3. Security summary (read-only): `esptool --chip esp32p4 --port PORT get-security-info`.
It should show Secure Boot enabled and flash encryption enabled. Afterwards,
reset the board again.

5.4. Extract the identity: `python scripts/factory-lockdown.py --extract-identity UNIT/boot.log --out UNIT`.
You should see `Device ID`, `Hostname` (`d-<id>.tempo.tinyrobotcompany.io`), and
one long line of base64 text. It saves `UNIT/identity-public.pem`. This is a
public key and is safe to share. If it says it found 0 identities, the firmware
did not start its identity storage. Check `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=5`,
then go to section 7. If it mentions `PRIVATE`, stop and report it straight away.

5.5. **Set the board up again.** Lockdown erased the flash, and NVS is now
encrypted and starts empty. Re-enter everything on the device:
- **Wi-Fi:** on the touchscreen, open Settings, then Wi-Fi / time, and enter the network and password.
- **Settings:** clock style, brightness, colours and view rotation, on the touchscreen.
- **Phone pairing and location:** follow `docs/IPHONE_SETUP.md`.
- **PRIM transport key:** after pairing, run `python3 scripts/provision-transport-key.py`
  (see `docs/OTA.md`). Read the key from your private `.env`; never type it into a log or a commit.

The old backup does not help here. It is plaintext and cannot be restored onto
an encrypted unit.

## 6. Register the device

6.1. In GitHub, open Actions, then **Factory device registration**, then **Run workflow** on `main`.

6.2. Paste the base64 line into `public_key_base64`, the serial into `serial`
and the device ID into `device_id`. Start it.

6.3. Approve the `tempo-infrastructure` environment when asked.

6.4. You should see a green run with `Registered device <id>`. A red run that
mentions `device_already_registered` means this key is already registered. If it
is the same unit, that is fine.

(The same script can run from the Mac with `--i-am-the-operator`, but the
workflow is the normal route.)

### Check the device is in DeviceRegistry

The registry table is in a private network, so you cannot read it from your
laptop. That is deliberate. Check it like this instead:

6.5. A green run means the factory job created the registry row. The job fails
if the row is not written.

6.6. Optional (read-only). The first time, `az` may install its `containerapp`
CLI extension automatically. That changes your local Azure CLI only, not Azure.
`az containerapp job logs show --subscription <id> -g rg-tempo-westeurope-compute -n tempo-westeurope-factory --container factory --execution <execution from the run>`.
It should print `d-<id>.tempo.tinyrobotcompany.io`, matching step 5.4. Logs can
disappear once the job pod is cleaned up.

6.7. The final proof is the device itself. When it is on Wi-Fi it enrols, and
its HTTPS name starts working. HTTP 401 from the service means the device is
not in the registry, or is disabled there.

## 7. Failed-unit recovery

Keep the `UNIT` folder. It records every eFuse state (`efuse-*.json`).

- **You typed the wrong phrase, or a step failed.** Nothing more was burned. Run
  the step 3.2 command again. Steps already done are skipped. Never add
  `--force-write-always` to espefuse.
- **`REFUSED` before step 3.3.** Nothing was changed. Fix what the message says,
  such as the build options or the key, and start again.
- **Verification (step 3.13) failed.** Do not flash. Send the `UNIT` folder for
  review. Development mode leaves `SPI_BOOT_CRYPT_CNT` writable, but changing it
  is a separate, reviewed decision. Do not try it on your own.
- **The board does not boot after step 4.** Usually the wrong build was used. In
  Development mode you can flash again over USB: run the step 3.2 command again
  with the correct signed build. Only step 12 is left to do. A plaintext or
  unsigned image will never boot on this unit.
- **The identity is not printed.** Check that the build has
  `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID=5`, then flash the corrected build as in the
  previous item. The identity is created once and never replaced automatically.
  Recovering a missing or corrupt identity needs a separately reviewed procedure.
- **The release signing private key is lost or leaked.** The backup key in
  `SECURE_BOOT_DIGEST1` still works: firmware signed with it boots. Switching
  releases to the backup key (and revoking slot 1 if the key leaked) is a
  separately reviewed key-rotation procedure. If **both** private keys are lost,
  the unit keeps its current firmware but can never be updated.
- **The wrong key was registered.** Disable that registry entry, using the
  operator procedure in `docs/DEVICE_CERTIFICATE_SERVICE.md`, then register the right key.
- **USB-OTG was recorded in step 1.5.** Do not lock the unit down until this has been reviewed.

## Rehearsal chip (optional, later)

The scripts take `--chip`, but only `esp32p4` has a reviewed eFuse profile. They
refuse `esp32s3` and `esp32c6` until those layouts have been reviewed against
those chips' own Espressif pages. Until then, use the virtual eFuse practice in
section 0.

## Espressif references (ESP32-P4, ESP-IDF v6.1)

- [Security features enablement workflows](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/security-features-enablement-workflows.html): external enablement order, `burn-key` purposes, `SPI_BOOT_CRYPT_CNT`, `write-protect-efuse RD_DIS`, revocation.
- [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/secure-boot-v2.html): RSA-3072 recommended on the P4, three digest slots, unused slots revoked, USB-OTG ROM download disabled.
- [Flash encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/flash-encryption.html): XTS-AES key purposes, Development vs Release, `encrypted-flash`, `DIS_DOWNLOAD_MANUAL_ENCRYPT`.
- [HMAC](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/hmac.html): `HMAC_UP` purpose, key blocks 4 to 9 (`BLOCK_KEY0` to `BLOCK_KEY5`).
- [NVS encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/storage/nvs_encryption.html): HMAC-based scheme and on-chip key generation (not used, see above).
- [USB Serial/JTAG console](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/usb-serial-jtag-console.html) and [security overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/security/security.html): download mode over USB, and when Secure Download Mode applies.
- ESP-IDF 6.1 source: `components/bootloader_support/src/esp32p4/*_secure_features.c` (the eFuses the bootloader burns itself, which step 3.9 mirrors), `components/efuse/esp32p4/esp_efuse_table.csv` (P4 eFuse meanings) and `components/bootloader/Kconfig.projbuild` (download-mode defaults).
