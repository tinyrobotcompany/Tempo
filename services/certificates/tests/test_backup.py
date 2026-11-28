import copy
import json
from unittest.mock import Mock

import pytest
from azure.core.exceptions import ResourceExistsError, ResourceNotFoundError
from azure.data.tables import EdmType, EntityProperty
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from tempo_certificates import backup
from tempo_certificates.protocol import device_id, hostname

SUFFIX = "tempo.example.test"
NOW = 1_800_000_000


def identity(enabled=True, serial="S1"):
    pem = ec.generate_private_key(ec.SECP256R1()).public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    identifier = device_id(pem)
    return {"PartitionKey": identifier, "RowKey": "identity", "public_key": pem, "enabled": enabled,
            "hostname": hostname(identifier, SUFFIX), "serial": serial,
            "registered_at": EntityProperty(NOW, EdmType.INT64)}


class Registry:
    """DeviceRegistry double: create-only writes, like the restore job's contract."""
    def __init__(self, rows=()):
        self.rows = {(r["PartitionKey"], r["RowKey"]): copy.deepcopy(r) for r in rows}

    def list_entities(self):
        return iter(copy.deepcopy(list(self.rows.values())))

    def get_entity(self, partition, row):
        try:
            return copy.deepcopy(self.rows[(partition, row)])
        except KeyError as error:
            raise ResourceNotFoundError() from error

    def create_entity(self, row):
        key = row["PartitionKey"], row["RowKey"]
        if key in self.rows:
            raise ResourceExistsError()
        self.rows[key] = copy.deepcopy(row)


class Container:
    def __init__(self):
        self.blobs = {}

    def create(self, name, body):
        assert name not in self.blobs
        self.blobs[name] = body

    def read(self, name):
        return self.blobs[name]

    def names(self):
        return sorted(self.blobs)


def test_export_is_deterministic_typed_and_named_by_utc_time():
    rows = [identity(), identity(enabled=False)]
    container = Container()
    name, count = backup.export(Registry(rows), container, NOW)
    assert count == 2 and name == "device-registry/2027/01/15/20270115T080000Z.json"
    assert backup.NAME.fullmatch(name)
    document = json.loads(container.blobs[name])
    assert document["format"] == backup.FORMAT and document["exported_at"] == NOW
    assert [r["PartitionKey"] for r in document["entities"]] == sorted(r["PartitionKey"] for r in rows)
    assert document["entities"][0]["registered_at"] == {"edm": "Edm.Int64", "value": NOW}
    assert "private" not in container.blobs[name].decode().lower()


def test_round_trip_restores_only_missing_rows_and_never_overwrites():
    kept, lost, disabled_later = identity(), identity(), identity()
    container = Container()
    name, _ = backup.export(Registry([kept, lost, disabled_later]), container, NOW)
    live = Registry([kept, dict(disabled_later, enabled=False)])
    rows = backup.parse(container.read(name), SUFFIX)
    assert backup.restore(live, rows, apply=False) == {"missing": 1, "differing": 1, "identical": 1, "created": 0}
    assert len(live.rows) == 2
    assert backup.restore(live, rows, apply=True) == {"missing": 1, "differing": 1, "identical": 1, "created": 1}
    assert live.rows[(lost["PartitionKey"], "identity")] == lost
    # A device disabled after the backup stays disabled.
    assert live.rows[(disabled_later["PartitionKey"], "identity")]["enabled"] is False
    assert backup.restore(live, rows, apply=True)["created"] == 0


def test_concurrent_admission_during_restore_is_not_overwritten():
    row = identity()
    live = Registry()
    live.create_entity = Mock(side_effect=ResourceExistsError())
    assert backup.restore(live, [row], apply=True) == {"missing": 1, "differing": 0, "identical": 0, "created": 0}


@pytest.mark.parametrize("tamper", [
    lambda r: r.update(PartitionKey="0" * 32),
    lambda r: r.update(hostname="d-" + "0" * 32 + "." + SUFFIX),
    lambda r: r.update(RowKey="device"),
    lambda r: r.update(enabled="yes"),
])
def test_backup_that_does_not_derive_from_public_keys_is_rejected(tamper):
    row = backup.encode(identity())
    tamper(row)
    body = json.dumps({"format": backup.FORMAT, "exported_at": NOW, "entities": [row]})
    with pytest.raises(Exception):
        backup.parse(body, SUFFIX)
    with pytest.raises(ValueError, match="not_a_registry_backup"):
        backup.parse(json.dumps({"format": "other", "entities": []}), SUFFIX)


def test_export_job_logs_success_count_and_redacted_failure(caplog):
    container = Container()
    assert backup.main(["export"], lambda: (Registry([identity()]), container), lambda: NOW) == 0
    assert "registry_backup_completed entities=1" in caplog.text
    caplog.clear()
    broken = Container()
    broken.create = Mock(side_effect=RuntimeError("https://secret.example/sig=abc"))
    assert backup.main([], lambda: (Registry([identity()]), broken), lambda: NOW) == 1
    assert "registry_backup_failed" in caplog.text and "sig=abc" not in caplog.text


def test_restore_job_defaults_to_listing_then_dry_run(monkeypatch, capsys, caplog):
    row = identity()
    container = Container()
    name, _ = backup.export(Registry([row]), container, NOW)
    live = Registry()
    connect = lambda: (live, container)
    monkeypatch.setenv("DEVICE_SUFFIX", SUFFIX)
    monkeypatch.delenv("RESTORE_BLOB", raising=False)
    assert backup.main(["restore"], connect) == 0 and name in capsys.readouterr().out
    monkeypatch.setenv("RESTORE_BLOB", "../other/blob.json")
    assert backup.main(["restore"], connect) == 2
    monkeypatch.setenv("RESTORE_BLOB", name)
    assert backup.main(["restore"], connect) == 0 and not live.rows
    assert "registry_restore_checked missing=1" in caplog.text
    monkeypatch.setenv("RESTORE_APPLY", "yes")
    assert backup.main(["restore"], connect) == 0 and len(live.rows) == 1
    assert backup.main(["delete"], connect) == 2


def test_blob_client_uses_entra_token_create_only_and_checks_status():
    session, credential = Mock(), Mock()
    credential.get_token.return_value.token = "token"
    session.put.return_value.status_code = 201
    blobs = backup.BlobContainer("https://stbk.blob.core.windows.net/", "device-registry", credential, session)
    blobs.create("device-registry/x.json", b"{}")
    url = session.put.call_args.args[0]
    headers = session.put.call_args.kwargs["headers"]
    assert url == "https://stbk.blob.core.windows.net/device-registry/device-registry/x.json"
    assert headers["If-None-Match"] == "*" and headers["Authorization"] == "Bearer token"
    credential.get_token.assert_called_with("https://storage.azure.com/.default")
    session.put.return_value.status_code = 409
    with pytest.raises(RuntimeError, match="status=409"):
        blobs.create("device-registry/x.json", b"{}")
    session.get.return_value.status_code = 200
    session.get.return_value.content = (b"<EnumerationResults><Blobs><Blob><Name>device-registry/b</Name></Blob>"
                                        b"<Blob><Name>device-registry/a</Name></Blob></Blobs></EnumerationResults>")
    assert blobs.names() == ["device-registry/a", "device-registry/b"]
    for endpoint in ("http://stbk.blob.core.windows.net", "https://stbk.blob.core.windows.net.evil.test"):
        with pytest.raises(ValueError):
            backup.BlobContainer(endpoint, "device-registry", credential, session)
