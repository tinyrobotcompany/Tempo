import datetime
from unittest.mock import Mock

import pytest
from azure.core.exceptions import ResourceNotFoundError
from azure.data.tables import TableTransactionError
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

from tempo_certificates.issuer import verify_chain
from tempo_certificates.protocol import Rejected
from tempo_certificates.store import TableStore
from tempo_certificates.runtime import CloudIssuer
from tempo_certificates.worker import Worker
from test_service import NOW, SUFFIX, csr, setup, signed


class Entity(dict):
    metadata = {"etag": 'W/"revision"'}


def test_registry_is_separate_and_never_writable_by_device_requests():
    registry, state = Mock(), Mock()
    registry.get_entity.return_value = Entity(public_key="public", hostname="name", enabled=True)
    state.get_entity.side_effect = ResourceNotFoundError()
    store = TableStore(state, registry)
    row = store.get("id")
    assert row["etag"] is None
    assert store.accept(row, "a" * 64, NOW + 600)
    operations = state.submit_transaction.call_args.args[0]
    assert operations[0][0] == "create" and operations[1][0] == "create"
    state.submit_transaction.assert_called_once()
    assert operations[0][1]["PartitionKey"] == operations[1][1]["PartitionKey"] == "id"
    assert operations[0][1]["RowKey"] != operations[1][1]["RowKey"]
    assert "public_key" not in operations[1][1] and "enabled" not in operations[1][1]
    registry.create_entity.assert_not_called()
    registry.update_entity.assert_not_called()


def transaction_error(status, index):
    error = TableTransactionError(message="test", index=index)
    error.status_code = status
    return error


def test_conflict_and_replay_have_distinct_atomic_outcomes():
    table = Mock()
    store = TableStore(table, Mock())
    row = {"PartitionKey": "id", "RowKey": "device", "etag": 'W/"1"'}
    table.submit_transaction.side_effect = transaction_error(412, 1)
    assert store.accept(row, "a" * 64, NOW) is False
    table.submit_transaction.side_effect = transaction_error(409, 0)
    with pytest.raises(Rejected, match="replayed_request"):
        store.accept(row, "a" * 64, NOW)
    table.submit_transaction.side_effect = transaction_error(503, 0)
    with pytest.raises(TableTransactionError):
        store.accept(row, "a" * 64, NOW)


def test_real_certificate_chain_key_name_and_expiry_validation():
    now = datetime.datetime.fromtimestamp(NOW, datetime.timezone.utc)
    root_key = ec.generate_private_key(ec.SECP256R1())
    root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "test root")])
    root = (x509.CertificateBuilder().subject_name(root_name).issuer_name(root_name)
            .public_key(root_key.public_key()).serial_number(1)
            .not_valid_before(now - datetime.timedelta(days=1)).not_valid_after(now + datetime.timedelta(days=100))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
            .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), False)
            .sign(root_key, hashes.SHA256()))
    name = "d-" + "a" * 32 + "." + SUFFIX
    leaf_key = ec.generate_private_key(ec.SECP256R1())
    request = csr(name, leaf_key)
    leaf = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)]))
            .issuer_name(root_name).public_key(leaf_key.public_key()).serial_number(2)
            .not_valid_before(now - datetime.timedelta(minutes=1)).not_valid_after(now + datetime.timedelta(days=30))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), False)
            .add_extension(x509.SubjectAlternativeName([x509.DNSName(name)]), False)
            .sign(root_key, hashes.SHA256()))
    chain = leaf.public_bytes(serialization.Encoding.PEM).decode()
    result = verify_chain(chain, request, name, NOW, [root])
    assert NOW < result["renew_after"] < result["expires_at"]
    with pytest.raises(ValueError, match="certificate_key_mismatch"):
        verify_chain(chain, csr(name), name, NOW, [root])
    with pytest.raises(Exception):
        verify_chain(chain, request, name, NOW + 31 * 86400, [root])


def test_dns_worker_does_not_contact_ca_or_require_acme_secrets(setup):
    _, store, _ = setup
    store.row.update(RowKey="dns", operation="address", desired_address="192.168.1.34",
                     state="pending", next_attempt=NOW, job_id="job", attempts=0)
    vault, dns = Mock(), Mock()
    vault.get_secret.side_effect = RuntimeError("CA unavailable")
    issuer = CloudIssuer(vault, "production", dns)
    Worker(store, issuer, dns, lambda: NOW).process(store.get(store.row["PartitionKey"]))
    assert store.row["state"] == "ready" and store.row["address"] == "192.168.1.34"
    vault.get_secret.assert_not_called()
    dns.set_address.assert_called_once()


