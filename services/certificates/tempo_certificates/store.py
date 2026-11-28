"""Azure Table transactions bind replay consumption to scheduling atomically."""
from itertools import islice
import logging

LOG = logging.getLogger("tempo.certificates")

from azure.core import MatchConditions
from azure.core.exceptions import ResourceNotFoundError, ResourceExistsError, ResourceModifiedError
from azure.data.tables import UpdateMode
from azure.data.tables import TableTransactionError

from .protocol import Rejected


class TableStore:
    def __init__(self, table, registry):
        self.table, self.registry = table, registry

    def get(self, identifier, lane="device"):
        try:
            registration = self.registry.get_entity(identifier, "identity")
        except ResourceNotFoundError:
            return None
        try:
            entity = self.table.get_entity(identifier, lane)
            row = dict(entity, etag=entity.metadata["etag"])
        except ResourceNotFoundError:
            row = {"PartitionKey": identifier, "RowKey": lane, "etag": None, "state": "idle"}
        row.update({key: registration[key] for key in ("public_key", "hostname", "enabled")})
        return row

    @staticmethod
    def entity(row):
        return {k: v for k, v in row.items() if k not in ("etag", "public_key", "hostname", "enabled", "product")}

    def accept(self, row, nonce, expires):
        mutation = (("update", self.entity(row), {"mode": UpdateMode.REPLACE, "etag": row["etag"],
                     "match_condition": MatchConditions.IfNotModified}) if row["etag"] else ("create", self.entity(row)))
        operations = [("create", {"PartitionKey": row["PartitionKey"], "RowKey": "nonce-" + nonce, "expires": expires}), mutation]
        try:
            # One entity-group change set, one table and PartitionKey: Azure commits
            # BOTH writes or NEITHER. A failed ETag cannot consume the nonce.
            # https://learn.microsoft.com/rest/api/storageservices/performing-entity-group-transactions
            self.table.submit_transaction(operations)
            return True
        except TableTransactionError as error:
            if error.status_code == 409 and error.index == 0:
                raise Rejected("replayed_request", 409) from error
            if error.status_code in (409, 412):
                return False
            raise

    def replace(self, row):
        try:
            self.table.update_entity(self.entity(row), mode=UpdateMode.REPLACE, etag=row["etag"],
                                     match_condition=MatchConditions.IfNotModified)
            return True
        except (ResourceModifiedError, ResourceNotFoundError):
            return False

    def candidates(self, now):
        # Due work is durable; crashed workers become eligible after their lease expires.
        query = "(RowKey eq 'device' or RowKey eq 'dns') and next_attempt le @now and (state eq 'pending' or state eq 'running')"
        for entity in self.table.query_entities(query, parameters={"now": now}, results_per_page=100):
            row = self.get(entity["PartitionKey"], entity["RowKey"])
            if row and row["enabled"] and row.get("next_attempt", now + 1) <= now and row["state"] in ("pending", "running"):
                yield row

    def expire_nonces(self, now, identifier=None, limit=1000):
        query = "RowKey ge 'nonce-' and RowKey lt 'nonce.' and expires lt @now"
        parameters = {"now": now}
        if identifier is not None:
            query = "PartitionKey eq @identifier and " + query
            parameters["identifier"] = identifier
        rows = self.table.query_entities(query, parameters=parameters, results_per_page=min(limit, 100))
        deleted = 0
        for entity in islice(rows, limit):
            try:
                self.table.delete_entity(entity["PartitionKey"], entity["RowKey"])
            except ResourceNotFoundError:
                pass  # Another API replica or the worker already removed it.
            deleted += 1
        if deleted == limit:
            LOG.warning("nonce_cleanup_budget_reached limit=%d", limit)
        return deleted

    def expiring_certificates(self, now, horizon):
        # Enabled devices whose current certificate lapses within (now, horizon].
        query = "RowKey eq 'device' and expires_at gt @now and expires_at le @horizon"
        count = 0
        for entity in self.table.query_entities(query, parameters={"now": now, "horizon": horizon},
                                                results_per_page=100):
            row = self.get(entity["PartitionKey"])
            if row and row["enabled"]:
                count += 1
        return count

    def register(self, row):
        try:
            self.registry.create_entity(row)
        except ResourceExistsError as error:
            raise Rejected("device_already_registered", 409) from error
