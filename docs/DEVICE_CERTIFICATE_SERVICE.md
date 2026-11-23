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

## Azure isolation

`infra/certificates/main.bicep` deploys at subscription scope. Resource groups are
`rg-tempo-<environment>-<location>-<concern>`:

| Concern | Resources |
| --- | --- |
| network | VNet, compute/private subnets, private DNS zones and links |
| data | ZRS Storage account, device tables, private authenticated container registry |
| security | Key Vault, private endpoints, four managed identities |
| ops | Log Analytics with retention and daily cap |
| compute | Container Apps environment, API, worker, manual factory/bootstrap jobs |

No integration resources are needed. No group mixes workload concerns. Table and
vault public access and storage account-key authentication are disabled. The API
and worker can read registry rows but cannot admit/change identities. Only factory
identity can write registry. Both can write operational state. Only the worker
can read vault secrets. Only the isolated bootstrap job can create account secrets.
Starting or changing factory/bootstrap jobs is privileged: grant narrowly to
manufacturing/platform operators, never device/API/worker identities. Deployers
need RBAC assignment authority; that is not a device runtime permission.

## Operator deployment sequence

1. Select the intended Azure subscription, environment, region, cost owner and
   budget. Run Bicep build and subscription `what-if`; inspect all costs and role
   assignments. Do not default to an unrelated active subscription.
2. Deploy the foundation first with `deployCompute=false` (the default). This
   creates the private authenticated Azure Container Registry without starting apps.
   Build `services/certificates/Dockerfile` for **linux/amd64** using Azure ACR build,
   scan the image, then deploy with `deployCompute=true` and its immutable `@sha256`
   reference. Runtime identities have pull-only access; anonymous/admin pull is off.
   The registry uses authenticated public TLS endpoints (Basic tier); device tables
   and secrets use private endpoints. No device keys enter the build context.
3. Deploy the compute phase of the subscription template. Record its enrollment URL in the signed
   firmware build configuration; the Azure-managed HTTPS certificate secures the
   enrollment endpoint. No custom API domain is required.
4. Create a Spaceship API credential limited to DNS read/write and, where supported,
   the intended domain. Store a `spaceship-credentials` JSON secret containing `key` and `secret`
   together in Key Vault, so rotation is atomic. The bootstrap job can accept it
   through `SPACESHIP_CREDENTIALS`, referencing an encrypted Azure Container Apps
   job secret supplied through Azure administration, and copy it to the private
   vault from inside the VNet. Remove the temporary job secret afterwards. Never commit them,
   put them in firmware, pass them as CLI arguments, or save them on a customer PC.
   Provider-level scope may cover more than the Tempo namespace; the adapter also
   enforces exact device labels and never uses `force: true`.
5. Run the bootstrap job once in Azure with `ACME_CONTACT_EMAIL` and the exact
   operator-accepted `ACCEPTED_ACME_TERMS_URL`. It creates the **service account**
   RSA key inside Azure and writes it directly to Key Vault. This is not a device
   key. The job reuses an existing account key on retry. Never run bootstrap jobs
   concurrently. Remove operator start access when bootstrap is complete.
6. Use a separate staging deployment and CA account first. Its vault also requires
   `acme-staging-roots` containing the official Let's Encrypt staging roots. These
   roots must never be compiled into production firmware or browser trust stores.
   Production uses the public CA bundle; it does not load that secret.
7. Run the factory job with `MANUFACTURING_PUBLIC_KEY` (Base64 **public** SPKI PEM)
   and `MANUFACTURING_SERIAL`. Obtain the key from the physical unit, not an email
   or unauthenticated network claim. No customers can call this path.
8. Verify real DNS, issuance, phone trust, renewal, DHCP changes and outage recovery
   before shipping. Manufacturing enables Secure Boot/flash encryption under a
   controlled irreversible eFuse procedure; never burn fuses as part of a normal
   firmware update. Define hardware recovery and key-loss/replacement policy.

Key Vault's private endpoint intentionally requires a trusted network path for
direct data-plane administration. Use the controlled cloud bootstrap job for
initial DNS-secret transfer, or an approved private operations runner/VPN. Do not
temporarily open the vault to the internet. Job secret/configuration administration
is privileged; do not place values in plaintext CLI arguments or commit them.

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
- Alert on `worker_cycle_failed`, repeated `device_job_failed`, near-expiry devices,
  CA rate limits, and DNS credential rotation failures. Log events omit private
  material. Configure alert recipients and budgets before production activation.
- Host tests exercise signatures, replay, cross-device/name denial, malformed CSR,
  address policy, provider mutation scoping, worker recovery and API bounds.
  Run `bash scripts/test-certificates.sh` and `az bicep build --file
  infra/certificates/main.bicep`. Production dependencies are wheel-hash locked for
  Linux amd64/CPython 3.14; regenerate with `lock_dependencies.py` and audit them.

## Source coverage and evidence

Design checked against official [ACME DNS-01](https://letsencrypt.org/docs/challenge-types/#dns-01-challenge),
[Spaceship API](https://docs.spaceship.dev/), [ACME Python client](https://acme-python.readthedocs.io/en/stable/api/client.html),
[Azure managed identities](https://learn.microsoft.com/azure/container-apps/managed-identity),
and [Azure Tables transactions](https://learn.microsoft.com/python/api/azure-data-tables/azure.data.tables.tableclient).
Sources consulted 2026-11-23. Local tests and template compilation are verified
development evidence. Cloud deployment, DNS API permissions, device key protection,
manufacturing operations and browser/router compatibility require live acceptance.

The deployment wrapper `scripts/deploy-certificates.py` requires an explicit
subscription and defaults to what-if. `--apply` is an explicit deployment action.
It refuses mutable image tags. Foundation and application phases are separate,
and neither phase merges a pull request or flashes a device.