class TableFake:
    """Table entity/ETag contract double; not evidence of Azure's server atomicity."""
    def __init__(self):
        self.rows = {}
        self.revision = 0

    def get_entity(self, partition, row):
        import copy
        try:
            return copy.deepcopy(self.rows[(partition, row)])
        except KeyError as error:
            raise ResourceNotFoundError() from error

    def submit_transaction(self, operations):
        import copy
        proposed = copy.deepcopy(self.rows)
        for index, operation in enumerate(operations):
            method, value = operation[:2]
            key = value["PartitionKey"], value["RowKey"]
            if method == "create" and key in proposed:
                raise transaction_error(409, index)
            if method == "update" and (key not in proposed or proposed[key].metadata["etag"] != operation[2]["etag"]):
                raise transaction_error(412, index)
            entity = Entity(value)
            self.revision += 1
            entity.metadata = {"etag": str(self.revision)}
            proposed[key] = entity
        self.rows = proposed

    def update_entity(self, value, **options):
        self.submit_transaction([("update", value, options)])

    def query_entities(self, query, parameters, results_per_page):
        if "next_attempt" in query:
            return iter([dict(row) for row in self.rows.values()
                         if row["RowKey"] in ("device", "dns") and row.get("next_attempt", parameters["now"]+1) <= parameters["now"]
                         and row.get("state") in ("pending", "running")])
        return iter([dict(row) for row in self.rows.values()
                     if row["RowKey"].startswith("nonce-") and row["expires"] < parameters["now"]
                     and ("identifier" not in parameters or row["PartitionKey"] == parameters["identifier"])])

    def delete_entity(self, partition, row):
        self.rows.pop((partition, row), None)


def test_real_store_keeps_first_and_existing_address_jobs_in_dns_lane(setup):
    from tempo_certificates.service import DeviceService
    key, memory, _ = setup
    registry, table = Mock(), TableFake()
    registry.get_entity.return_value = Entity(memory.row)
    store = TableStore(table, registry)
    service = DeviceService(store, SUFFIX, clock=lambda: NOW)
    identifier = memory.row["PartitionKey"]
    certificate = store.get(identifier)
    certificate.update(certificate="existing-chain", state="pending", next_attempt=NOW+10000)
    store.accept(certificate, "seed", NOW+600)
    issuer, dns_adapter = Mock(), Mock()
    for address in ("192.168.1.34", "192.168.1.35"):
        before = dict(table.rows[(identifier, "device")])
        reply = service.handle(*signed(key, memory, "address", address=address))
        assert reply.status == 202
        dns = store.get(identifier, "dns")
        assert dns["RowKey"] == "dns" and dns["operation"] == "address"
        assert dns["desired_address"] == address
        assert not {"certificate", "csr", "renew_after", "expires_at"} & dns.keys()
        assert dict(table.rows[(identifier, "device")]) == before
        Worker(store, issuer, dns_adapter, lambda: NOW).run_once()
        issuer.issue.assert_not_called()
        status = service.handle(*signed(key, memory))
        assert status.body["address"] == address and status.body["dns_state"] == "ready"
        assert status.body["certificate"] == "existing-chain"
        repeated = service.handle(*signed(key, memory, "address", address=address))
        assert repeated.body["certificate"] == "existing-chain" and repeated.body["address"] == address
        assert "csr" not in store.get(identifier, "dns")
        # Allow a fresh address request without waiting for the worker cooldown.
        completed = store.get(identifier, "dns")
        completed["retry_after"] = 0
        assert store.replace(completed)
    assert dns_adapter.set_address.call_count == 2


def test_api_cleanup_is_bounded_scoped_and_independent_of_worker(setup):
    from tempo_certificates.service import DeviceService
    key, memory, _ = setup
    registry, table = Mock(), TableFake()
    registry.get_entity.return_value = Entity(memory.row)
    identifier = memory.row["PartitionKey"]
    for partition in (identifier, "other"):
        for index in range(40):
            row = Entity(PartitionKey=partition, RowKey=f"nonce-{index}", expires=NOW - 1)
            table.rows[(partition, row["RowKey"])] = row
    store = TableStore(table, registry)
    DeviceService(store, SUFFIX, clock=lambda: NOW).handle(*signed(key, memory))
    own = [r for (p, _), r in table.rows.items() if p == identifier and r["RowKey"].startswith("nonce-")]
    assert len(own) == 9  # 40 old - capped 32 deletions + one newly accepted request.
    assert len([p for p, _ in table.rows if p == "other"]) == 40


