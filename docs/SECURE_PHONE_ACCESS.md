# Secure phone access: device-generated identities

Private keys are generated on each ESP and never exported to a laptop, DNS provider
or backend. The former laptop key/certificate provisioning CLI has been removed.
An ephemeral test-only CA fixture remains under tests for loopback TLS tests; it
has no production provisioning role.

## Production flow

1. Configure the signed factory firmware with `CONFIG_TEMPO_ENROLLMENT_URL`, the
   deployed Azure HTTPS URL ending in `/v1/device`. The DNS suffix defaults to
   `tempo.tinyrobotcompany.io`.
2. Complete the board-specific Secure Boot and flash-encryption manufacturing
   procedure. `CONFIG_TEMPO_PRODUCTION_IDENTITY=y` refuses identity generation and
   HTTPS startup unless both protections are active. Code does not burn eFuses.
3. The ESP generates separate P-256 manufacturing-identity and TLS keys in its
   dedicated `phone_tls` partition. Only the PUBLIC identity PEM is printed for
   the trusted manufacturing station. Admission of that public key is performed
   by the separately authorized Azure factory job, never a public device endpoint.
4. Once connected and time-synchronized, the device signs versioned, timestamped,
   nonce-bearing requests to the service. Its hostname is `d-<identity hash>.tempo.
   tinyrobotcompany.io` (without a line break). It submits its TLS CSR, polls for a
   public certificate and independently verifies the key, hostname, validity and
   chain against public ISRG roots before committing it to NVS.
5. The device submits address changes and renews halfway through the certificate's
   actual lifetime. Jobs survive backend restarts. Firmware retains its existing
   certificate on network/CA/storage failures, backs off and never downgrades to
   HTTP. HTTPS restarts after a replacement is durably installed. Pairing persists.

Customers perform Wi-Fi setup and scan the pairing QR. They do not register a
DNS/cloud account, manage certificates or provision private keys from a computer.
The backend source and deployment instructions live in the companion certificate
service change (`docs/DEVICE_CERTIFICATE_SERVICE.md` when integrated).

## Trust and access boundaries

The cloud endpoint uses the ESP-IDF public CA bundle, normal hostname verification,
and no redirects. Issued device certificates are checked against embedded public
ISRG Root X1/X2 anchors taken from the maintained public CA bundle. Those public
certificates are not secrets. Firmware has no DNS API credentials or CA account key.
Manufacturing identity keys authorize enrollment; TLS keys authenticate the local
phone endpoint; pairing authorizes phone control. These roles are separate.

The pairing bearer remains persistent, with physical Forget all phones revocation.
Per-phone sessions and individual revocation remain a separate security change.
Never describe this change as implementing short-lived pairing sessions.

A record pointing at a private LAN address is not port forwarding. Some DNS
resolvers/routers reject private-address responses. The QR remains hidden if the
ESP's resolver does not return only its current IPv4 address; phones may use a
different resolver. Test that boundary explicitly. Do not disable router security
or bypass a browser warning to claim compatibility.

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
  connect with no warning. Port 80 must be closed.
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

Source compilation/host tests are development evidence. Cloud deployment, physical
key protection, browser trust and network compatibility require live acceptance.
