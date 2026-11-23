# HTTPS phone access: prototype and production boundary

This branch implements HTTPS-only phone control and a USB-provisioned, unique device
identity. It is a **prototype migration**, not a deployed certificate service. It
must not be merged/released as an ordinary upgrade until the USB migration and
real-device acceptance below are complete. No physical eFuses are changed.

## What is implemented

- ESP-IDF HTTPS server, port 443 only. There is no HTTP server or downgrade fallback.
- Bounded identity loading, hostname and certificate/private-key matching checks.
- Pairing QR appears only after a background DNS check matches the current station
  address; unresolved, stale, mixed-address and IPv6 answers hide the QR. The cache
  expires after ten seconds and is invalidated immediately on IPv4 address change.
  The phone may use a different DNS resolver, so browser acceptance is still required.
- A dedicated 64 KiB `phone_tls` NVS partition at `0x1020000`. Existing settings,
  `otadata`, and both 8 MiB application slots retain their addresses and sizes.
- USB provisioning tools that reject wrong keys/names, untrusted issuers, wildcard
  or shared-name certificates, and certificates expiring within a day.
- Transport-key provisioning over verified HTTPS only; redirects and environment
  proxies are disabled. System trust is used unless an explicit development CA is
  supplied. There is no `--insecure` option.
- A physical-display **Forget all phones** action with a second-tap confirmation.
  Durable token rotation revokes all previous phones, including after reboot. An
  unsuccessful save is shown as an error; the in-memory token is not replaced.
- Native browser geolocation on trusted HTTPS; address search remains available.
- A read-only TLS probe that verifies trust and hostname before checking that an
  unauthenticated API call receives 401.

The existing shared bearer is still persistent. Per-phone enrollment, temporary
pairing invitations, renewable short sessions and individual-device revocation are
separate work still required for the full production pairing design. This change
adds an immediate physical revocation mechanism; it does not claim those features.

## Domain and certificate ownership

The proposed namespace is `tempo.tinyrobotcompany.io`, under the user's existing
`tinyrobotcompany.io` domain. No additional domain purchase is needed. A device
might have the random name `device-a7f3.tempo.tinyrobotcompany.io`; names must not
contain a customer name or email. Each device has its own key and leaf certificate.
Never copy one device's key to another or ship a fleet-wide wildcard key.

An A record for that device name resolves to its LAN IPv4 address. This is **not** a
port-forward or an internet-exposed control service. Private-address records may
be rejected by DNS-rebinding protections or filtered resolvers. DHCP address
changes, iPhone hotspots, encrypted DNS and offline DNS caching need validation.
A static record suffices for the prototype; automated updates are not implemented.

Public issuance uses DNS-01 proof of domain control. No port forwarding is needed.
Public NS lookup identifies Spaceship as the DNS provider. Scoped API access, the
public CA, issuer deployment and renewal policy are not yet configured. Do not put DNS-provider credentials or
CA private keys on a Tempo. Do not change existing apex, MX or website records.

## Create a development identity

Prerequisites: OpenSSL 3 (on macOS `brew install openssl@3`), the project's ESP-IDF
Python environment, and a private directory outside Git. `TEMPO_OPENSSL` can select
an explicit OpenSSL executable. The sample device name below is illustrative;
choose a unique random device label before publishing DNS.

```sh
python3 scripts/phone-identity.py create \
  --directory /absolute/private/device-a7f3 \
  --hostname device-a7f3.tempo.tinyrobotcompany.io
python3 scripts/phone-identity.py dev-cert \
  --directory /absolute/private/device-a7f3
python3 scripts/phone-identity.py validate \
  --directory /absolute/private/device-a7f3 \
  --trust /absolute/private/device-a7f3/ca.pem
```

The directory is mode 0700; keys and generated images are mode 0600. Existing
identities are not overwritten. The development CA lasts 365 days; its leaf lasts
30 days. **The prototype key is generated on the provisioning computer**, not on
the ESP. Protect the directory and remove unnecessary copies after provisioning;
retain a controlled backup if USB renewal will use it. Production enrollment must
use device-generated keys/CSRs so private keys never leave the device. The test CA
private key must never be installed on the ESP or a phone. Only `ca.pem` is public.

Activate ESP-IDF's Python environment, then generate the dedicated partition:

```sh
python3 scripts/phone-identity.py image \
  --directory /absolute/private/device-a7f3 \
  --trust /absolute/private/device-a7f3/ca.pem \
  --idf "$IDF_PATH"
```

`phone_tls.bin` contains a private key and is a secret, not a build artifact. Never
upload it to CI, attach it to a PR or include it in an OTA release. Neither CA nor
private key is embedded in the firmware executable. The ESP's certificate partition
is **not encrypted at rest** in this prototype.

