# Protected identity storage foundation

This prerequisite provides the storage boundary for PR #25. It does not activate
enrollment, migrate existing devices, change existing phone TLS behavior or program
hardware. The consumer PR must use these APIs before claiming protected identities.

`station_identity_store_init()` requires Secure Boot, flash encryption, and an
existing HMAC_UP key whose value and purpose are locked and whose value cannot be
read. Manufacturing chooses its slot explicitly through
`CONFIG_TEMPO_IDENTITY_HMAC_KEY_ID`; default -1 refuses initialization. No slot is
assumed, no eFuse is burned, and no failed partition is erased.

The hardware HMAC derives two domain-separated 256-bit XTS keys for the dedicated
`phone_tls` partition. Only `nvs_flash_secure_init_partition` initializes it. Labels
are storage-format constants. The encryption configuration is wiped from the stack.
Existing plaintext partitions require a separately reviewed manufacturing migration.

The reserved PSA IDs 0x30000001 and 0x30000002 use this encrypted backend, never the
SDK default NVS backend. Configuration mismatches fail compilation. Records are
bounded to 512 bytes, storage errors propagate, and runtime deletion is refused.
The identity API generates non-exportable persistent keys and uses opaque handles
for CSR creation, signatures and certificate matching. It rejects existing keys
whose type, algorithm, persistence or export policy does not match. PR #25 supplies
the enrollment worker and switches the HTTPS consumer to key handles.

This is software cryptography over encrypted storage, not secure-element isolation.
Privileged malicious firmware can still misuse keys and access RAM. Firmware trust,
physical provisioning, recovery, and real-device interrupted-write tests remain
release prerequisites. Host tests validate API selection and errors, not actual
flash confidentiality. No hardware or cloud activation is part of this PR.

Reference: [ESP-IDF NVS encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/storage/nvs_encryption.html).
