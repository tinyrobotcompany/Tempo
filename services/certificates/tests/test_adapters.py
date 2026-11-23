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


def test_distinct_dns_lane_remains_available_during_ca_backoff(setup):
    key, store, service = setup
    certificate_row = store.row
    certificate_row.update(state="pending", next_attempt=NOW + 10000)
    dns_row = dict(certificate_row, RowKey="dns", state="idle", next_attempt=0)
    original_get = store.get
    store.get = lambda identifier, lane="device": dict(dns_row) if lane == "dns" else original_get(identifier)
    accepted = []
    store.accept = lambda row, nonce, expiry: accepted.append(dict(row)) or True
    reply = service.handle(*signed(key, store, "address", address="192.168.1.34"))
    assert reply.status == 202 and accepted[0]["RowKey"] == "dns"
    assert accepted[0]["desired_address"] == "192.168.1.34"
    assert certificate_row["next_attempt"] == NOW + 10000


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
