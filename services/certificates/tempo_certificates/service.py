"""Device admission and durable work scheduling; no DNS/CA credentials in the API."""
import time

from .protocol import Rejected, Reply, authenticate, decode, hostname, validate_address, validate_csr


class DeviceService:
    def __init__(self, store, suffix, clock=time.time):
        self.store, self.suffix, self.clock = store, suffix, clock

    def handle(self, body, signature):
        request = decode(body)
        now = int(self.clock())
        for _ in range(4):
            row = self.store.get(request["device_id"], "dns" if request["action"] == "address" else "device")
            if row is None or not row["enabled"]:
                raise Rejected("device_not_enrolled", 401)
            authenticate(request, body, signature, row["public_key"], now)
            name = hostname(request["device_id"], self.suffix)
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
            # Only one mutation can run per device. Status does not modify job data.
            busy = row.get("state") in ("pending", "running")
            if action != "status" and busy:
                reply = Reply({"state": row["state"], "retry_after": 30}, 202)
            elif action == "certificate" and now < row.get("renew_after", 0):
                # Serve the already issued certificate, even if a device retries after reboot.
                reply = self.status(row)
            elif action == "address" and request["address"] == row.get("address"):
                reply = self.status(row)
            elif action != "status":
                if now < row.get("retry_after", 0):
                    raise Rejected("retry_later", 429)
                row.update(state="pending", operation=action, attempts=0, next_attempt=now,
                           job_id=request["nonce"], csr=request.get("csr", ""),
                           desired_address=request.get("address", ""), error="")
                reply = Reply({"state": "pending", "retry_after": 30}, 202)
            else:
                reply = self.status(row)
            if action == "status":
                dns = self.store.get(request["device_id"], "dns")
                reply.body.update(address=(dns or {}).get("address", ""),
                                  dns_state=(dns or {}).get("state", "idle"))
            row["last_request"] = now
            row["request_count"] = row.get("request_count", 0) + 1
            if self.store.accept(row, request["nonce"], request["issued_at"] + 600):
                return reply
        raise Rejected("busy", 409)

    @staticmethod
    def status(row):
        return Reply({"hostname": row["hostname"], "state": row.get("state", "idle"),
                      "certificate": row.get("certificate", ""),
                      "renew_after": row.get("renew_after", 0),
                      "expires_at": row.get("expires_at", 0),
                      "address": row.get("address", ""),
                      "error": row.get("error", ""), "retry_after": 30})
