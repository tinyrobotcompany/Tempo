# .NET certificate backend: design (Phase 1)

Status: **proposed, not approved**. This document describes how the Python
certificate service in `services/certificates/` gets replaced. It adds no code.
Nothing here is implemented, deployed or verified against Azure. Where a fact
comes from documentation rather than a test, the source is cited. Anything
still to be proven is marked **verify in Phase 2**.

Baseline: `origin/main` at `4eae79b` (2026-11-28). Open PRs that touch the same
service are #54 (alerts), #56 (registry backup) and #58 (production CA). The
port follows whatever of those has merged by the time each implementation PR
opens (see section 11).

## 0. Goals and non-goals

Goals:

- A .NET 10 modular monolith with `Domain`, `Application`, `Infrastructure` and
  `Api` layers, following `simonholmes001/voxa`, with the device-facing API and
  the worker on **Azure Functions Flex Consumption** (isolated worker).
- **Drop-in** behaviour:
  - wire contract v1 (`docs/DEVICE_CERTIFICATE_SERVICE.md`) unchanged;
  - the same `DeviceState` and `DeviceRegistry` rows, with the same property
    names **and the same EDM types**;
  - the same Key Vault secret names (`spaceship-credentials`,
    `acme-account-key`, `acme-account-uri`, `acme-staging-roots`);
  - the same log event names.
- No firmware change. No Python left in the backend at the end.
- Privilege separation stays as it is today. Runtime identities cannot register
  devices or write CA secrets. Factory admission, CA bootstrap, backup and
  restore each keep their own identity.

Non-goals: changing the protocol, the row schema, the DNS provider or the CA, or
adding features. Behaviour differences found while porting are recorded and
reproduced, not fixed. A fix would be its own reviewed change after cutover.

## 1. Component mapping

### 1.1 Solution layout

```
global.json                                  SDK pin (repository root, as in voxa)
backend/
  Tempo.Certificates.sln
  Directory.Build.props                      net10.0, Nullable, TreatWarningsAsErrors, AnalysisLevel latest
  Directory.Packages.props                   central package versions (section 3)
  coverage-thresholds.json                   section 6
  src/
    Tempo.Certificates.Domain/               pure rules, BCL only (System.Security.Cryptography, System.Formats.Asn1)
    Tempo.Certificates.Application/          use cases and ports; depends on Domain
    Tempo.Certificates.Infrastructure/       Azure Tables, Key Vault, Blob, Spaceship, ACME, DNS adapters
    Tempo.Certificates.Api/                  Functions host: HTTP triggers (API Function app)
    Tempo.Certificates.Worker/               Functions host: timer triggers (worker Function app)
    Tempo.Certificates.Jobs/                 console host: factory, bootstrap, backup, restore (Container Apps jobs)
  tests/
    Tempo.Certificates.Domain.Tests/
    Tempo.Certificates.Application.Tests/
    Tempo.Certificates.Infrastructure.Tests/
    Tempo.Certificates.Api.Tests/
    Tempo.Certificates.Worker.Tests/
    Tempo.Certificates.Jobs.Tests/
    contract/v1/                             shared request/response vectors (section 4.3)
```

`Api`, `Worker` and `Jobs` are three composition roots over the same inner
layers. Voxa has one `Api` host. Tempo needs three, because Azure grants a
managed identity to a whole Function app or job, not to a single function.
Every function in one app can use every identity assigned to that app. So the
only way to keep the existing identity split is a separate host per identity.
Dependencies point inwards only. An architecture test in each test project
enforces this by reading the assembly's referenced assemblies, so no extra
library is needed.

### 1.2 Python module to .NET type

