import base64
import copy
import json
import secrets

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID
from fastapi.testclient import TestClient

from tempo_certificates.app import create_app
from tempo_certificates.dns import Spaceship
from tempo_certificates.protocol import CONTEXT, Rejected, device_id, hostname, validate_csr
from tempo_certificates.service import DeviceService
from tempo_certificates.worker import Worker

NOW = 1800000000
SUFFIX = "tempo.tinyrobotcompany.io"


class MemoryStore:
    """Transactional storage test double, never selected by production runtime."""
    def __init__(self, row):
        self.row = dict(row, etag=0)
        self.nonces = set()

    def get(self, identifier, lane="device"):
        return copy.deepcopy(self.row) if identifier == self.row["PartitionKey"] else None

    def accept(self, row, nonce, expires):
        if nonce in self.nonces:
            raise Rejected("replayed_request", 409)
        if row["etag"] != self.row["etag"]:
            return False
        self.nonces.add(nonce)
        return self.replace(row)

    def replace(self, row):
        if row["etag"] != self.row["etag"]:
            return False
        self.row = dict(copy.deepcopy(row), etag=row["etag"] + 1)
        return True


@pytest.fixture
def setup():
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    identifier = device_id(pem)
    name = hostname(identifier, SUFFIX)
    store = MemoryStore({"PartitionKey": identifier, "RowKey": "device", "public_key": pem,
                         "hostname": name, "enabled": True, "state": "idle"})
    service = DeviceService(store, SUFFIX, clock=lambda: NOW)
    return key, store, service


def signed(key, store, action="status", **fields):
    payload = dict(device_id=store.row["PartitionKey"], action=action,
                   nonce=secrets.token_hex(32), issued_at=NOW, **fields)
    body = json.dumps(payload, separators=(",", ":")).encode()
    signature = base64.b64encode(key.sign(CONTEXT + body, ec.ECDSA(hashes.SHA256()))).decode()
    return body, signature


def csr(name, key=None, names=None):
    key = key or ec.generate_private_key(ec.SECP256R1())
    return x509.CertificateSigningRequestBuilder().subject_name(x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, name)])).add_extension(
        x509.SubjectAlternativeName([x509.DNSName(n) for n in (names or [name])]), False
    ).sign(key, hashes.SHA256()).public_bytes(serialization.Encoding.PEM).decode()


def test_enrollment_and_renewal_are_signed_csr_only(setup):
    key, store, service = setup
    request = signed(key, store, "certificate", csr=csr(store.row["hostname"]))
    reply = service.handle(*request)
    assert reply.status == 202 and store.row["state"] == "pending"
    assert "PRIVATE KEY" not in json.dumps(store.row)


def test_replay_cannot_reschedule_work(setup):
    key, store, service = setup
    body, signature = signed(key, store)
    service.handle(body, signature)
    service.clock = lambda: NOW + 3
    with pytest.raises(Rejected, match="replayed_request"):
        service.handle(body, signature)


def test_other_device_cannot_impersonate_registered_key(setup):
    _, store, service = setup
    forged = ec.generate_private_key(ec.SECP256R1())
    with pytest.raises(Rejected, match="invalid_signature"):
        service.handle(*signed(forged, store))
    assert not store.nonces


def test_revoked_identity_is_denied(setup):
    key, store, service = setup
    store.row["enabled"] = False
    with pytest.raises(Rejected, match="device_not_enrolled"):
        service.handle(*signed(key, store))


@pytest.mark.parametrize("name", ["example.com", "*.tempo.tinyrobotcompany.io", "device-001.tempo.tinyrobotcompany.io"])
def test_csr_cannot_select_another_hostname(setup, name):
    key, store, service = setup
    with pytest.raises(Rejected, match="invalid_csr"):
        service.handle(*signed(key, store, "certificate", csr=csr(name)))
    assert not store.nonces


def test_multi_name_csr_denied(setup):
    _, store, _ = setup
    name = store.row["hostname"]
    with pytest.raises(Rejected, match="invalid_csr"):
        validate_csr(csr(name, names=[name, "example.com"]), name)


@pytest.mark.parametrize("address", ["8.8.8.8", "127.0.0.1", "169.254.169.254", "::1", "224.0.0.1", "100.64.0.1", 123])
def test_address_must_be_rfc1918(setup, address):
    key, store, service = setup
    with pytest.raises(Rejected, match="invalid_lan_address"):
        service.handle(*signed(key, store, "address", address=address))


def test_request_bound_to_exact_bytes_and_path(setup):
    key, store, service = setup
    body, signature = signed(key, store)
    with pytest.raises(Rejected, match="invalid_signature"):
        service.handle(body + b" ", signature)
    wrong = base64.b64encode(key.sign(body, ec.ECDSA(hashes.SHA256()))).decode()
    with pytest.raises(Rejected, match="invalid_signature"):
        service.handle(body, wrong)


def test_clock_skew_and_unknown_fields_rejected(setup):
    key, store, service = setup
    body, signature = signed(key, store)
    service.clock = lambda: NOW + 301
    with pytest.raises(Rejected, match="stale_request"):
        service.handle(body, signature)
    with pytest.raises(Rejected, match="invalid_fields"):
        service.handle(*signed(key, store, hostname="example.com"))


def test_busy_request_does_not_replace_csr(setup):
    key, store, service = setup
    first = csr(store.row["hostname"])
    service.handle(*signed(key, store, "certificate", csr=first))
    service.clock = lambda: NOW + 3
    service.handle(*signed(key, store, "certificate", csr=csr(store.row["hostname"])))
    assert store.row["csr"] == first