## Public certificate import and renewal

The `create` command produces `device.csr`. Have the chosen CA issue an individual
server certificate for its exact DNS name after DNS-01 authorization. Put the leaf
first, followed by intermediates, in `fullchain.pem`. Use a trusted CA bundle as
`--trust`; the same validate/image commands work for public certificates. Never
use the leaf itself as a substitute for independently established issuer trust.

For this prototype, renewal is an operator task: obtain a new certificate for the
same key/name, validate it, regenerate `phone_tls.bin`, write only its partition
over USB and restart. Do this before expiry. Expired/untrusted certificates are
rejected by clients; there is no HTTP fallback. The firmware can load a certificate
before SNTP has established the date, but browsers enforce certificate dates.

Production still needs authenticated enrollment, CA/DNS automation, per-device
issuance quotas, renewal before expiry with retry/monitoring, key rotation and a
recovery procedure. A CA/DNS service could run on Azure but is not deployed here.
It must verify device ownership of each requested name, accept CSRs rather than
private keys, prevent clients selecting another device's DNS name or arbitrary
public addresses, and keep issuer/DNS secrets out of firmware. A per-device name
will be public in certificate-transparency logs. No customer account is required.

## Controlled USB migration — do not use normal OTA

Old devices identify as `ab-8mb-v1`; this firmware/release tooling identifies as
`ab-8mb-https-v1`. Old firmware rejects the new manifest layout before installation.
This prevents an update from disabling phone access because the existing partition
table does not contain a TLS identity. It also means old devices cannot follow the
new release stream until migrated. A production transition needs a fleet plan.

Before flashing, stop serial monitors/watchers and confirm sole device ownership.
Back up the existing partition table, NVS, `otadata`, and both application slots to
private storage; backups can contain credentials. Verify the current physical
board, offsets and flash size. A normal `idf.py flash` writes initial `otadata`, so
it is not a substitute for a reviewed migration command. Do not erase the chip.

Prepare a single USB operation for this board containing:

- Updated partition table at `0x8000`.
- Validated `phone_tls.bin` at `0x1020000`.
- Built application at the **currently boot-selected** application slot, determined
  from the device (not guessed). Preserve `otadata` and the other application slot.

The existing rollback-enabled bootloader is retained. A power interruption during
USB table/application writes can require USB recovery; never claim atomic migration.
Do not activate a trial image until a valid HTTPS identity is installed: startup
health checks intentionally require the phone server. Signed OTA checks, hash/size
checks, inactive-slot writing and trial rollback remain in place after migration.

## Acceptance

1. On the computer, before DNS is configured, verify the real device by hostname
   while connecting to its known LAN address:

   ```sh
   python3 scripts/check-phone-https.py \
     --hostname device-a7f3.tempo.tinyrobotcompany.io \
     --address 192.168.1.42 --ca /absolute/private/device-a7f3/ca.pem
   ```

2. Verify port 80 is closed. Verify a wrong name, wrong CA and expired certificate
   fail before transmitting a pairing token or transport key.
3. Publish the device's DNS A record, or use a controlled test resolver. For a
   development CA, install **only** its public certificate through a trusted route
   and explicitly trust it on the test phone. Public certificates need no manual
   trust setup. Never bypass browser certificate warnings.
4. Scan the HTTPS QR, refresh/reopen the page, restart Tempo and confirm reconnect.
   Test Safari and Chrome on iOS, Chrome on Android and Firefox/Edge on desktop.
   Remove/change the A record, change the LAN address and test mixed answers; the
   device must hide its QR until its resolver again finds only the current address.
5. Verify native location permission success/denial, address search, API-key save,
   settings, countdown/Install now, display sleep/wake and weather/transport.
6. Forget all phones on the display. Old phones must receive 401; a new QR scan
   works. Restart and verify the old credential is still rejected. Exercise failed
   persistence without reporting false success.
7. Renew the leaf certificate for the same device name/key; ensure pairing survives.
8. Test missing/corrupt identity and power-interrupted provisioning using the USB
   recovery path. Confirm no automatic erasure of settings or downgrade to HTTP.

Host tests and a passing ESP build do not establish these physical/browser results.

The SDK-backed regression `scripts/test-phone-tls-real.sh` builds the pinned SDK
Mbed TLS implementation on the host and calls the actual TLS startup function
with a non-self-signed CA-issued leaf. It rejects a wrong SAN, wrong key and
malformed certificate. Only ESP storage/server calls are substituted. Its
ephemeral test CA is never a device provisioning path. CI runs this alongside
the ESP32-P4 build. Startup checks the exact DNS SAN and key pair; issuer trust
is validated before installation, and browsers enforce dates before SNTP.