| Python (`tempo_certificates/…`) | .NET project and type | Notes |
| --- | --- | --- |
| `protocol.py`: `CONTEXT`, `MAX_BODY`, `MAX_CLOCK_SKEW`, `decode`, `authenticate`, `validate_csr`, `validate_address`, `device_id`, `hostname`, `Rejected`, `Reply` | Domain: `DeviceProtocol`, `RequestDecoder`, `RequestSignature`, `CsrPolicy`, `LanAddress`, `DeviceId`, `DeviceHostname`, `Rejection` (code and status), `Reply` | `RequestDecoder` copies CPython `json.loads` acceptance rules (section 4.2) |
| `admission.py`: `Admission` | Domain: `AdmissionGate` (token bucket, injected `TimeProvider`) | Burst 60, 5/s per process. Register as a singleton per instance |
| `service.py`: `DeviceService.handle`, `status` | Application: `DeviceRequestHandler` | Line-for-line port: four ETag attempts, lane choice, busy, renew, retry and `request_window` logic |
| `worker.py`: `Worker.process`, `run_jobs`, `run_once`, `cleanup_nonces`, `LostLease`, `LEASE_SECONDS` (plus `report_expiring_certificates` from #54) | Application: `JobProcessor`, `LaneRunner`, `NonceSweeper`, `ExpiryReporter`, `LeaseLostException` | Lease token `RandomNumberGenerator.GetHexString(32, lowercase: true)` |
| `runtime.py`: `CloudIssuer`, `run_cycle`, `main` (two threads, sleep 30 s) | Application: `CloudIssuer`, `WorkerCycle`. Worker host: `DnsLaneFunction`, `DeviceLaneFunction`, `ExpiryCheckFunction` | Section 2.3 |
| `secret_cache.py`: `SecretCache` | Application: `SecretCache` (allowlist of four names, at most 300 s, never serves stale values on error, `Invalidate`) | Thread-safe. One per worker instance |
| `store.py`: `TableStore` | Infrastructure: `TableDeviceStore` implementing `IDeviceStore` and `IRegistryWriter`; `TableValueCodec` | Same queries, same entity-group transaction, same 409/412 mapping |
| `dns.py`: `Spaceship`, `TLSAdapter` | Infrastructure: `SpaceshipDnsProvider` (`IDnsProvider`) on a dedicated `HttpClient` | No proxy, no redirects, TLS 1.2 or later, 5 s connect and 15 s total timeouts |
| `issuer.py`: `AcmeIssuer`, `verify_chain`, `DIRECTORIES` | Infrastructure: `AcmeDns01Issuer` (`ICertificateIssuer`), `CertificateChainVerifier`, `AcmeDirectories`, `TxtPropagationProbe` | Section 3.1 |
| `app.py`: FastAPI app, `/health`, `/v1/device`, `create_azure_store` | Api: `DeviceFunction`, `HealthFunction`, `FallbackFunction`, `ContractResponseWriter`, `AzureTableFactory` (Infrastructure) | Section 4.2 |
| `manufacturing.py` | Jobs: `factory` verb, `FactoryAdmission` (Application) | Same environment variables, flags, output line and error texts |
| `bootstrap.py` | Jobs: `bootstrap` verb, `CaAccountBootstrap` (Application) | Section 3.1 |
| `backup.py` (PR #56) | Jobs: `backup export` and `backup restore` verbs, `RegistryBackup` (Application), `BlobBackupContainer` (Infrastructure) | Section 9 |

### 1.3 Azure resource to host and trigger

| Today (`compute.bicep`) | Identity | After cutover | Trigger |
| --- | --- | --- | --- |
| Container App `…-api` (min 0, max 3 replicas) | `…-api` | Function app `…-func-api` (Flex, own plan) | HTTP: `POST /v1/device`, `GET /health`, plus a fallback route |
| Container App `…-worker` (min 1, max 1, two threads, 30 s loop) | `…-worker` | Function app `…-func-worker` (Flex, own plan) | Timers: `DnsLane` and `DeviceLane` every 30 s, `ExpiryCheck` hourly |
| Job `…-factory` (manual) | `…-factory` | **Container Apps job (kept)**, .NET image, `Tempo.Certificates.Jobs factory` | Manual (`az containerapp job start`) |
| Job `…-bootstrap` (manual) | `…-bootstrap` | **Container Apps job (kept)**, `… bootstrap` | Manual, through `certificate-account.yaml` |
| Job `…-registry-backup` (PR #56, schedule `17 3 * * *`) | `…-backup` | **Container Apps job (kept)**, `… backup export` | Schedule |
| Job `…-registry-restore` (PR #56, manual) | `…-restore` | **Container Apps job (kept)**, `… backup restore` | Manual |

**Decision D1: the operator jobs stay as Container Apps jobs and run .NET.**
The brief says "Functions", so this is the first point the AiC needs to confirm
(question 2). The reasons:

- Today an operator starts a privileged step with
  `Microsoft.App/jobs/start/action` under Entra RBAC. The step runs inside the
  VNet, takes its inputs as per-execution environment overrides, and uses no
  shared secret. A Function cannot be started manually in the same way. It
  needs one of these:
  - the host admin endpoint with the **master key**, a shared secret that
    weakens the "no bearer credentials" rule;
  - an HTTP trigger behind App Service Authentication, which needs a new Entra
    app registration and Graph-level setup;
  - a queue message, which needs operator data-plane access to private
    storage.
- `scripts/certificate-account.sh`, its test, the PR #59 factory registration
  script and the PR #56 restore runbook all drive `az containerapp job …`.
  Keeping jobs leaves those operator interfaces unchanged.
- An idle Consumption-only Container Apps environment and idle jobs cost
  nothing (section 10). The ACR stays because the jobs need an image, and it
  also carries the Function packages (section 6.3).

Rejected alternative B, everything on Functions:

- factory, restore and bootstrap become Function apps triggered through the
  admin endpoint;
- the ACR is removed, saving about US$5 a month;
- three more Flex apps, and three more host-storage private endpoints (about
  US$22 a month) if each keeps its own host storage, which section 2.2 shows it
  must;
- master-key handling in the factory workflow.

## 2. Hosting on Flex Consumption

### 2.1 Facts relied on (Microsoft Learn, read 2026-11-28)

- One app per Flex plan. .NET 10 isolated worker is supported. Linux only. No
  deployment slots. `WEBSITE_TIME_ZONE` is unsupported, so timers run in UTC.
  Source: [Flex Consumption plan, Considerations / Supported language stack versions](https://learn.microsoft.com/azure/azure-functions/flex-consumption-plan).
- Instance memory is 512 MB (0.25 vCPU), 2,048 MB (default) or 4,096 MB.
  `maximumInstanceCount` can be set from 1 to 1,000. Always-ready instances are
  optional, default 0, and have no free grant. On-demand billing is GB-seconds
  while executing, plus executions, each with a monthly free grant. Minimum
  billable time is 1,000 ms, rounded up to 100 ms. Source: same page,
  *Instance sizes*, *Always ready instances*, *Billing*.
- Default HTTP concurrency per instance is 4 at 512 MB, 16 at 2,048 MB and 32
  at 4,096 MB. Source: [Concurrency, HTTP trigger concurrency](https://learn.microsoft.com/azure/azure-functions/functions-concurrency).
- VNet integration:
  - the subnet must be delegated to **`Microsoft.App/environments`**;
  - /27 minimum, /26 recommended for several apps;
  - the subnet can be shared by several Flex apps;
  - it **cannot be shared with a Container Apps environment**;
  - it must not hold private or service endpoints;
  - outbound traffic reaches private endpoints in the VNet.

  Sources: [Flex how-to, Subnet delegation / sizing / sharing / usage restrictions](https://learn.microsoft.com/azure/azure-functions/flex-consumption-how-to)
  and [Networking options, Regional virtual network integration](https://learn.microsoft.com/azure/azure-functions/functions-networking-options).
- Timeout is 30 minutes by default and unbounded at most. Source:
  [Scale, Function app timeout duration](https://learn.microsoft.com/azure/azure-functions/functions-scale).
- Timer: NCRONTAB with a seconds field. Only one instance of a timer function
  runs across all instances, coordinated through blob storage. It "will not
  trigger again if there is an outstanding invocation still running". There
  are no retries. `UseMonitor` defaults to false for schedules shorter than a
  minute. Source: [Timer trigger, NCRONTAB / Scale-out / Retry behavior / Configuration](https://learn.microsoft.com/azure/azure-functions/functions-bindings-timer).
- Host storage with an identity needs **Storage Blob Data Owner**, and a
  user-assigned identity is selected with `__credential` and `__clientId`.
  Sources: [Timer trigger, Connections](https://learn.microsoft.com/azure/azure-functions/functions-bindings-timer)
  and [Functions reference, identity-based connections](https://learn.microsoft.com/azure/azure-functions/functions-reference).
- `extensions.http.routePrefix: ""` removes the default `api` prefix. Source:
  [HTTP trigger, host.json settings](https://learn.microsoft.com/azure/azure-functions/functions-bindings-http-webhook).
- Custom domains are supported, as are managed and App Service certificates.
  Source: [Flex Consumption plan, Considerations (Certificates)](https://learn.microsoft.com/azure/azure-functions/flex-consumption-plan).
- A deployment to a private endpoint needs a runner with network and DNS access
  to it. Source: [Deployment technologies, Secured virtual networks](https://learn.microsoft.com/azure/azure-functions/functions-deployment-technologies).

**Verify in Phase 2** (Learn does not settle these):

- that West Europe offers Flex, using
  `az functionapp list-flexconsumption-locations` (read-only);
- which storage sub-resources a host with only HTTP and timer triggers needs:
  blob is required, and table and queue need checking;
- whether a GitHub-hosted runner can deploy (one deploy) when the deployment
  container is reachable only privately. The design below avoids depending on
  this.

### 2.2 Topology

```
                   Internet (devices)
                          │ HTTPS POST /v1/device
                          ▼
      ┌──────────────── Flex plan A ─────────────┐   ┌────────── Flex plan B ──────────┐
      │ …-func-api  (identity …-api)             │   │ …-func-worker (identity …-worker)│
      │ 512 MB, max 3 instances, always-ready 0  │   │ 512 MB, max 1 instance            │
      └───────────────┬──────────────────────────┘   └──────────────┬──────────────────┘
                      │ VNet integration: new subnet "functions" 10.62.3.0/26
                      │ (delegated Microsoft.App/environments; NOT the ACA subnet)
                      ▼
   private subnet 10.62.2.0/24:  table PE (existing) · vault PE (existing)
                                 blob PE → st…fnapi (host + deployment storage, API)
                                 blob PE → st…fnwrk (host + deployment storage, worker)
   compute subnet 10.62.0.0/23:  Container Apps environment → factory, bootstrap, backup, restore jobs
```

- **One host-storage account per Function app** (`stfa…` and `stfw…`, Standard
  LRS, Entra-only, public access disabled, one blob private endpoint each).
  Host storage needs Blob Data Owner at account scope, and the deployment
  package lives in that account. If the two apps shared an account, the API
  identity could overwrite the worker's package and so run code as the worker
  identity, which can read Key Vault. Separate accounts prevent that.
- Each app's host storage is also its deployment storage
  (`functionAppConfig.deployment.storage`, `UserAssignedIdentity`). This is the
  documented default.
- **API app:**
  - public inbound, `httpsOnly`, minimum TLS 1.2;
  - FTP and basic publishing credentials disabled;
  - `maximumInstanceCount: 3`, so the per-process admission budget stays
    bounded as it is today (3 × 60 burst);
  - per-instance HTTP concurrency raised from 4 to 16 through
    `scaleAndConcurrency`, close to today's 20 per replica. Requests are
    I/O-bound.
- **Worker app:**
  - no HTTP functions;
  - the main site gets an IP restriction with default action Deny and no allow
    rules, so only the platform and the deployment path can reach it
    (**verify in Phase 2**: deployment still succeeds);
  - `maximumInstanceCount: 1`.
- Application settings mirror today's environment variables:
  - `TABLE_ENDPOINT`, `DEVICE_SUFFIX`, `KEY_VAULT_URL`, `DNS_DOMAIN`,
    `ACME_ENVIRONMENT`, `AZURE_CLIENT_ID`;
  - `AzureWebJobsStorage__blobServiceUri`, `__credential=managedidentity` and
    `__clientId`;
  - `APPLICATIONINSIGHTS_CONNECTION_STRING`, plus
    `APPLICATIONINSIGHTS_AUTHENTICATION_STRING=Authorization=AAD;ClientId=…`.
- **API URL:** a Flex app is reached at `<app>.azurewebsites.net`, which is not
  the Container Apps FQDN. See section 7 and question 1.

### 2.3 Mapping the 30-second worker cycle onto triggers

Today two threads, `dns` and `device`, each run this loop: nonce sweep, read
the DNS secret, run due jobs in the lane, sleep 30 s. A slow CA job delays only
its own lane (`test_dns_job_runs_while_certificate_is_waiting`).

| Function | Schedule | Body |
| --- | --- | --- |
| `DnsLane` | `*/30 * * * * *`, `UseMonitor = false` | `WorkerCycle.Run(lane: "dns")` |
| `DeviceLane` | `*/30 * * * * *`, `UseMonitor = false` | `WorkerCycle.Run(lane: "device")` |
| `ExpiryCheck` | `0 0 * * * *` | `ExpiryReporter.Run` (#54 behaviour: count only, once an hour) |

- **Lane isolation.** The two lanes are separate functions, so they run
  concurrently and a 240 s ACME job in `DeviceLane` does not hold back
  `DnsLane`.
- **Singleton and overlap.** Each timer runs at most once across instances and
  is not re-triggered while it is still running. This matches the Python loop,
  where the next cycle starts only after the previous one ends. Correctness
  never depended on this: leases, ETags and `next_attempt` already make
  concurrent workers safe, which is what allows a parallel run with the Python
  worker during cutover (section 7).
- **Missed occurrences.** `UseMonitor = false`, so occurrences missed during a
  restart are not replayed. Work is durable in `DeviceState` and the next tick
  picks it up, as after a Python restart.
- **Failure handling.** Any exception that escapes a cycle is caught in the
  function and logged as `worker_cycle_failed lane=<lane>`. The function then
  returns normally. Timers do not retry, and an unhandled exception would only
  add host noise.
- **Timeout.** `functionTimeout` is `00:10:00`, against a worst case of about
  4 minutes per certificate job. This is also an upper bound on how long a lost
  lease can stall a row: `LEASE_SECONDS` is 900, so the lease outlives any
  single execution.
- **Expiry check.** The hourly check moves from a monotonic timer inside the
  `device` thread to its own timer. The alert contract (one event per hour,
  2-hour window) is unchanged.
- **Scale to zero.** Learn does not say outright that a Flex timer still fires
  after the app scales to zero (**verify in Phase 2**, a C1 check). A 30-second schedule keeps the worker effectively warm, and on-demand
  billing counts only executing time (section 10).

## 3. Libraries (pinned)

Versions are the latest stable on nuget.org on 2026-11-28, checked through the
NuGet v3 API. All are pinned centrally in `Directory.Packages.props`, with
`RestorePackagesWithLockFile` and `--locked-mode` in CI and the hook.
`dotnet list package --vulnerable --include-transitive` replaces `pip-audit`.

| Package | Version | Use |
| --- | --- | --- |
| .NET SDK (`global.json`) | 10.0.404, `rollForward: latestPatch` | Build. Runtime 10.0.15 (LTS, support phase active) |
| `Microsoft.Azure.Functions.Worker` | 2.52.0 | Isolated worker |
| `Microsoft.Azure.Functions.Worker.Sdk` | 2.1.0 | Build |
| `Microsoft.Azure.Functions.Worker.Extensions.Http.AspNetCore` | 2.1.1 | Streaming body read with a hard 6,144-byte cap, exact response headers |
| `Microsoft.Azure.Functions.Worker.Extensions.Timer` | 4.3.1 | Timers |
| `Microsoft.Azure.Functions.Worker.ApplicationInsights` | 2.51.0 | Worker telemetry |
| `Azure.Data.Tables` | 12.13.0 | `DeviceState`, `DeviceRegistry` |
| `Azure.Security.KeyVault.Secrets` | 4.12.0 | Worker and bootstrap secrets |
| `Azure.Storage.Blobs` | 12.30.2 | Backup and restore |
| `Azure.Identity` | 1.21.0 | `ManagedIdentityCredential(clientId)` only, never the full `DefaultAzureCredential` chain in Azure |
| `Certes` | 4.1.0 (depends on `BouncyCastle.Cryptography` 2.7.0) | ACME client (section 3.1) |
| `DnsClient` | 1.8.0 | TXT propagation probe |
| `Microsoft.Extensions.Hosting` | 10.0.15 | Jobs host |
| Tests: `xunit.v3` 4.0.3, `xunit.runner.visualstudio` 4.0.1, `Microsoft.NET.Test.Sdk` 18.12.0, `coverlet.collector` 10.1.1, `Microsoft.Extensions.TimeProvider.Testing` 10.11.1 | | Mocks are hand-written fakes, as in the Python tests. No mocking library |
| Dev tool: `azurite` (npm) | 3.37.0 (3.38.0 is less than two weeks old) | Table and Blob emulator for adapter integration tests (section 4.1) |

Azure SDK clients get `Diagnostics.IsLoggingEnabled = false`,
`IsDistributedTracingEnabled = false` and `Retry.MaxRetries = 2`, with network
timeouts of 5 s for connect and 15 s per try. This mirrors the Python
`logging_enable=False, connection_timeout=5, read_timeout=15, retry_total=2`.

### 3.1 ACME client: options compared

| Option | Maintenance (checked 2026-11-28) | Fit |
| --- | --- | --- |
| **Certes** 4.1.0 (MIT) | Released 2026-09-27, with v5.0.0-beta.1 on 2026-09-29. Repository not archived, last push 2026-11-23, 4 open issues | RFC 8555 with DNS-01, external CSR finalize (`IOrderContext.Finalize(byte[])`), RS256 account keys from PEM, typed problem documents (`AcmeRequestException.Error.Type`), ARI and profiles. Brings BouncyCastle |
| ACMESharpCore 2.2.0.148 | Last package 2020-08-20, last push 2024-02-20, 21 open issues | Unmaintained. Rejected |
| Direct RFC 8555 | Ours | About 400 lines: JWS RS256 (`RSA.SignData` PKCS#1 v1.5), RFC 7638 thumbprint, nonce pool with `badNonce` retry, POST-as-GET, order, authorization, challenge, finalize and poll. No third-party crypto. Every request and error path is ours to test and redact |

**Decision D2: use Certes 4.1.0 behind a port.** Fall back to a direct
implementation if the Phase 2 spike fails any check below.

`ICertificateIssuer` and the ACME calls sit behind the Infrastructure type
`AcmeDns01Issuer`. Nothing outside it references Certes, so changing the
library touches one class and its tests. The Phase 2 spike (PR 5), which must
pass before any other ACME code:

1. Certes loads the existing `acme-account-key` format (PKCS#8 PEM, RSA 3072,
   as `bootstrap.py` writes it).
2. The `kid` equals the stored `acme-account-uri`. Python uses the stored URI
   directly. Certes looks the account up with `onlyReturnExisting`, so the
   design compares the returned location with the stored URI and fails closed
   on a mismatch. The Python prefix check (`…/acct/` under the configured
   directory) is kept.
3. Finalize works with the device CSR (DER) without Certes generating a key.
4. The issued chain is stored **as the CA returned it**. If Certes's
   `CertificateChain` re-serialises the PEM, download the raw body with
   POST-as-GET instead.
5. Problem types `unauthorized`, `accountDoesNotExist` and `rateLimited` are
   visible, so the cache invalidation and `ca_rate_limited` behaviour from #54
   carry over.

The issuance flow matches `issuer.py`:

1. Validate the CSR, then create the order.
2. Require exactly one authorization, for the expected name.
3. Checkpoint `challenge` **before** the TXT `PUT`.
4. Probe propagation: `DnsClient` resolves `_acme-challenge.<name>` TXT through
   the VNet resolver, with a 5 s lifetime, every 5 s, for up to 150 s.
   Otherwise fail with `dns_propagation_timeout`.
5. Answer the challenge, then poll and finalize with a 90 s deadline.
6. Verify the chain.

Bootstrap:

- generate the key with `RSA.Create(3072)` and export it with
  `ExportPkcs8PrivateKeyPem()` (same PEM label);
- store it in Key Vault **before** registration;
- require `meta.termsOfService == ACCEPTED_ACME_TERMS_URL`;
- `newAccount` with `mailto:` contact and `termsOfServiceAgreed`;
- store `acme-account-uri`;
- the optional `SPACESHIP_CREDENTIALS` object must be exactly `{key, secret}`
  with non-empty strings;
- the same final message, printed to stdout.

### 3.2 P-256, CSR and X.509 with `System.Security.Cryptography`

- **Device key and id.** `ECDsa.ImportFromPem` accepts only `PUBLIC KEY`. The
  curve must be `nistP256` (`1.2.840.10045.3.1.7`). Anything else gives
  `invalid_public_key`. The id is the first 32 lowercase hex characters of
  SHA-256 over `ExportSubjectPublicKeyInfo()` (canonical uncompressed SPKI,
  which is what `public_bytes(DER, SubjectPublicKeyInfo)` produces).
- **Signature.**
  - header at most 104 characters;
  - strict Base64 with the alphabet and padding of `b64decode(validate=True)`;
  - then `VerifyData(CONTEXT ‖ body, sig, SHA256, DSASignatureFormat.Rfc3279DerSequence)`.

  Vectors include non-minimal DER, a high-S signature (accepted by both) and
  P1363-format signatures (rejected).
- **CSR.**
  - length 1 to 4,096 characters;
  - `CertificateRequest.LoadSigningRequestPem(pem, SHA256, CertificateRequestLoadOptions.UnsafeLoadCertificateExtensions)`.
    The flag only exposes extensions for inspection, because nothing is ever
    signed from the request. The signature is checked by default;
  - P-256 key;
  - the subject is decoded with `AsnReader`, and must be exactly one RDN with
    one CN equal to the hostname;
  - exactly one requested extension, which must be SAN with exactly one
    `dNSName` equal to the hostname.

  **Verify in Phase 2:** .NET accepts the PEM labels `CERTIFICATE REQUEST` and
  `NEW CERTIFICATE REQUEST` the way `cryptography` does, and handles text
  around the block the same way. Firmware (mbedTLS) emits
  `CERTIFICATE REQUEST`. Vectors pin the other cases.
- **Chain** (`verify_chain`):
  - the UTF-8 length must be greater than 0 and less than 8,192;
  - `X509Certificate2Collection.ImportFromPem`;
  - the leaf SPKI must equal the CSR SPKI;
  - the leaf SAN must be exactly `[name]`;
  - `X509Chain` with `RevocationMode.NoCheck` (as today: no revocation check),
    `VerificationTime = now`, the serverAuth EKU in
    `ApplicationPolicy`, and `ExtraStore` set to the intermediates from the
    response;
  - trust: for staging, `TrustMode = CustomRootTrust` with
    `acme-staging-roots`. For production, the platform trust store, the
    closest equivalent of Python's `certifi` Mozilla bundle;
  - the certificate must have at least 1 day left;
  - `renew_after = max(now + 3600, start + (end − start) / 2)`, using integer
    division.

  `PolicyBuilder` in `cryptography` applies some CA/Browser Forum profile
  checks that `X509Chain` does not. The chain tests
  (`test_real_certificate_chain_key_name_and_expiry_validation`) carry over,
  with extra negative cases for a CA leaf and a missing serverAuth EKU.
- **LAN address.** A strict dotted-quad parser, because `IPAddress.TryParse`
  accepts `10.1`, hex and octal and is too lenient. It rejects leading zeros as
  Python 3.14 `ipaddress` does, keeps only RFC 1918, and returns the canonical
  form.

### 3.3 Azure Tables and Key Vault with managed identity

- `ManagedIdentityCredential(ManagedIdentityId.FromUserAssignedClientId(AZURE_CLIENT_ID))`.
  Local tests never construct credentials.
- Endpoint guards are kept: `https://…table.core.windows.net`, a Key Vault URL,
  and `https://…blob.core.windows.net`.
- **The EDM type contract matters most for drop-in safety.**
  - `azure-data-tables` (Python) writes `int` as **Edm.Int32** when the value
    fits and as Edm.Int64 otherwise. Today every timestamp (about 1.8e9) fits in
    Int32.
  - It formats filter parameters the same way: `1800000000` without the `L`
    suffix.
  - The Table service compares typed values, so an Int64 literal does not
    match Int32 properties. If .NET wrote Int64, or sent `…L` literals,
    `candidates` and `expire_nonces` would silently find nothing for rows
    written by Python, and Python's queries would find nothing for rows
    written by .NET.
  - So `TableValueCodec`:
    - reads Int32 and Int64 as `long`;
    - writes `long` as Int32 when it fits and Int64 otherwise;
    - writes strings and booleans as they are;
    - builds every filter with the same rule. It never uses
      `TableClient.CreateQueryFilter` with a `long`, which emits `L`.
  - The tests fix this contract. Rows from a Python-shaped fixture must
    round-trip byte-identical through .NET.
- `UpdateMode.Replace` with `IfMatch` is used everywhere Python uses `REPLACE`,
  so removed fields disappear the same way.
  `SubmitTransactionAsync([Add nonce-<n>, Add|UpdateReplace(etag) row])`:
  - 409 at index 0 means `replayed_request` (409);
  - 409 or 412 elsewhere means retry;
  - anything else propagates.
- The `entity()` stripping of `etag`, `public_key`, `hostname`, `enabled` and
  `product` is kept. Registry identity fields are read on every request and
  never written to `DeviceState`.
- Key Vault: `SecretClient.GetSecretAsync(name)` behind `SecretCache`. The
  worker identity keeps Key Vault Secrets User. Only bootstrap holds Secrets
  Officer.

## 4. Contract preservation

### 4.1 Every Python test becomes an xUnit test

The 46 Python test functions (more cases once parametrised), and the tests that
#54 and #56 add, are ported one to one. Each .NET test cites its Python origin
in a `[Trait("python", "<file>::<test>")]`. A repository check (Node,
`node --test`) fails if any `def test_` name in `services/certificates/tests`
has no matching trait, until the final PR deletes the Python tests. Python
fakes become the same C# fakes:

- `MemoryStore` becomes `MemoryDeviceStore`;
- `TableFake` becomes `TableClientFake`, with the same "not evidence of server
  atomicity" note;
- `DNS` becomes `RecordingDns`;
- the inline issuer classes become `ScriptedIssuer`.

Time is injected with `FakeTimeProvider`, with `NOW = 1800000000`.

| Python file | Python tests | .NET test class |
| --- | --- | --- |
| `test_service.py` | signed CSR only; replay; impersonation; revoked identity; CSR hostname (3 cases); multi-name CSR | `Application.Tests/DeviceRequestHandlerTests`, `Domain.Tests/CsrPolicyTests` |
| | RFC 1918 address (7 cases, including the integer `123`); exact bytes and path; clock skew and unknown fields | `Domain.Tests/LanAddressTests`, `Domain.Tests/RequestSignatureTests`, `Domain.Tests/RequestDecoderTests` |
| | busy keeps CSR; valid certificate instead of early reissue | `Application.Tests/DeviceRequestHandlerTests` |
| | API limits and error redaction | `Api.Tests/DeviceFunctionTests` (in-process ASP.NET Core test host) |
| | worker crash recovery; failed worker keeps certificate (3 failure types); revocation during issuance; invalid persisted job (5 corruptions, log text); completed job with cleared CSR | `Application.Tests/JobProcessorTests` |
| | DNS exact record; no duplicate (2); no success before convergence (4) | `Infrastructure.Tests/SpaceshipDnsProviderTests` |
| `test_backend_guards.py` | foreign product field; existing device needs no change | `Application.Tests/DeviceRequestHandlerTests` |
| | other product namespace; large zone lookup; sorted lookup boundaries (5); zone size change | `Infrastructure.Tests/SpaceshipDnsProviderTests` |
| | unsigned requests bounded before storage; admission recovers | `Api.Tests/AdmissionTests`, `Domain.Tests/AdmissionGateTests` |
| | DNS job runs while certificate waits | `Worker.Tests/LaneIsolationTests` (two timer bodies on separate tasks with barriers) |
| `test_secret_cache.py` | cache expiry and no stale value; DNS auth invalidation (4 statuses); cycles reuse the DNS secret; ACME auth refresh (3 codes) | `Application.Tests/SecretCacheTests`, `Infrastructure.Tests/SpaceshipDnsProviderTests`, `Application.Tests/WorkerCycleTests`, `Application.Tests/CloudIssuerTests` |
| `test_adapters.py` | registry separate and never written; conflict and replay outcomes; failed ETag keeps nonce; API cleanup bounded and scoped; nonce expiry boundary | `Infrastructure.Tests/TableDeviceStoreTests` (fake) **and** `TableDeviceStoreAzuriteTests` |
| | real chain validation (2) | `Infrastructure.Tests/CertificateChainVerifierTests` (certificates built with `CertificateRequest.Create`) |
| | DNS worker never touches the CA; cleanup order; cleanup failure does not skip jobs; cleanup without Key Vault | `Application.Tests/WorkerCycleTests` |
| | real store keeps DNS lane; cleanup failure blocks admission; authentication precedes admission | `Application.Tests/DeviceRequestHandlerStoreTests` |

The Azurite tests add something Python never had: they check the real SDK's
batch, ETag, typed-filter and Int32/Int64 behaviour against an emulator. They
are still not evidence of Azure's server semantics. Live acceptance is still
required (section 7).

### 4.2 Byte-for-byte HTTP behaviour

The **contract surface** is:

- status code;
- response body bytes;
- the `Content-Type`, `Cache-Control` and `Retry-After` headers;
- the order in which checks happen.

Transport headers (`Date`, `Server`, `Content-Length`, `Request-Context`,
`x-ms-*`) are outside it. The firmware reads only the status, the body and
`Retry-After` (`device_enrollment.c`).

| Case | Status | Body | Extra headers |
| --- | --- | --- | --- |
| Admission bucket empty (checked first) | 429 | `{"error":"slow_down"}` | `Retry-After: 30` |
| `Content-Type` before `;` is not exactly `application/json` (case-sensitive, not trimmed) | 415 | `{"error":"json_required"}` | none |
| Streamed body over 6,144 bytes | 413 | `{"error":"request_too_large"}` | none |
| `Rejected(code, status)` | status | `{"error":"<code>"}` | `Cache-Control: no-store`, `Retry-After: 30` |
| Success (`Reply`) | 200 or 202 | Python key order, see below | `Cache-Control: no-store`, `Retry-After: 30` |
| Any other exception | 503 | `{"error":"service_unavailable"}` | `Retry-After: 60` |
| `GET /health` | 200 | `{"status":"ok"}` | none |

- **Serialisation.** Starlette writes compact JSON in insertion order, with
  `ensure_ascii=False`. The .NET writer uses `Utf8JsonWriter` with
  `JavaScriptEncoder.UnsafeRelaxedJsonEscaping`. The default encoder would
  escape `+` in PEM Base64 as `+`, and `<`, `>` and `&` the same way.
  - Property order is fixed in code: `hostname, state, certificate,
    renew_after, expires_at, address, dns_state, error, retry_after` for
    status; `state, retry_after` for 202.
  - Integers are written without exponent.
  - `Content-Type` is exactly `application/json`, with no `; charset=utf-8`.
  - Vectors check certificates that contain `\n`, `+`, `/` and `=`.
- **Signature header.** If `X-Device-Signature` is present it is used, even if
  empty. Otherwise `X-Tempo-Signature` is used, otherwise `""`. If a header is
  repeated, the first value is taken.
- **Request decoding mirrors CPython `json.loads` with the duplicate-key
  hook.** Each rule below is a test vector:
  - an empty body gives 413 `request_too_large`;
  - byte input is encoding-detected: UTF-8, UTF-8 with BOM, UTF-16 and UTF-32
    LE/BE;
  - `NaN`, `Infinity` and `-Infinity` are accepted as numbers;
  - duplicate keys are detected **when the enclosing object closes**, at any
    depth. So a syntax error earlier in the same object wins with
    `invalid_json`;
  - lone surrogate escapes are accepted, and then fail the field regex;
  - integers over 4,300 digits give `invalid_json` (CPython's int string
    limit);
  - `issued_at` must be a JSON integer token: `1.0`, `1e3`, `true` and `NaN`
    give `invalid_time`. An integer outside Int64 passes decoding and is
    rejected later as `stale_request`, as Python's `abs(now − t) > 300` does;
  - check order: `invalid_json`, `duplicate_field`, `invalid_request` (not an
    object), `invalid_fields` (action check, then exact key set),
    `invalid_device_id`, `invalid_nonce`, `invalid_time`. Then the registry
    lookup (`device_not_enrolled`), `stale_request` and `invalid_signature`
    (401), nonce sweep, `device_configuration_mismatch` (503), `slow_down`
    (429), then CSR or address validation.

  `System.Text.Json` alone does not reproduce these rules: it rejects `NaN`
  and duplicates differently and does not detect UTF-16 or UTF-32. So
  `RequestDecoder` is a small recursive-descent parser over the decoded text.
  Its only job is to reproduce these rules, and it is covered at 95% (Domain
  gate).
- **Routing.**
  - `host.json` sets `routePrefix: ""`, so the paths are exactly `/v1/device`
    and `/health`;
  - `POST /v1/device` uses `AuthorizationLevel.Anonymous`. Authentication is
    the device signature;
  - a catch-all `FallbackFunction` returns FastAPI's `{"detail":"Not Found"}`
    (404);
  - a wrong method on a known path returns `{"detail":"Method Not Allowed"}`
    (405 with `Allow`).

  These two are best-effort and outside the firmware contract. The Functions
  host keeps `/admin/*` and `/runtime/*`.

### 4.3 Cross-implementation contract test

`backend/tests/contract/v1/vectors.json` is one committed file.

- It holds a **test-only** P-256 identity key. Its PEM is committed and its
  comment states that it is generated and never registered in Azure.
- It holds a registry row and initial `DeviceState` rows.
- It holds about 80 cases. Each case has:
  - the exact body bytes (Base64);
  - the signature header(s);
  - `content-type`;
  - the clock value;
  - an optional "store contention" script (for example, 412 on the first
    batch);
  - the expected status, body bytes, contract headers, and resulting rows
    including EDM types.
- The cases cover every rule in 4.2, every rejection code, the
  pending/busy/renew/address/status paths, and replay after a lost response.
- ECDSA signatures are randomised, so the vectors store concrete signatures.
  Any valid signature verifies.

There are two runners over the same file:

- **.NET:** `Api.Tests/ContractVectorTests` runs every case through the real
  Functions HTTP pipeline (ASP.NET Core test server) and the real
  `TableDeviceStore` against Azurite.
- **Python:** a temporary pytest module,
  `services/certificates/tests/test_contract_vectors.py`. It runs every case
  through `create_app(DeviceService(TableStore(TableFake…)))` with
  `TestClient`.

Both run in CI. When both pass, the two implementations give byte-identical
answers to the same signed requests. Neither implementation's output is the
"expected" one: the expected values are reviewed by hand against the Python
source, and the Python runner shows they are what Python really does.

**This adds about 60 lines of temporary Python test code to the service that is
being removed**, against the "add no new Python" rule. The file is deleted in
the final PR together with the service. It needs an explicit AiC and Simon
decision (question 3). If it is refused, the fallback is weaker: the vectors
run only against .NET, and their Python equivalence rests on review alone.

Optional, after deployment: a read-only smoke run of the same `status` vectors
against both live endpoints. It needs a dedicated test identity registered by
the factory job, which is an operator action (question 5).

## 5. Infrastructure changes

| File | Change | Owner |
| --- | --- | --- |
| `infra/certificates/functions.bicep` (new, compute group) | For each of `api` and `worker`: a Flex plan (`FC1`) and a site (`functionapp,linux`, `dotnet-isolated` 10.0, 512 MB, max instances 3 and 1, HTTP concurrency 16 for the API, `virtualNetworkSubnetId`). Two host-storage accounts (LRS, `allowSharedKeyAccess: false`, `publicNetworkAccess: Disabled`, `minimumTlsVersion: TLS1_2`), each with a deployment container and a blob private endpoint into the private subnet, using the `privatelink.blob` zone. A workspace-based Application Insights component on the existing workspace (`disableLocalAuth: true`), plus role assignments | Agent 5 |
| `infra/certificates/compute.bicep` | During parallel run: unchanged API and worker apps. Jobs move to `backendImage` and the .NET entry (`command: ['/app/Tempo.Certificates.Jobs', 'factory']` and so on). Then the cutover parameter (section 7). Finally the Container Apps `api` and `worker` are removed | Agent 5 |
| `infra/certificates/main.bicep` (compute parts) | New `backendImageDigest` parameter. `imageDigest` stays the Python image until removed. A `workerRuntime` parameter (`python` or `dotnet`). The `functions` module. Outputs `enrollmentUrl` and `apiName` move to the Function app | Agent 5 |
| `infra/certificates/network.bicep` | **New subnet `functions` `10.62.3.0/26`**, delegated to `Microsoft.App/environments`, with no endpoints. The `privatelink.blob` zone and link (already in #56) | **Cross-component** |
| `infra/certificates/bootstrap.bicep` | `delegableRoles.compute`: add Storage Blob Data Owner `b7e6dc6d-f1e8-4753-8033-0f276bb0955b` and Monitoring Metrics Publisher `3913510d-42f4-4e42-8a64-420c390055eb`. Other groups are unchanged: data keeps Table Reader, Table Contributor and AcrPull (plus #56's blob roles), security keeps Secrets User and Officer | **Cross-component** (and an operator bootstrap re-run before the infra preview, as in #56) |
| `infra/certificates/ops.bicep` | Alert queries extended to App Insights traces (section 8) | **Cross-component (Agent 4)** |

**Role assignments** (new ones marked ★):

| Principal | Scope | Role |
| --- | --- | --- |
| api | `DeviceRegistry`, `DeviceState` | Table Data Reader, Table Data Contributor (unchanged) |
| worker | `DeviceRegistry`, `DeviceState`, Key Vault | Table Data Reader, Table Data Contributor, Secrets User (unchanged) |
| api ★ | `stfa…` (its own host storage) | Storage Blob Data Owner |
| worker ★ | `stfw…` (its own host storage) | Storage Blob Data Owner |
| api, worker ★ | Application Insights component | Monitoring Metrics Publisher (Entra-only ingestion) |
| factory, bootstrap, backup, restore | as today | unchanged, including AcrPull for jobs |
| api, worker | ACR | AcrPull, removed with the Container Apps apps in the final PR |

No runtime identity gains Key Vault Officer, registry write, or access to the
other app's storage.

Bicep follows the current API versions in the repository. The Flex
`Microsoft.Web/serverfarms` and `sites` resources use `2024-04-01`, as in voxa.
`delegation.test.sh` already derives roles per group from the modules that
`main.bicep` maps, so the new module is checked automatically. A new
`.github/scripts/infrastructure-functions.test.sh` checks the compiled
template:

- a Flex plan per app;
- different host-storage accounts;
- storage private and Entra-only;
- the subnet is not the Container Apps subnet;
- `routePrefix` is empty;
- API max instances 3 and worker max 1;
- no Key Vault role for api.

## 6. CI, hooks, coverage and delivery

### 6.1 Hook and local suite

- `scripts/test-backend.sh` (new, POSIX shell):
  - `dotnet --version` must satisfy `global.json`, otherwise exit 1. A missing
    SDK fails the commit;
  - `dotnet restore backend/Tempo.Certificates.sln --locked-mode`;
  - `dotnet test … --collect:"XPlat Code Coverage" --results-directory build-host/backend-test --logger trx`;
  - `node .github/scripts/check-backend-coverage.mjs build-host/backend-test backend/coverage-thresholds.json`;
  - `dotnet list … package --vulnerable --include-transitive`, which fails on
    any finding.
- Azurite runs from a locked npm dev dependency, started and stopped by the
  script on a random local port with data under `build-host/`. It is never
  used outside tests.
- `scripts/test-all.sh` gains `bash scripts/test-backend.sh`, with
  `TEMPO_BACKEND_SEPARATE_JOB` like the existing certificates switch. The
  pre-commit hook already runs `test-all.sh` against the staged snapshot, so
  docs-only commits also run .NET tests.
- **Zero tests must fail.** The coverage script reads the TRX files and fails
  if any test project ran zero tests, or if the solution contains a test
  project that produced no TRX file.

### 6.2 CI (`ci.yaml`)

- New job `Backend Tests` on `ubuntu-24.04`:
  `actions/setup-dotnet` pinned to a full SHA, using `global.json`;
  `setup-node` 22 for Azurite and the coverage script; then
  `bash scripts/test-backend.sh`. It uploads coverage and TRX artifacts.
- `CI Required` gains `backend` in `needs`.
- `host-tests` sets `TEMPO_BACKEND_SEPARATE_JOB=1`.
- The `Certificate Service Tests` job and the `pip-audit` step stay until the
  final PR, then go.
- `infrastructure-changes.sh` (cross-component) adds `backend/**` only if
  needed. Backend changes do not deploy infrastructure.

### 6.3 Image and deploy workflows: one immutable artifact

Today `certificate-image.yaml` builds one image and records its digest. A
reviewed `imageDigest` PR then deploys it. That model stays.

- `backend/Dockerfile` is multi-stage:
  - stage 1 is the SDK image pinned by digest. It runs
    `dotnet publish` for `Api`, `Worker` and `Jobs` (Release,
    `linux-x64`, `ContinuousIntegrationBuild=true`, deterministic). It zips
    `Api` and `Worker` and records their SHA-256;
  - the final stage is
    `mcr.microsoft.com/dotnet/runtime:10.0-noble-chiseled@sha256:<digest>`
    (non-root, no shell, no package manager). It holds the Jobs app as entry
    point and `/packages/api.zip`, `/packages/worker.zip` and
    `/packages/SHA256SUMS`.

  The image digest therefore pins the job code **and** both Function packages.
- `scripts/certificate-image.sh`:
  - builds `backend/` instead of `services/certificates/`, into repository
    `tempo-certificates-backend`;
  - the Waveform guard greps `backend/src`;
  - the rest is unchanged: ACR Tasks, unique tag, digest from the ARM result,
    context checks, artifact JSON;
  - during parallel run both images are built, the Python one under its
    current paths.
- `certificate-image.yaml`:
  - paths add `backend/**` and `global.json`;
  - `validate` runs `scripts/test-backend.sh` instead of Python;
  - `.github/scripts/certificate-image.test.sh` is updated (cross-component
    unless reassigned).
- Function code deploy (`infrastructure-deploy.yaml`, cross-component): after
  `az deployment sub create` succeeds:
  - pull `…/tempo-certificates-backend@sha256:<backendImageDigest>`. The
    delivery identity has Contributor on the data group, which should include
    ACR pull (**verify in Phase 2**; otherwise add a pull role);
  - extract the two zips and check `SHA256SUMS`;
  - run `az functionapp deployment source config-zip` for each app;
  - then `verify.sh` checks:
    - `GET /health`;
    - `POST /v1/device` with `{}`, which must give 400 `invalid_fields` and
      `Cache-Control: no-store`. This is a pure decode error, with no storage
      access and no device needed;
    - for the worker, the timer function list.

  Promotion uses the same immutable artifact, and code deploys only after a
  reviewed digest change.
- `certificate-account.yaml` and `scripts/certificate-account.sh` are unchanged
  in logic. The bootstrap job is still a Container Apps job, and it now runs
  the .NET entry. The test mocks stay valid.

### 6.4 Coverage gates

These are floors that may not be lowered, at Simon's standard or above:

```json
{ "overall": 80,
  "layers": { "Tempo.Certificates.Domain": 95, "Tempo.Certificates.Application": 90,
              "Tempo.Certificates.Infrastructure": 75, "Tempo.Certificates.Api": 75,
              "Tempo.Certificates.Worker": 75, "Tempo.Certificates.Jobs": 70 } }
```

Line and branch coverage both count. The checker is Node 22 (no Python) and has
its own `*.test.mjs`. Generated code (`*.g.cs`) is excluded. Nothing else is.

## 7. Cutover and rollback

The data stays compatible in both directions throughout, because of the
following:

- same rows, fields and EDM types (3.3);
- same nonce scheme;
- same leases and ETags;
- Python and .NET workers can run at the same time safely, because the design
  already coordinates several replicas;
- the API is stateless.

Rollback is therefore always "send traffic or work back". It never needs a
data migration.

**Preconditions.**

- Cut over **while `acmeEnvironment` is still `staging`**, so before #58.
- Cut over **before any device is manufactured with an enrollment URL**.
  `CONFIG_TEMPO_ENROLLMENT_URL` is compiled into firmware. `main.bicep`
  outputs the Container Apps FQDN today, and the Flex URL differs. Question 1
  decides whether a stable custom name (for example `enroll.tempo…` with an App
  Service managed certificate) comes first, so that later compute changes never
  touch device builds.

| Step | Change (each a reviewed PR, deployed by the pipeline) | Verification | Rollback |
| --- | --- | --- | --- |
| C0 | Operator: bootstrap re-run for the new delegable roles. Network subnet merged | Bootstrap outputs | none needed |
| C1 | Deploy Function apps alongside Container Apps. .NET timers disabled with `AzureWebJobs.DnsLane.Disabled=true` and `…DeviceLane.Disabled=true`. API live on its own hostname. Jobs still Python | `verify.sh` decode probe, health, App Insights receives traces, no `worker_cycle_failed` | Remove the module (PR revert) |
| C2 | Jobs switch to the .NET image (`backendImageDigest`) | Bootstrap rerun is idempotent (account exists). Factory dry run with an invalid key gives the expected error. Backup export writes a blob, and a restore dry run of it gives `identical` | Revert the job commands to the Python image (`imageDigest` still present) |
| C3 | `workerRuntime=dotnet`: .NET timers enabled, Container Apps worker `minReplicas: 0` | Due jobs complete: a test-device address change (operator) or the first real job. Alerts quiet for 24 h | `workerRuntime=python` |
| C4 | Device traffic: the firmware build uses the Function or custom URL. No device yet means no live switch | Device acceptance on staging CA: issuance, renewal and address change | Before any device ships: rebuild with the old URL. After a custom name: a CNAME switch (TTL 60) |
| C5 | Soak: at least 7 days and one renewal cycle on staging | No alerts, contract vectors green | as C3 and C4 |
| C6 | Final PR: remove Container Apps `api` and `worker`, `imageDigest`, `services/certificates/`, Python lockfiles and CI steps. Update docs | `what-if` shows only the expected deletes. Note: the preview step currently fails on any `Delete` (`infrastructure-deploy.yaml`), so this needs an explicit, reviewed exception (question 6) | Revert the PR. The Python image digest is still in ACR |

`acmeEnvironment=production` (#58) happens only after C5.

## 8. Observability

- **Event names are copied exactly, with the same text and structured
  suffixes.** They live in a `LoggerMessage` source-generated class
  `CertificateEvents` (Application), category `tempo.certificates`:
  - `worker_cycle_failed lane={Lane}`;
  - `device_job_failed`, `device_job_invalid`, `device_job_lease_lost`,
    `device_job_succeeded operation={Operation}`;
  - `nonce_cleanup_failed admission=denied|worker`,
    `nonce_cleanup_budget_reached limit={Limit}`;
  - from #54: `certificate_expiring_soon count={Count}`,
    `certificate_expiry_check_failed`, `ca_rate_limited`;
  - from #56: `registry_backup_failed`, `registry_backup_completed entities={Count}`,
    `registry_restore_checked|applied missing=… differing=… identical=… created=…`.

  Levels match Python (Warning or Information). A unit test captures the
  rendered strings with a recording logger and compares them with a list that
  also feeds the alert test (below).
- **Redaction is kept.** Exceptions are never logged, and neither are request
  bodies, signatures, addresses, CSRs, provider bodies or URLs. Azure SDK
  logging is off. HTTP dependency auto-collection is disabled in the worker,
  because ACME order URLs and Spaceship query strings must not enter
  telemetry. API request telemetry records route and status only.
- **Where the logs land.**
  - Container Apps jobs (.NET console, `AddSimpleConsole(SingleLine = true)`)
    still write to `ContainerAppConsoleLogs_CL.Log_s`. The backup and restore
    alerts therefore keep working unchanged;
  - Function apps write to workspace-based Application Insights, table
    **`AppTraces`**, column `Message`
    ([table mapping](https://learn.microsoft.com/azure/azure-monitor/app/convert-classic-resource)).
- **Two settings that silently lose events, set explicitly:**
  1. `host.json` `logging.applicationInsights.samplingSettings.excludedTypes`
     includes `Trace`, or sampling is disabled. Otherwise
     count-threshold alerts undercount.
  2. The isolated worker's Application Insights default filter rule that keeps
     only Warning and above is removed. Otherwise
     `device_job_succeeded` and `registry_backup_completed` would not arrive.
     **Verify in Phase 2**: Learn does not name this rule, but the package
     adds it. A test asserts the configured filter options.
- **Alert rules (Agent 4, cross-component).** Each query becomes
  `union (ContainerAppConsoleLogs_CL | where Log_s contains_cs '<e>'), (AppTraces | where Message contains_cs '<e>')`.
  The table must exist when the rule is created, which App Insights guarantees
  after C1. `infrastructure-alerts.test.sh` and `infrastructure-backup.test.sh`
  currently grep Python sources and `python -m` commands, and need to point at
  `backend/src`. The ingestion cap stays at 1 GB a day on the workspace.

## 9. Registry backup (.NET replacement for PR #56)

The `Tempo.Certificates.Jobs backup export|restore` verbs replace
`python -m tempo_certificates.backup export|restore`. They keep PR #56's
resources, identities, schedule, environment variables (`TABLE_ENDPOINT`,
`BACKUP_BLOB_ENDPOINT`, `BACKUP_CONTAINER`, `DEVICE_SUFFIX`, `RESTORE_BLOB`,
`RESTORE_APPLY`, `AZURE_CLIENT_ID`), exit codes (0, 1, 2) and printed lines.

- **Export:**
  - `TableClient.QueryAsync<TableEntity>()` on `DeviceRegistry`;
  - sort by `(PartitionKey, RowKey)`;
  - the document is `{"entities":[…],"exported_at":N,"format":"tempo-device-registry-v1"}`
    with recursively sorted keys and compact separators;
  - an Int64 value becomes `{"edm":"Edm.Int64","value":N}`, matching Python's
    `EntityProperty` encoding. Other values stay plain;
  - blob name `device-registry/YYYY/MM/DD/YYYYMMDDTHHMMSSZ.json` (UTC);
  - `BlobClient.UploadAsync` with `Conditions = { IfNoneMatch = ETag.All }`,
    Entra token only, no SAS or shared key.
- **Restore:**
  - with no `RESTORE_BLOB`, list the ten newest names;
  - the name must match the regex;
  - parse the document. Reject the whole backup unless every row has
    `RowKey == "identity"`, `PartitionKey == DeviceId(public_key)`,
    `hostname == DeviceHostname(id, suffix)` and a boolean `enabled`;
  - compare with the live registry. With `RESTORE_APPLY=yes`, **only add
    missing rows** (`AddEntityAsync`; a 409 means it was admitted concurrently,
    so skip). Never update. `DeviceState` is untouched.
- **Format compatibility in both directions** is a test requirement. A
  committed backup document produced by PR #56's Python exporter, its test
  fixture, restores in .NET. A .NET export parses with the Python `parse`
  rules, checked by the temporary contract runner if question 3 is approved,
  otherwise by fixture review.
- Tests port `tests/test_backup.py` one to one (10 tests): typed export and UTC
  naming; a round trip that creates only missing rows and keeps later
  disables; concurrent admission; tamper rejection; redacted failure log; list,
  bad name and dry-run defaults; create-only blob client.
- Infrastructure: PR #56 Bicep unchanged except the job `image` and `command`.

## 10. Cost estimate (West Europe, list prices)

Prices come from the Azure Retail Prices API on 2026-11-28 (USD, before tax).
Free grants are as the API reports them through tier thresholds. Private
endpoint pricing (US$0.01/hour) and Container Apps free grants come from the
public pricing pages and were not returned by the API. **These are estimates
from assumed workloads.**

| Item | Today (Container Apps) | After (Flex) |
| --- | --- | --- |
| Worker | 0.25 vCPU, 0.5 GiB, always one replica: 648k vCPU-s and 1.296M GiB-s a month. At idle rates (vCPU 0.000004, GiB 0.000004), less the free grant of 180k vCPU-s and 360k GiB-s, **about US$5.6**. If mostly active (vCPU 0.000034), **about US$19.7** | 2 lanes × 2,880 runs a day = 172,800 executions a month (free up to 250,000) plus 720 expiry checks. GB-s at 512 MB with a 1 s minimum ≈ 86,400 (free up to 100,000, then US$0.000026). **About US$0**, rising by about US$0.26 per extra 10k GB-s |
| API | Scale to zero, a few requests a day per device: **about US$0** | Same execution model, within the same free grant: **about US$0** |
| Always-ready API instance (optional, removes cold start) | none | 0.5 GB × 2.592M s × 0.000004 = **US$5.18** a month (not proposed by default) |
| Host storage (2 × LRS, KB-scale) | none | **under US$0.10** |
| Private endpoints for host storage | none | 2 × about US$7.30 = **about US$14.60** (+US$7.30 each if table or queue sub-resources prove necessary) |
| ACR Basic | US$0.1666 a day ≈ **US$5.00** | unchanged (jobs and packages) |
| Container Apps environment and jobs | idle, about US$0 | unchanged |
| Logs (workspace, 5 GB a month free, then US$2.99/GB, 1 GB/day cap) | small | small. App Insights adds a little trace volume, well under the free allowance at fleet scale |
| **Total, compute-related** | **about US$10.6 to 24.7 a month** | **about US$19.7 to 20 a month** (about US$25 with always-ready) |

**Conclusion:** Flex does not save money at this scale. The two private
endpoints required by per-app host storage cost about as much as the always-on
worker replica they replace. The move is justified by Simon's standard stack,
not by cost. A cheaper variant, putting host storage on public endpoints with
Entra-only access, would save about US$14.60 a month but breaks the private
data-plane baseline. It is not recommended. Per Simon's operating model, any
new paid service needs an explicit decision, so question 4 asks for one.

## 11. PR plan

Every PR stays well below 100,000 bytes of diff against `main`, measured with
`git diff origin/main...HEAD | wc -c` before opening or updating it, and is
split further if needed. Every PR carries its tests (Red, Green, Refactor) and
a changeset where `changeset-check.sh` requires one.

| # | Branch | Content | Estimate | Depends on |
| --- | --- | --- | --- | --- |
| 1 | `docs/certificate-backend-dotnet-design` | This document | about 61 KB | none |
| 2 | `ci/backend-dotnet-skeleton` | `global.json`, solution, empty projects, architecture tests, `test-backend.sh`, Node coverage checker and its test, `Backend Tests` job, test-all and hook wiring | about 30 KB | 1 approved; cross-component OK for `test-all.sh` |
| 3 | `feat/backend-domain-protocol` | Domain: decoder, signature, CSR, address, ids, admission. `vectors.json` (decode and auth cases) | about 70 KB | 2 |
| 4 | `feat/backend-application` | Application: request handler, job processor, cycle, secret cache, cloud issuer logic, events. Fakes and ported tests | about 75 KB | 3 |
| 5 | `feat/backend-tables-keyvault-dns` | Infrastructure: `TableDeviceStore` and codec (with Azurite tests), Key Vault, Spaceship | about 70 KB | 4 |
| 6 | `feat/backend-acme` | ACME spike first (section 3.1 checks), issuer, chain verifier, TXT probe | about 60 KB | 5 |
| 7 | `feat/backend-function-hosts` | Api and Worker hosts, `host.json`, full contract vectors, temporary Python vector runner (if approved) | about 70 KB | 6 |
| 8 | `feat/backend-jobs` | Jobs host: factory, bootstrap, backup and restore. Dockerfile. `certificate-image.sh` and workflow | about 70 KB | 7, and #56 merged |
| 9 | `feat/infra-functions-parallel` | `functions.bicep`, compute and main parameters, template guard test (C1 and C2) | about 35 KB | 8; subnet, delegation and deploy-step requests done |
| 10 | `feat/infra-worker-cutover` | `workerRuntime=dotnet` (C3) | about 3 KB | 9 deployed and verified |
| 11a | `refactor/remove-python-certificate-tests` | C6 part 1: delete `services/certificates/tests/`, the temporary vector runner, `requirements*.txt`, `requirements-test.in`, `lock_dependencies.py`, `test-certificates.sh` and the Python CI and audit steps | about 50 KB of deletions | C5 soak complete |
| 11b | `refactor/remove-python-certificate-service` | C6 part 2: delete `tempo_certificates/`, the Dockerfile, the Container Apps api and worker, and `imageDigest`. Update `DEVICE_CERTIFICATE_SERVICE.md` and `CERTIFICATE_IMAGE_DELIVERY.md` | about 60 KB | 11a |

Deleted lines count towards the diff. `services/certificates/` is about 92 KB today, and about 105 KB with #56, so removing it is split into 11a and 11b from the start.

## 12. Requests outside my ownership (summary)

1. `infra/certificates/network.bicep`: `functions` subnet `10.62.3.0/26`,
   delegated to `Microsoft.App/environments`, plus the `privatelink.blob` zone
   (#56).
2. `infra/certificates/bootstrap.bicep`: two `compute` delegable roles, and an
   operator bootstrap re-run before C1.
3. `infra/certificates/ops.bicep`, `infrastructure-alerts.test.sh` and
   `infrastructure-backup.test.sh` (Agent 4): `AppTraces` union, event-source
   greps moved to `backend/src`, backup job command.
4. `.github/workflows/infrastructure-deploy.yaml` and
   `scripts/infrastructure/verify.sh`: the code-deploy and probe steps (6.3).
5. `scripts/test-all.sh`, `.github/scripts/certificate-image.test.sh` and
   `.github/scripts/infrastructure-changes.sh`: backend wiring.
6. PR #59 factory registration script: none needed, provided the factory job
   keeps its name and environment variables (D1).

## 13. Open decisions (needed before Phase 2)

1. **Enrollment URL.** Either adopt a stable custom name such as
   `enroll.tempo.tinyrobotcompany.io` (custom domain and managed certificate on
   the API app, with one-time Spaceship CNAME and `asuid` TXT records set by an
   operator) before any device is built, or compile the plain Function app URL
   into firmware. The custom name is recommended: it makes rollback a DNS
   change, and future compute changes never touch device builds. Firmware code
   is unchanged either way; only the `CONFIG_TEMPO_ENROLLMENT_URL` value
   differs.
2. **D1.** Keep factory, bootstrap, backup and restore as Container Apps jobs
   running .NET, rather than Function apps.
3. **Temporary Python contract runner** (section 4.3), deleted in PR 11a.
4. **Cost and new paid resources** (section 10): two Flex apps, two
   host-storage accounts with private endpoints, and Application Insights.
   Approve, and say whether an ADR file is wanted in addition to this
   document.
5. **Optional live smoke test** with a dedicated test identity registered by
   the factory job (an operator action).
6. **C6 deletes.** The infrastructure preview gate rejects any `Delete`. A
   reviewed, one-time allowance is needed for removing the Container Apps api
   and worker.
