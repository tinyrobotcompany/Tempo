# Secure phone access: device-generated identities

Private keys are generated on each ESP and never exported to a laptop, DNS provider
or backend. The former laptop key/certificate provisioning CLI has been removed.
An ephemeral test-only CA fixture remains under tests for loopback TLS tests; it
has no production provisioning role.

## Production flow

1. Configure the signed factory firmware with `CONFIG_TEMPO_ENROLLMENT_URL`, the
   deployed Azure HTTPS URL ending in `/v1/device`. The DNS suffix defaults to
   `tempo.tinyrobotcompany.io`. An empty endpoint initializes the protected device
   identity for manufacturing admission but starts no enrollment worker. HTTPS
   remains unavailable until a valid certificate is installed. A non-empty invalid
   endpoint returns an error and starts no worker.
2. Complete the board-specific Secure Boot and flash-encryption manufacturing
   procedure, including a dedicated HMAC_UP key with read, write and purpose locks.
   Set `CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID` to that reviewed factory slot (default
   -1 refuses identity storage). All builds require these protections for persistent
   identity storage; disabling `CONFIG_TEMPO_PRODUCTION_IDENTITY` cannot bypass them.
   Firmware only reads security state and derives storage keys; it never burns eFuses.
3. The ESP generates separate P-256 manufacturing-identity and TLS keys in its
   dedicated, encrypted `phone_tls` partition through a reserved PSA storage backend.
   Private-key export/copy permissions are not granted. CSR, signatures and HTTPS
   use opaque key IDs; application code never serializes private PEMs. Only the PUBLIC identity PEM is printed for
   the trusted manufacturing station. Admission of that public key is performed
   by the separately authorized Azure factory job, never a public device endpoint.
4. Once connected and time-synchronized, the device signs versioned, timestamped,
   nonce-bearing requests to the service. Its hostname is `d-<identity hash>.tempo.
   tinyrobotcompany.io` (without a line break). It submits its TLS CSR, polls for a
   public certificate and independently verifies the key, hostname, validity and
   chain against public ISRG roots before committing it to NVS.
5. The device submits address changes and renews halfway through the certificate's
   actual lifetime. Jobs survive backend restarts. Firmware retains its existing
   certificate on network/CA/storage failures, backs off and never switches to
   HTTP automatically. HTTPS restarts after a replacement is durably installed. Pairing persists.

Customers perform Wi-Fi setup and scan the pairing QR. They do not register a
DNS/cloud account, manage certificates or provision private keys from a computer.
The backend source and deployment instructions live in the companion certificate
service documentation (`docs/DEVICE_CERTIFICATE_SERVICE.md`).

## Trust and access boundaries

The cloud endpoint uses the ESP-IDF public CA bundle, normal hostname verification,
and no redirects. Issued device certificates are checked against embedded public
ISRG Root X1/X2 anchors taken from the maintained public CA bundle. Those public
certificates are not secrets. Firmware has no DNS API credentials or CA account key.
Manufacturing identity keys authorize enrollment; TLS keys authenticate the local
phone endpoint; pairing authorizes phone control. These roles are separate.

The pairing bearer remains persistent, with physical Forget all phones revocation.
Per-phone sessions, temporary invitations and individual revocation remain a separate security change.
Never describe this change as implementing short-lived pairing sessions.

A record pointing at a private LAN address is not port forwarding. Some DNS
resolvers/routers reject private-address responses. The QR remains hidden if the
ESP's resolver does not return only its current IPv4 address; phones may use a
different resolver. Test that boundary explicitly. Do not disable router security
or bypass a browser warning to claim compatibility. Customers on such networks can choose
Local mode on the display (below).

## No automatic HTTP fallback; user-chosen Local mode

HTTPS is the default and the firmware never falls back to HTTP by itself. The only
plain-HTTP path is **Local mode**, which the customer chooses by touch on the display
(Settings > iPhone setup > "Local mode (not encrypted)", or "Can't open the page?"
under the QR code). A confirmation screen states that traffic on the Wi-Fi is not
encrypted. No phone, HTTP, cloud or OTA path can switch it on: there is no API route
for it, phone payloads cannot carry it, and it is stored as its own NVS key
(`station/local_mode`) outside the settings blob. `tests/test_local_mode_contract.mjs`
and `tests/test_phone_local_mode.inc` pin this.

The display offers Local mode (it never enables it) when HTTPS cannot start (no
device identity, for example an unlocked development board), when the installed
certificate has expired, or when renewal has been overdue for 7 days or more. In
every case port 80 stays closed until the customer confirms on the display.

