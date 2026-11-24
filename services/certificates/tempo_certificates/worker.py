"""Restart-safe certificate/DNS jobs. No device key material is accepted or stored."""
import logging
import secrets
import time

from .protocol import Rejected, validate_address, validate_csr

LOG = logging.getLogger("tempo.certificates")
LEASE_SECONDS = 900


class LostLease(Exception):
    pass


def cleanup_nonces(store, now):
    try:
        store.expire_nonces(now)
    except Exception:
        LOG.warning("nonce_cleanup_failed admission=worker")


class Worker:
    def __init__(self, store, issuer, dns, clock=time.time):
        self.store, self.issuer, self.dns, self.clock = store, issuer, dns, clock

    def process(self, candidate):
        now = int(self.clock())
        token = secrets.token_hex(16)
        candidate.update(state="running", lease=token, next_attempt=now + LEASE_SECONDS,
                         attempts=candidate.get("attempts", 0) + 1)
        if not self.store.replace(candidate):
            return
        identifier = candidate["PartitionKey"]

        def checkpoint(fields):
            # API status polling can update ETags. Retry without clobbering those writes.
            for _ in range(8):
                row = self.store.get(identifier, candidate["RowKey"])
                if not row or not row["enabled"] or row.get("lease") != token or row["next_attempt"] <= self.clock():
                    raise LostLease()
                row.update(fields)
                if self.store.replace(row):
                    return row
            raise LostLease()

        try:
            row = checkpoint({})
            if row.get("challenge"):
                self.dns.remove_challenge(row["hostname"], row["challenge"])
                checkpoint({"challenge": ""})
            # Validate durable input before charging issuance quota or starting
            # provider work (after orphan challenge cleanup). Provider exceptions must remain retryable and redacted.
            try:
                if row.get("operation") == "certificate":
                    if not row.get("job_id"):
                        raise Rejected("invalid_job")
                    if row.get("completed_job") != row["job_id"]:
                        validate_csr(row.get("csr"), row["hostname"])
                elif row.get("operation") == "address":
                    validate_address(row.get("desired_address"))
                else:
                    raise Rejected("invalid_job")
            except Rejected:
                checkpoint({"state": "error", "error": "invalid_job", "lease": "",
                            "retry_after": now + 86400})
                LOG.warning("device_job_invalid")
                return
            if row["attempts"] > 4:
                checkpoint({"state": "error", "error": "retry_limit_reached", "retry_after": now + 86400})
                return
            if row["operation"] == "certificate":
                # At most four attempts per job. Persist success before cleanup so a
                # provider outage cannot cause unnecessary duplicate certificate orders.
                if row.get("completed_job") == row["job_id"]:
                    result = {}
                else:
                    window = row.get("issue_window", 0)
                    count = row.get("issue_count", 0) if now - window < 86400 else 0
                    if count >= 4:
                        checkpoint({"state": "error", "error": "issuance_quota", "retry_after": window + 86400})
                        return
                    checkpoint({"issue_window": window if count else now, "issue_count": count + 1})
                    result = self.issuer.issue(row["hostname"], row["csr"], checkpoint)
                    result["completed_job"] = row["job_id"]
                    row = checkpoint(result)
                row = checkpoint({})
                if row.get("challenge"):
                    self.dns.remove_challenge(row["hostname"], row["challenge"])
            elif row["operation"] == "address":
                self.dns.set_address(row["hostname"], row["desired_address"])
                result = {"address": row["desired_address"]}
            else:
                raise ValueError("unknown_job")
            if row["operation"] == "certificate":
                result.update(challenge="", csr="")
            checkpoint(dict(result, state="ready", error="", lease="", retry_after=now + 60))
            LOG.info("device_job_succeeded operation=%s", row["operation"])
        except LostLease:
            LOG.warning("device_job_lease_lost")
        except Exception:
            # Never include exceptions from HTTP libraries: URLs, headers and CSR
            # content must not leak into application telemetry.
            LOG.warning("device_job_failed")
            try:
                row = checkpoint({})
                attempts = row["attempts"]
                delay = min(21600, 300 * 2 ** min(attempts, 6))
                checkpoint({"attempts": attempts, "state": "pending" if attempts < 4 else "error",
                            "next_attempt": int(self.clock()) + delay, "retry_after": int(self.clock()) + delay,
                            "error": "certificate_or_dns_unavailable", "lease": ""})
            except LostLease:
                pass

    def run_once(self):
        cleanup_nonces(self.store, int(self.clock()))
        self.run_jobs()

    def run_jobs(self):
        for candidate in self.store.candidates(int(self.clock())):
            self.process(candidate)
