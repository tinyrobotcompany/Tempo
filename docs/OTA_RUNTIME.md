# OTA runtime and credential migration

This PR implements the device side. The separate release automation PR must land afterwards before automatic signed GitHub publication is available. No hardware acceptance is claimed by host tests.

Checks use one production channel: every 720 hours plus up to one hour of jitter; explicit hourly testing adds up to five minutes. Manual checks are immediate when connected with trusted time, except provider rate limits. Failed checks back off 15 minutes, one hour, then six hours. Saved settings and successful check deadlines survive restart.

Settings → Software updates and the paired phone page show progress. Automatic installation defaults to 03:00–04:00 local time; the phone can disable or change it. Installation has a 60-second countdown; Postpone persists a 24-hour delay.

The firmware verifies a signed ECDSA P-256 manifest, target/layout/version, image size and SHA-256 before activating the inactive 8 MB slot. Trial-boot health checks support rollback. This does not enable hardware Secure Boot or update the bootloader, partitions or C6.

## Bootstrap

Use one flashing owner and stop USB watchers. Install over USB with the rollback-enabled bootloader and existing A/B partition layout. Record the installed version and ensure a later published version is strictly newer for OTA acceptance.

The build no longer reads `.env`. Provision the PRIM key in paired phone setup under Transport API access, or run `python3 scripts/provision-transport-key.py --env /absolute/path/to/private/.env` and enter the current QR URL at its hidden prompt. Transport uses the separate NVS namespace `tempo_secret`; updates do not erase it. Pairing authenticates requests but local HTTP is not encrypted. Use a trusted LAN. NVS encryption and protection against physical extraction are not claimed.

## Acceptance

Run `bash scripts/test-all.sh` and the ESP32-P4 build. Then verify credential/settings persistence, progress, postponement, successful newer-image installation and retained transport access. Controlled hardware tests must cover network loss, power cuts during download/activation/trial boot, and rollback without a reboot loop. Return to 30-day checks afterwards. Do not flash while the display-idle-controls session owns the device.
