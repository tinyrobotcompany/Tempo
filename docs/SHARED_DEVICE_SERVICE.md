# Shared Tempo / Waveform device service

The existing service supports both products without a second Azure stack. Tempo
owns this backend and its deployment pipeline; Waveform must not independently
deploy these resources. Normal phone traffic remains direct HTTPS to the device.
No audio, recognition credentials or private device keys are sent to this service.

## Product authorization and compatibility

Existing Tempo requests, signatures, registry rows and hostnames remain valid.
Missing `product` means `tempo`. Waveform requests include `"product":"waveform"`
and use the signature context `WAVEFORM-DEVICE-V1\nPOST\n/v1/device\n`, rather than
Tempo's context. Both sign the exact UTF-8 body with ECDSA P-256/SHA-256 and send a
base64 DER signature. New clients use `X-Device-Signature`; existing
`X-Tempo-Signature` remains supported. Replay checks remain transactional.

Only manufacturing can assign a product in DeviceRegistry. DeviceState cannot
change it. The API compares the signed product against that registration before
scheduling work. Each registration gets one namespace: `DEVICE_SUFFIX` for Tempo,
`WAVEFORM_DEVICE_SUFFIX` for Waveform. The CSR must contain exactly that device's
CN and single DNS SAN. A key cannot be registered a second time for another product.

Factory job arguments for Waveform: `--product waveform --serial <serial>` with
`MANUFACTURING_PUBLIC_KEY` containing only the base64 public PEM. The factory job's
existing managed identity and private connectivity are reused. Private keys stay
on the device. `--suffix`, if provided, must equal the configured product suffix.

## Resources and verified deployment state

Read-only inventory on 2026-11-25 in subscription
`923dd002-44bc-431c-b986-464d2ca524a2` found staging storage
`sttempo22bw55qnmg6pm`, registry `acrtempo22bw55qnmg6pm`, Key Vault
`kv-tempo-22bw55qnmg6pm`, identities, network/private endpoints and logs. It found
no deployed Container Apps environment, API, worker or factory job. Thus no live
shared enrollment endpoint or capacity is claimed by this implementation.

Keep the existing concern-separated groups: network, data, security, ops, compute.
The main infrastructure templates target `rg-tempo-westeurope-<concern>` while
most resources were still in `rg-tempo-staging-westeurope-<concern>` at inspection.
Complete the existing reviewed migration before applying the target templates;
do not create a duplicate foundation as a shortcut. No additional resource type,
resource group, replica allocation or identity is introduced for Waveform here.

## Cost and admission controls

Each API process permits a burst of 60 requests and sustained five requests/second
before body parsing/storage access. With the existing maximum of three API replicas,
that is a bounded per-deployment admission rate, not a global distributed quota or
DDoS protection. An attacker can consume admission capacity; monitor 429s and size
limits against real device demand before rollout. Existing signed-device quotas
remain in place. The gate has constant memory and does not trust forwarded IPs.

The existing single worker replica runs independent certificate and DNS loops,
with separate clients and existing durable leases. Slow CA issuance cannot block
new DNS scheduling. This adds no second always-on worker resource. DNS exact-name
lookup uses the provider's documented `orderBy=name` and binary search over pages;
zone-size changes or inconsistent ordering fail closed for retry. Confirm ordering
against the real DNS provider during staging acceptance. Large duplicate record
sets are bounded separately from total fleet size.

Waveform polls pending work every 30 seconds and otherwise checks daily with jitter,
or when its address/renewal deadline changes. Existing Tempo firmware retains its
own polling schedule until separately updated; this backend cannot eliminate those
old clients' requests. Shared registered-domain CA issuance limits still apply.

## Rollout, validation and rollback

1. Merge the reviewed backend changes without enabling new production enrollment.
2. Complete the existing foundation migration and image-delivery/bootstrap procedure.
   Review infrastructure what-if: existing resources are reused, no second stack.
3. Activate the existing API/worker once, using the approved image digest and DNS/CA
   configuration. This activates compute costs absent from the inspected inventory.
4. Test legacy Tempo and new Waveform enrollment in staging, cross-product denial,
   revocation, replay, DNS changes, CA delay, renewal and HTTP throttling. Staging CA
   certificates are deliberately not trusted by customer Waveform firmware.
5. Promote the reviewed service configuration to the production CA, register a
   factory-provisioned Waveform test device and use the enrollment URL output in
   its manufacturing build. Do not guess or hardcode an undeployed URL.
6. Validate two successive signed production OTA installs and concurrent recognition,
   TLS and OTA on hardware. Preserve existing Tempo access throughout.

Rollback the API and worker image together to the prior reviewed digest if needed.
Tempo's existing rows remain compatible. Older images do not support Waveform's
protocol, so Waveform renewal pauses until the shared image is restored; valid
installed certificates and core local operation continue. Do not delete registry
records, keys or DNS records during an application rollback. Restore configuration
from the captured pre-deployment state; never recreate the foundation to roll back.

No Azure deployment, DNS write, factory admission, eFuse programming or hardware
acceptance is performed by these source changes.
