"""Daily DeviceRegistry export to private, immutable blob storage, and its restore.

Registry rows hold only public identity keys, hostnames, serials and flags. The
export job can read the registry and add blobs; the separate manual restore job
can read blobs and add missing registry rows. Neither overwrites anything.
"""
import datetime
import email.utils
import json
import logging
import os
import re
import sys
import xml.etree.ElementTree as ElementTree

import requests
from azure.core.exceptions import ResourceExistsError, ResourceNotFoundError
from azure.data.tables import EdmType, EntityProperty

from .protocol import device_id, hostname

LOG = logging.getLogger("tempo.certificates")
FORMAT = "tempo-device-registry-v1"
PREFIX = "device-registry/"
NAME = re.compile(r"^device-registry/\d{4}/\d{2}/\d{2}/\d{8}T\d{6}Z\.json$")
STORAGE_SCOPE = "https://storage.azure.com/.default"


class BlobContainer:
    """Minimal Entra-authenticated Blob REST client; no shared keys or SAS."""
    def __init__(self, endpoint, container, credential, session=None):
        endpoint = endpoint.rstrip("/")
        if not endpoint.startswith("https://") or not endpoint.endswith(".blob.core.windows.net"):
            raise ValueError("An Azure HTTPS Blob endpoint is required")
        if not re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{1,61}[a-z0-9])", container):
            raise ValueError("Invalid container name")
        self.base, self.credential = f"{endpoint}/{container}", credential
        self.session = session or requests.Session()

    def _headers(self, **extra):
        token = self.credential.get_token(STORAGE_SCOPE).token
        return {"Authorization": f"Bearer {token}", "x-ms-version": "2023-11-03",
                "x-ms-date": email.utils.formatdate(usegmt=True), **extra}

    def create(self, name, body):
        # If-None-Match: * never replaces an existing backup (immutability also forbids it).
        reply = self.session.put(f"{self.base}/{name}", data=body, timeout=30, headers=self._headers(**{
            "x-ms-blob-type": "BlockBlob", "If-None-Match": "*", "Content-Type": "application/json"}))
        if reply.status_code != 201:
            raise RuntimeError(f"blob_write_failed status={reply.status_code}")

    def read(self, name):
        reply = self.session.get(f"{self.base}/{name}", timeout=30, headers=self._headers())
        if reply.status_code != 200:
            raise RuntimeError(f"blob_read_failed status={reply.status_code}")
        return reply.content

    def names(self):
        reply = self.session.get(self.base, timeout=30, headers=self._headers(), params={
            "restype": "container", "comp": "list", "prefix": PREFIX, "maxresults": "5000"})
        if reply.status_code != 200:
            raise RuntimeError(f"blob_list_failed status={reply.status_code}")
        return sorted(node.text for node in ElementTree.fromstring(reply.content).iter("Name"))


def encode(entity):
    row = {}
    for key, value in entity.items():
        # EntityProperty is a tuple; keep its EDM type (e.g. Int64) for an exact restore.
        row[key] = {"edm": value.edm_type.value, "value": value.value} if isinstance(value, EntityProperty) else value
    return row


def decode(row):
    return {key: EntityProperty(value["value"], EdmType(value["edm"])) if isinstance(value, dict) else value
            for key, value in row.items()}


def export(registry, container, now):
    entities = sorted((encode(entity) for entity in registry.list_entities()),
                      key=lambda row: (row["PartitionKey"], row["RowKey"]))
    stamp = datetime.datetime.fromtimestamp(now, datetime.timezone.utc)
    name = stamp.strftime(PREFIX + "%Y/%m/%d/%Y%m%dT%H%M%SZ.json")
    body = json.dumps({"format": FORMAT, "exported_at": int(now), "entities": entities},
                      sort_keys=True, separators=(",", ":")).encode()
    container.create(name, body)
    return name, len(entities)


def parse(body, suffix):
    document = json.loads(body)
    if document.get("format") != FORMAT or not isinstance(document.get("entities"), list):
        raise ValueError("not_a_registry_backup")
    rows = [decode(row) for row in document["entities"]]
    for row in rows:
        # Trust comes only from the public key: an ID or name that does not derive
        # from it rejects the whole backup.
        identifier = device_id(row["public_key"])
        if (row.get("RowKey") != "identity" or row.get("PartitionKey") != identifier
                or row.get("hostname") != hostname(identifier, suffix) or not isinstance(row.get("enabled"), bool)):
            raise ValueError("inconsistent_registry_backup")
    return rows


def plain(value):
    return value.value if isinstance(value, EntityProperty) else value


def restore(registry, rows, apply):
    """Compare a backup with the registry; with apply, create only missing rows."""
    result = {"missing": 0, "differing": 0, "identical": 0, "created": 0}
    for row in rows:
        try:
            current = registry.get_entity(row["PartitionKey"], row["RowKey"])
        except ResourceNotFoundError:
            result["missing"] += 1
            if apply:
                try:
                    registry.create_entity(row)
                    result["created"] += 1
                except ResourceExistsError:
                    pass  # Admitted concurrently; never overwrite a live row.
            continue
        # Existing rows (including later disables) always win over the backup.
        same = all(plain(current.get(key)) == plain(value) for key, value in row.items())
        result["identical" if same else "differing"] += 1
    return result


def clients(credential):
    from azure.data.tables import TableServiceClient
    endpoint = os.environ["TABLE_ENDPOINT"]
    if not endpoint.startswith("https://") or not endpoint.endswith(".table.core.windows.net"):
        raise ValueError("An Azure HTTPS Table endpoint is required")
    tables = TableServiceClient(endpoint, credential=credential, logging_enable=False,
                                connection_timeout=5, read_timeout=30, retry_total=2)
    container = BlobContainer(os.environ["BACKUP_BLOB_ENDPOINT"], os.environ["BACKUP_CONTAINER"], credential)
    return tables.get_table_client("DeviceRegistry"), container


def main(argv=None, connect=None, clock=None):
    import time
    logging.basicConfig(level=logging.WARNING)
    LOG.setLevel(logging.INFO)
    mode = (argv if argv is not None else sys.argv[1:])[:1] or ["export"]
    if connect is None:
        from azure.identity import DefaultAzureCredential
        connect = lambda: clients(DefaultAzureCredential(managed_identity_client_id=os.environ.get("AZURE_CLIENT_ID")))
    if mode == ["export"]:
        try:
            registry, container = connect()
            name, count = export(registry, container, (clock or time.time)())
        except Exception:
            LOG.warning("registry_backup_failed")
            return 1
        LOG.info("registry_backup_completed entities=%d", count)
        print(f"Exported {count} registry rows to {name}.")
        return 0
    if mode != ["restore"]:
        print("usage: python -m tempo_certificates.backup [export|restore]", file=sys.stderr)
        return 2
    registry, container = connect()
    name = os.environ.get("RESTORE_BLOB", "")
    if not name:
        print("Set RESTORE_BLOB to one of the newest backups:\n" + "\n".join(container.names()[-10:]))
        return 0
    if not NAME.fullmatch(name):
        print("RESTORE_BLOB is not a registry backup name.", file=sys.stderr)
        return 2
    apply = os.environ.get("RESTORE_APPLY") == "yes"
    result = restore(registry, parse(container.read(name), os.environ["DEVICE_SUFFIX"]), apply)
    LOG.warning("registry_restore_%s missing=%d differing=%d identical=%d created=%d",
                "applied" if apply else "checked", result["missing"], result["differing"],
                result["identical"], result["created"])
    print(json.dumps(dict(result, backup=name, applied=apply)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
