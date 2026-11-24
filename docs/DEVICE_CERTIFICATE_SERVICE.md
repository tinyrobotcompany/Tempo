# Tempo production certificate service

This service is a cloud control plane. It never generates, receives or stores a
Tempo private key. Firmware integration and deployment acceptance are separate
requirements; passing host tests does not establish a deployed product.

## Identity and customer flow

1. During manufacturing, the ESP generates two P-256 keys: a permanent identity
   key and a TLS key. Only its public identity key leaves the device. A trusted
   manufacturing operator reads that public key over a physically controlled
   connection and admits it to `DeviceRegistry` using the factory Azure job.
2. The device ID is the first 128 bits of SHA-256 of the identity public key's DER
   SubjectPublicKeyInfo. Its assigned name is `d-<id>.tempo.tinyrobotcompany.io`.
   The service recomputes that name; devices cannot choose names or register keys.
3. After Wi-Fi and time synchronization, the ESP signs a request containing a CSR
   for its TLS key. The Azure API verifies the manufacturing key, timestamp and
   nonce before atomically consuming the nonce and scheduling durable work.
4. The worker publishes the DNS-01 TXT value in Spaceship, asks Let's Encrypt to
   issue the certificate, independently verifies the returned chain/name/key,
   and stores only the public certificate. The ESP fetches and validates it.
5. The device submits signed RFC1918 address changes and renews its certificate
   halfway through its actual lifetime. DNS jobs use a separate row from issuance
   jobs, so CA backoff does not block address changes. Phone traffic stays local.

Customers need no cloud account, DNS account, certificate installation or computer.
The cloud service does not relay phone control or require router port forwarding.
DNS rebinding filters can block private-address DNS responses. Browser/router
acceptance is mandatory; unsupported networks must show an actionable status and
must never trigger HTTP fallback, certificate-warning bypass, or router weakening.

## Wire contract v1

`POST /v1/device`, `Content-Type: application/json`, maximum 6144 bytes.
`X-Tempo-Signature` is standard Base64 of the ASN.1 DER P-256 ECDSA/SHA-256 signature
over these exact bytes (including newlines), followed by the unmodified body:

```
TEMPO-DEVICE-V1
POST
/v1/device
```

Every body has `device_id` (32 lowercase hex), `nonce` (32 random bytes as 64
lowercase hex), `issued_at` (integer Unix seconds), and `action`:

| Action | Additional field | Result |
| --- | --- | --- |
| `certificate` | `csr`: PEM, P-256, exactly assigned CN and DNS SAN | 202 scheduled; existing certificate returned before renewal is due |
| `address` | `address`: RFC1918 IPv4 only | 202 scheduled or already applied |
| `status` | none | certificate, hostname, expiry, renewal time, address, DNS and issuance states |

Unknown/duplicate fields, additional SANs, CA extensions, wrong curves, invalid CSR
signatures and unsigned requests fail closed. Signed bodies are valid for five
minutes; consumed nonces remain at least ten minutes after their timestamp.
No bearer credentials or customer secrets belong in this API. No request bodies,
signatures, private addresses or provider error responses belong in logs.

Firmware should poll pending work at 30 seconds with jitter, slow to at least five
minutes after network failures, keep the current certificate until a replacement
has passed validation and durable storage, and never erase its identity on a
network, storage or certificate error. HTTP 401 means manufacturing admission is
missing/disabled, not permission to generate a replacement identity.

## Infrastructure dependency and activation

