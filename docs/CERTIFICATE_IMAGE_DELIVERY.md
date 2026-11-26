# Certificate service image delivery

Merge the Tempo-only backend correction (#44) before this pipeline (#34). Publishing
refuses source containing the retired Waveform protocol or configuration.

A main merge changing the service or image workflow starts validation, then protected
publication. The existing backend tests and dependency audit run before Azure access.
The publication path is GitHub Actions YAML and Bash/Azure CLI, with no Python
infrastructure helper. The backend application itself retains its existing runtime.

The publisher reads `containerRegistry` from the successful `tempo-infrastructure`
Bicep deployment. It builds the checked-out main commit with ACR Tasks for Linux
amd64, using a unique tag per workflow attempt. The completed ARM build result must
have Succeeded and contain exactly the expected image and a valid SHA-256 digest.
No registry administrator credentials or additional registry-reader grant is needed
for that ARM result. Stale main and wrong subscription/tenant checks run before and
after the build. A failed build cannot emit the promotable artifact.

The `certificate-image-<run>-<attempt>` artifact records source SHA, immutable image
reference and imageDigest. Review the digest, then change `imageDigest` in
`infra/certificates/main.bicep` through a PR. That Bicep merge triggers infrastructure
deployment. Building an image does not itself deploy anything or alter DNS/secrets.

No manual dispatch or automatic PR merge is added. The existing GitHub environment
requires approval before Azure publication. Live build and backend/device acceptance
remain necessary; mocked CLI tests do not establish those outcomes.