While on, port 80 serves the same page and authenticated API as HTTPS, HTTPS keeps
running if available, and the QR shows `http://<current IPv4>/#token=…`. The page
shows a permanent "Local mode – not encrypted" banner and an extra warning on the
transport-key form. Turning it off is done on the display, with confirmation; when a
valid certificate is back the display suggests it but never does it alone. Off is
saved first, then port 80 closes, then the pairing token is rotated because it was
sent unencrypted. If power is lost in between, the next boot finishes the rotation
with port 80 closed. The setting survives reboot and OTA; OTA and the trial-boot
health check do not depend on it. Port 80 is closed on new devices.

Risk accepted by the customer: anyone on the same Wi-Fi can read the pairing token
and transport key and control the display while Local mode is on. It changes nothing
about device identity, certificates or the cloud service.

## End of life

If Tiny Robot Company's certificate service ever stops, device certificates expire
within about 90 days and the HTTPS page stops working. Every device keeps a working
phone page forever through Local mode: once renewal has been overdue for 7 days, or
the certificate has expired, the display offers it, and the customer can switch it on. Local mode needs no cloud
service, DNS name or certificate. Weather, transport and the display keep working
independently of the certificate service.

## Manufacturing and existing-device migration

The `phone_tls` NVS partition is at `0x1020000`, size `0x10000`. All existing NVS,
OTA metadata and app offsets are preserved. Firmware layout is `ab-8mb-https-v1`;
older `ab-8mb-v1` firmware rejects these manifests. Ordinary OTA cannot migrate the
partition table or establish hardware eFuse protection.

For an existing development unit, first inspect its flash layout, running slot,
eFuse security state and recovery options. Preserve a controlled backup of its
settings and firmware before any destructive migration. Backups of old unencrypted
flash can contain credentials. Do not write the old `phone_tls.bin` import format;
new identities originate on-device. Do not use whole-chip erase or ordinary
`idf.py flash`, which resets OTA metadata, as a migration substitute.

Secure Boot/flash-encryption provisioning is irreversible and board-specific. A
reviewed factory procedure, protected signing material and a recovery decision are
prerequisites. Production firmware fails closed while those are missing. No eFuses
have been programmed merely by building this branch.

## Acceptance before release

- Enroll a physically admitted device; deny unknown/revoked keys and replayed requests.
- Confirm no private key crosses serial, HTTP, cloud storage, logs or build artifacts.
- Issue a real trusted certificate; verify iOS Safari/Chrome and desktop browsers
  connect with no warning. Port 80 must be closed while Local mode is off (the default).
- Verify key/name/issuer/expiry rejection, bounded responses and redirect denial.
- Test refresh, reboot, pairing revocation, location, transport-key save and controls.
- Advance renewal eligibility; keep the previous certificate during CA/DNS outages,
  then verify replacement and phone reconnect without user intervention.
- Change the DHCP address/Wi-Fi network; DNS updates automatically and QR readiness
  recovers. Test filtered/private DNS and phone resolvers separately.
- Interrupt certificate persistence; never erase keys/settings or activate a partial
  certificate. Test recovery of missing/corrupt identity under factory controls.
- Validate signed OTA and rollback after manufacturing migration; do not weaken the
  existing health or signing gates to make enrollment appear successful.

Identity storage uses `nvs_flash_secure_init_partition` with two domain-separated
HMAC-derived XTS keys. The HMAC input labels and PSA IDs are stable storage formats;
changing either requires a reviewed migration. The PSA IDs are 0x30000001 (factory)
and 0x30000002 (TLS); they never use the SDK default NVS backend. Missing/corrupt
storage is not erased automatically. Old plaintext PEM partitions are not imported;
existing prototypes require controlled re-enrollment and old-identity revocation.

This is encrypted software key storage with PSA-enforced export restrictions, not
an isolated secure element: trusted crypto/storage code handles key bytes in RAM.
An attacker executing privileged firmware can still abuse keys. Secure Boot, flash
encryption and factory-protected HMAC material remain necessary. Physical encrypted
flash extraction, interrupted writes and browser TLS acceptance are release gates.

Development without provisioned hardware uses host fixtures; it cannot generate
persistent device identities or enable HTTPS by disabling a build flag. Normal
boot initializes enrollment/identity before phone startup. Missing protections are
reported and phone access stays unavailable; local display/settings remain usable.
No automatic security downgrade, eFuse programming or flash migration occurs.

Source compilation/host tests are development evidence. Cloud deployment, physical
key protection, browser trust and network compatibility require live acceptance.

### Revocation feedback

A failed revocation can be retried without restarting Tempo; it requires a new
confirmation and immediately replaces the error with progress. While pending and
after success, the button is disabled for that page visit. Reopening Phone setup
clears old success/error feedback and permits another explicitly confirmed
revocation. Pending work remains visible across navigation. Host tests exercise
these transitions and verify duplicate pending requests do not repeat key rotation.
