"""Azure Table transactions bind replay consumption to scheduling atomically."""
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
        return {k: v for k, v in row.items() if k not in ("etag", "public_key", "hostname", "enabled")}

    def accept(self, row, nonce, expires):
        mutation = (("update", self.entity(row), {"mode": UpdateMode.REPLACE, "etag": row["etag"],
                     "match_condition": MatchConditions.IfNotModified}) if row["etag"] else ("create", self.entity(row)))
        operations = [("create", {"PartitionKey": row["PartitionKey"], "RowKey": "nonce-" + nonce, "expires": expires}), mutation]
        try:
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

    def expire_nonces(self, now):
        query = "RowKey ge 'nonce-' and RowKey lt 'nonce.' and expires lt @now"
        for entity in self.table.query_entities(query, parameters={"now": now}, results_per_page=100):
            self.table.delete_entity(entity["PartitionKey"], entity["RowKey"])

    def register(self, row):
        try:
            self.registry.create_entity(row)
        except ResourceExistsError as error:
            raise Rejected("device_already_registered", 409) from error