def test_cleanup_failure_prevents_more_nonce_admission(setup):
    key, store, service = setup
    store.expire_nonces = Mock(side_effect=RuntimeError("storage unavailable"))
    with pytest.raises(RuntimeError):
        service.handle(*signed(key, store))
    assert not store.nonces


def test_failed_state_etag_does_not_consume_nonce_in_batch_contract():
    table = TableFake()
    store = TableStore(table, Mock())
    row = dict(PartitionKey="id", RowKey="device", etag=None)
    assert store.accept(row, "first", NOW + 600)
    row["etag"] = "stale"
    assert not store.accept(row, "retry", NOW + 600)
    assert ("id", "nonce-retry") not in table.rows
    row["etag"] = table.rows[("id", "device")].metadata["etag"]
    assert store.accept(row, "retry", NOW + 600)
    with pytest.raises(Rejected, match="replayed_request"):
        store.accept(row, "retry", NOW + 600)


def test_worker_cleanup_precedes_candidate_failure():
    store = Mock()
    events = []
    store.expire_nonces.side_effect = lambda now: events.append("cleanup")
    store.candidates.side_effect = RuntimeError("enumeration unavailable")
    with pytest.raises(RuntimeError):
        Worker(store, Mock(), Mock(), lambda: NOW).run_once()
    assert events == ["cleanup"]


def test_worker_cleanup_failure_does_not_skip_jobs(caplog):
    store = Mock()
    store.expire_nonces.side_effect = RuntimeError("cleanup unavailable")
    store.candidates.return_value = []
    Worker(store, Mock(), Mock(), lambda: NOW).run_once()
    store.candidates.assert_called_once_with(NOW)
    assert "nonce_cleanup_failed" in caplog.text


def test_worker_cleans_nonces_even_when_vault_is_unavailable():
    from tempo_certificates.runtime import run_cycle
    store, vault = Mock(), Mock()
    vault.get_secret.side_effect = RuntimeError("vault unavailable")
    with pytest.raises(RuntimeError):
        run_cycle(store, vault, {})
    store.expire_nonces.assert_called_once()
    store.candidates.assert_not_called()


def test_nonce_cleanup_preserves_live_records_and_expiry_boundary():
    table = TableFake()
    for name, expires in (("old", NOW-1), ("boundary", NOW), ("live", NOW+600)):
        row = Entity(PartitionKey="device", RowKey="nonce-"+name, expires=expires)
        table.rows[("device", row["RowKey"])] = row
    assert TableStore(table, Mock()).expire_nonces(NOW, "device", limit=32) == 1
    assert set(table.rows) == {("device", "nonce-boundary"), ("device", "nonce-live")}


def test_authentication_precedes_nonce_admission_and_status_retries_are_fresh(setup):
    from tempo_certificates.service import DeviceService
    key, memory, _ = setup
    registry, table = Mock(), TableFake()
    registry.get_entity.return_value = Entity(memory.row)
    store = TableStore(table, registry)
    service = DeviceService(store, SUFFIX, clock=lambda: NOW)
    forged = ec.generate_private_key(ec.SECP256R1())
    with pytest.raises(Rejected, match="invalid_signature"):
        service.handle(*signed(forged, memory))
    assert not table.rows  # Authentication failed before any admission write.

    submit = table.submit_transaction
    attempts = []
    def contend_once(operations):
        attempts.append(operations)
        if len(attempts) == 1:
            raise transaction_error(412, 1)  # Entire batch rolled back, nonce included.
        return submit(operations)
    table.submit_transaction = contend_once
    request = signed(key, memory)
    assert service.handle(*request).status == 200
    assert len(attempts) == 2
    assert len([row for _, row in table.rows if row.startswith("nonce-")]) == 1
    # Simulate losing that HTTP response: replay is rejected, a newly signed retry succeeds.
    with pytest.raises(Rejected, match="replayed_request"):
        service.handle(*request)
    assert service.handle(*signed(key, memory)).status == 200