Infrastructure is owned by [PR #26](https://github.com/tinyrobotcompany/Tempo/pull/26)
and its `docs/INFRASTRUCTURE_DELIVERY.md` runbook. This service PR deliberately has
no competing Bicep templates or local deployment wrapper. Use the reviewed,
approval-gated GitHub workflow; do not run local Azure apply commands.

There is **one Azure environment**, West Europe, in the approved subscription.
The destination groups are `rg-tempo-westeurope-{network,data,security,ops,compute}`.
Existing resources must migrate and pass verification before old empty groups are
deleted. CA staging/production selects certificate trust only, never a second Azure
resource environment. Namespace changes must be reviewed in infrastructure code;
there is no wrapper silently choosing a staging or production DNS namespace.

The infrastructure keeps network, data, security, operations and compute separate.
Storage and Key Vault stay private; runtime identities cannot register devices.
Factory admission and CA-account bootstrap use separate privileged identities.
Keep registry backups and never open private data-plane access for convenience.

The bootstrap module in PR #26 models `bootstrapContactEmail`, `bootstrapTermsUrl`
and secure `bootstrapDnsCredentials`, with a Container Apps secret reference for
`SPACESHIP_CREDENTIALS`. Empty values do not accept CA terms or activate issuance.
The normal delivery workflow does not currently provide an activation-input path:
a reviewed secure bootstrap configuration/run is required before service activation.
Do not compensate with ad-hoc secret overrides or plaintext CLI arguments.
After copying credentials into Key Vault, remove the temporary job secret and
restrict bootstrap start permissions. Verify retries reuse the CA account key.
This PR does not activate bootstrap, accept terms or supply DNS credentials.

Before activation, complete the infrastructure migration/deployment, approve the
CA terms and contact, and establish provider-enforced DNS scope. Use only public
manufacturing keys for admission. Test CA roots are for controlled tests only and
must never enter customer firmware/browser trust stores. Verify real issuance,
renewal, DNS address changes, outages and phone/browser trust before shipping.
Device eFuses and private-key storage require separate hardware acceptance; never
burn eFuses during an ordinary update.

## Operations and validation

- Preserve DeviceRegistry independently of app deployments. Export/back up public
  registry entries under operator controls; do not assume Table storage supplies
  point-in-time recovery. Never reconstruct manufacturing trust from client claims.
- Disable a device in the registry to deny future enrollment/address requests.
  This alone does not revoke a certificate already issued by the CA; incident
  response must separately revoke the leaf and remove its DNS address.
- Worker leases and ETags coordinate replicas/revisions; DNS challenges are saved
  before publication, cleaned on retry, and certificate results are saved before
  cleanup. Four attempts bound a job; retries use backoff without erasing old certs.
  Invalid persisted CSR/address jobs stop with `invalid_job` before new provider
  work or issuance quota consumption. Investigate `device_job_invalid` events;
  existing certificates are retained. After the one-day error cooldown, a corrected
  signed request can create a new job.
- Alert on `worker_cycle_failed`, repeated `device_job_failed`, near-expiry devices,
  CA rate limits, and DNS credential rotation failures. Log events omit private
  material. Configure alert recipients and budgets before production activation.
- Host tests exercise signatures, replay, cross-device/name denial, malformed CSR,
  address policy, provider mutation scoping, worker recovery and API bounds.
  Run `bash scripts/test-certificates.sh`. Infrastructure validation belongs to
  PR #26; no local cloud deployment is part of this service test command.

## Source coverage and evidence

Design checked against official [ACME DNS-01](https://letsencrypt.org/docs/challenge-types/#dns-01-challenge),
[Spaceship API](https://docs.spaceship.dev/), [ACME Python client](https://acme-python.readthedocs.io/en/stable/api/client.html),
[Azure managed identities](https://learn.microsoft.com/azure/container-apps/managed-identity),
and [Azure Tables transactions](https://learn.microsoft.com/python/api/azure-data-tables/azure.data.tables.tableclient).
Sources consulted 2026-11-23. Local tests and template compilation are verified
development evidence. Cloud deployment, DNS API permissions, device key protection,
manufacturing operations and browser/router compatibility require live acceptance.

### Replay cleanup and lane isolation

Every signed request, including status, uses a single-use nonce. After a lost HTTP
response, the device signs a fresh request with a fresh nonce; resending identical
signed bytes is intentionally rejected as a replay. Authentication and request
validation precede admission. ETag contention rolls back the entire Table batch,
including the nonce, and is retried internally. Do not disable replay protection
for status or treat an HTTP delivery failure as permission to reuse a nonce.

Authenticated API requests delete up to 32 expired nonces from their own partition
before admitting one new request. Cleanup failure rejects admission. This bounds
ongoing growth during CA/DNS worker outages; idle devices create no further rows.
The worker sweeps expired nonces before enumerating jobs, so a job failure cannot
skip that sweep. Azure Table Storage itself supplies no TTL deletion.

`nonce_cleanup_failed` warns of failed sweeps/admission and
`nonce_cleanup_budget_reached` identifies a capped sweep/backlog. Configure log
alerts for either event before activation, alongside worker/job failures and
certificate expiry. No live Azure alert rules or recipients are configured by this
PR; rollout must verify those operational controls before customers use the service.

Registry identity fields are intentionally read from DeviceRegistry on every
request, not copied into writable state. Certificate and DNS jobs have distinct
row keys; combined status reads both explicitly. Adapter tests exercise first and
existing DNS rows through API admission, worker processing and status, including
certificate backoff and ETag conflicts. These tests model the Table contract;
they do not replace live Azure private-network/RBAC acceptance.

Production wheels are hash-locked for Linux amd64/Python 3.14. Test wheels are
hash-locked for that target and macOS arm64; `lock_dependencies.py --test` preserves
pinned versions and regenerates supported artifact hashes. Both install paths
require hashes. Linux service tests and dependency auditing are required CI jobs;
the macOS Intel firmware/UI job delegates service tests to that job. Local full
validation still includes service tests on supported hosts. Never bypass hashing
to install an unsupported platform artifact.