def test_valid_certificate_returned_instead_of_early_reissuance(setup):
    key, store, service = setup
    store.row.update(state="ready", certificate="existing", renew_after=NOW + 1000)
    reply = service.handle(*signed(key, store, "certificate", csr=csr(store.row["hostname"])))
    assert reply.body["certificate"] == "existing" and store.row["state"] == "ready"


def test_api_limits_and_error_redaction(setup):
    key, store, service = setup
    client = TestClient(create_app(service))
    assert client.post("/v1/device", content=b"x" * 6145, headers={"content-type": "application/json"}).status_code == 413
    assert client.post("/v1/device", content=b"{}").status_code == 415
    body, signature = signed(key, store)
    reply = client.post("/v1/device", content=body, headers={"content-type": "application/json", "x-tempo-signature": signature})
    assert reply.status_code == 200 and reply.headers["cache-control"] == "no-store"
    service.handle = lambda *args: (_ for _ in ()).throw(RuntimeError("secret provider detail"))
    reply = client.post("/v1/device", content=body, headers={"content-type": "application/json"})
    assert reply.status_code == 503 and "secret" not in reply.text


class DNS:
    def __init__(self):
        self.removed, self.addresses = [], []

    def remove_challenge(self, name, value):
        self.removed.append((name, value))

    def set_address(self, name, address):
        self.addresses.append((name, address))


def test_worker_recovers_crashed_challenge_and_commits_result(setup):
    key, store, service = setup
    service.handle(*signed(key, store, "certificate", csr=csr(store.row["hostname"])))
    store.row["challenge"] = "orphan"
    dns = DNS()

    class Issuer:
        def issue(self, name, request, checkpoint):
            checkpoint({"challenge": "current"})
            return {"certificate": "chain", "renew_after": NOW + 10000, "expires_at": NOW + 20000}

    Worker(store, Issuer(), dns, lambda: NOW).process(store.get(store.row["PartitionKey"]))
    assert store.row["state"] == "ready" and store.row["certificate"] == "chain"
    assert [value for _, value in dns.removed] == ["orphan", "current"]


def test_failed_worker_retains_existing_certificate_and_schedules_retry(setup):
    key, store, service = setup
    service.handle(*signed(key, store, "certificate", csr=csr(store.row["hostname"])))
    store.row["certificate"] = "still valid"

    class Issuer:
        def issue(self, *args):
            raise TimeoutError("secret")

    Worker(store, Issuer(), DNS(), lambda: NOW).process(store.get(store.row["PartitionKey"]))
    assert store.row["state"] == "pending" and store.row["next_attempt"] > NOW
    assert store.row["certificate"] == "still valid" and store.row["error"] == "certificate_or_dns_unavailable"


def test_revocation_during_issuance_prevents_publication(setup):
    key, store, service = setup
    service.handle(*signed(key, store, "certificate", csr=csr(store.row["hostname"])))

    class Issuer:
        def issue(self, *args):
            store.row["enabled"] = False
            return {"certificate": "chain"}

    Worker(store, Issuer(), DNS(), lambda: NOW).process(store.get(store.row["PartitionKey"]))
    assert "certificate" not in store.row


def test_dns_only_changes_exact_device_record(setup):
    _, store, _ = setup
    adapter = Spaceship("tinyrobotcompany.io", SUFFIX, "test", "test")
    calls = []
    label = adapter.label(store.row["hostname"])
    records = [{"name": "@", "type": "A", "address": "203.0.113.1"},
               {"name": "www", "type": "CNAME", "cname": "example.com"},
               {"name": label, "type": "A", "address": "192.168.1.33"}]

    def call(method, **kwargs):
        calls.append((method, kwargs))
        if method == "PUT":
            records.extend(kwargs["json"]["items"])
        if method == "DELETE":
            records[:] = [r for r in records if not any(all(r.get(k) == v for k, v in old.items()) for old in kwargs["json"])]
        return {"items": list(records), "total": len(records)} if method == "GET" else None

    adapter.call = call
    adapter.set_address(store.row["hostname"], "192.168.1.34")
    assert calls[1][1]["json"]["force"] is False
    assert calls[2][1]["json"] == [{"name": label, "type": "A", "address": "192.168.1.33"}]
    with pytest.raises(ValueError, match="outside device namespace"):
        adapter.set_address("www.tinyrobotcompany.io", "192.168.1.34")


@pytest.mark.parametrize("stale", [False, True])
def test_dns_existing_address_does_not_add_duplicate(setup, stale):
    _, store, _ = setup
    adapter = Spaceship("tinyrobotcompany.io", SUFFIX, "test", "test")
    label = adapter.label(store.row["hostname"])
    desired = {"name": label, "type": "A", "address": "192.168.1.34"}
    old = {"name": label, "type": "A", "address": "192.168.1.33"}
    snapshots = iter([[desired, old] if stale else [desired], [desired]])
    adapter.records = lambda _: next(snapshots)
    calls = []
    adapter.call = lambda method, **kwargs: calls.append((method, kwargs))
    adapter.set_address(store.row["hostname"], desired["address"])
    assert [method for method, _ in calls] == (["DELETE"] if stale else [])


@pytest.mark.parametrize("addresses", [[], ["192.168.1.33"], ["192.168.1.34", "192.168.1.34"],
                                      ["192.168.1.34", "192.168.1.33"]])
def test_dns_does_not_report_success_before_convergence(setup, addresses):
    _, store, _ = setup
    adapter = Spaceship("tinyrobotcompany.io", SUFFIX, "test", "test")
    snapshots = iter([[], [{"type": "A", "address": address} for address in addresses]])
    adapter.records = lambda _: next(snapshots)
    adapter.call = lambda *args, **kwargs: None
    with pytest.raises(RuntimeError, match="dns_address_not_converged"):
        adapter.set_address(store.row["hostname"], "192.168.1.34")
