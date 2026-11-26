"""Device admission and durable work scheduling; no DNS/CA credentials in the API."""
import time
import logging

LOG = logging.getLogger("tempo.certificates")

from .protocol import Rejected, Reply, authenticate, decode, hostname, validate_address, validate_csr


class DeviceService:
    def __init__(self, store, suffix, clock=time.time, waveform_suffix=""):
        self.suffixes = {"tempo": suffix}
        if waveform_suffix:
            if waveform_suffix == suffix:
                raise ValueError("Product DNS namespaces must differ")
            hostname("0" * 32, waveform_suffix)
            self.suffixes["waveform"] = waveform_suffix
        self.store, self.suffix, self.clock = store, suffix, clock

    def handle(self, body, signature):
        request = decode(body)
        now = int(self.clock())
        lane = "dns" if request["action"] == "address" else "device"
        for attempt in range(4):
            row = self.store.get(request["device_id"], lane)
            if row is None or not row["enabled"]:
                raise Rejected("device_not_enrolled", 401)
            product = request.get("product", "tempo")
            if product != row.get("product", "tempo"):
                raise Rejected("product_mismatch", 401)
            if product not in self.suffixes:
                raise Rejected("product_unavailable", 503)
            authenticate(request, body, signature, row["public_key"], now)
            if attempt == 0:
                # Independent of CA/DNS worker health. If cleanup storage is unavailable,
                # fail before admitting more rows instead of accumulating unbounded state.
                try:
                    self.store.expire_nonces(now, request["device_id"], limit=32)
                except Exception:
                    LOG.warning("nonce_cleanup_failed admission=denied")
                    raise
            name = hostname(request["device_id"], self.suffixes[product])
            if row["hostname"] != name:
                raise Rejected("device_configuration_mismatch", 503)
            if now - row.get("request_window", 0) >= 60:
                row.update(request_window=now, request_count=0)
            if row.get("request_count", 0) >= 30:
                raise Rejected("slow_down", 429)
            action = request["action"]
            if action == "certificate":
                validate_csr(request["csr"], name)
            if action == "address":
                validate_address(request["address"])
            # Only one mutation per lane. Status does not modify job data.
            busy = row.get("state") in ("pending", "running")
            if action != "status" and busy:
                reply = Reply({"state": row["state"], "retry_after": 30}, 202)
            elif action == "certificate" and now < row.get("renew_after", 0):
                # Serve the already issued certificate, even if a device retries after reboot.
                reply = self.status(row)
            elif action == "address" and request["address"] == row.get("address"):
                reply = self.status(self.store.get(request["device_id"], "device"), row)
            elif action != "status":
                if now < row.get("retry_after", 0):
                    raise Rejected("retry_later", 429)
                row.update(state="pending", operation=action, attempts=0, next_attempt=now,
                           job_id=request["nonce"], error="")
                row["csr" if action == "certificate" else "desired_address"] = request["csr" if action == "certificate" else "address"]
                reply = Reply({"state": "pending", "retry_after": 30}, 202)
            else:
                reply = self.status(row, self.store.get(request["device_id"], "dns"))
            row["last_request"] = now
            row["request_count"] = row.get("request_count", 0) + 1
            if self.store.accept(row, request["nonce"], request["issued_at"] + 600):
                return reply
        raise Rejected("busy", 409)

    @staticmethod
    def status(row, dns=None):
        return Reply({"hostname": row["hostname"], "state": row.get("state", "idle"),
                      "certificate": row.get("certificate", ""),
                      "renew_after": row.get("renew_after", 0),
                      "expires_at": row.get("expires_at", 0),
                      "address": (dns or {}).get("address", ""),
                      "dns_state": (dns or {}).get("state", "idle"),
                      "error": row.get("error", ""), "retry_after": 30})
