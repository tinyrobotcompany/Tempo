"""Worker entry point. Managed identity reads service secrets from Key Vault."""
import logging
import json
import os
import time
from concurrent.futures import ThreadPoolExecutor

from azure.identity import DefaultAzureCredential
from azure.keyvault.secrets import SecretClient
from cryptography import x509
from acme import messages

from .app import create_azure_store
from .dns import Spaceship
from .issuer import AcmeIssuer
from .worker import Worker, cleanup_nonces, report_expiring_certificates
from .secret_cache import SecretCache

LOG = logging.getLogger("tempo.certificates")
RATE_LIMITED = "urn:ietf:params:acme:error:rateLimited"
EXPIRY_CHECK_SECONDS = 3600


class CloudIssuer:
    """Load CA state only for certificate jobs; CA outages must not block DNS."""
    def __init__(self, vault, environment, dns):
        self.vault, self.environment, self.dns = vault, environment, dns

    def issue(self, name, csr, checkpoint):
        read = lambda key: self.vault.get_secret(key).value
        roots = (x509.load_pem_x509_certificates(read("acme-staging-roots").encode())
                 if self.environment == "staging" else None)
        try:
            issuer = AcmeIssuer(read("acme-account-key"), read("acme-account-uri"),
                                self.environment, self.dns, roots)
            return issuer.issue(name, csr, checkpoint)
        except messages.Error as error:
            if error.typ == RATE_LIMITED:
                LOG.warning("ca_rate_limited")
            if error.typ in ("urn:ietf:params:acme:error:unauthorized",
                              "urn:ietf:params:acme:error:accountDoesNotExist"):
                self.vault.invalidate("acme-account-key", "acme-account-uri")
            raise


def run_cycle(store, vault, environment, lane=None, check_expiry=False):
    # Housekeeping precedes external secret/provider access and job enumeration.
    now = int(time.time())
    cleanup_nonces(store, now)
    if check_expiry:
        report_expiring_certificates(store, now)
    credentials = json.loads(vault.get_secret("spaceship-credentials").value)
    dns = Spaceship(environment["DNS_DOMAIN"], environment["DEVICE_SUFFIX"],
                    credentials["key"], credentials["secret"],
                    authentication_failed=lambda: vault.invalidate("spaceship-credentials"))
    try:
        Worker(store, CloudIssuer(vault, environment["ACME_ENVIRONMENT"], dns), dns).run_jobs(lane)
    finally:
        dns.session.close()


def main():
    logging.basicConfig(level=logging.WARNING)
    logging.getLogger("tempo.certificates").setLevel(logging.INFO)
    # Separate loops and clients: a CA wait never delays DNS scheduling, and
    # requests.Session / Azure clients are not shared between these threads.
    def run_lane(lane):
        credential = DefaultAzureCredential(managed_identity_client_id=os.environ.get("AZURE_CLIENT_ID"))
        vault = SecretCache(SecretClient(os.environ["KEY_VAULT_URL"], credential, logging_enable=False))
        store = None
        next_expiry_check = 0.0
        while True:
            try:
                if store is None:
                    store = create_azure_store()
                # Hourly, in one lane only, so the expiry alert sees one event per hour.
                check = lane == "device" and time.monotonic() >= next_expiry_check
                if check:
                    next_expiry_check = time.monotonic() + EXPIRY_CHECK_SECONDS
                run_cycle(store, vault, os.environ, lane, check_expiry=check)
            except Exception:
                LOG.warning("worker_cycle_failed lane=%s", lane)
            time.sleep(30)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(run_lane, lane) for lane in ("dns", "device")]
        for job in jobs:
            job.result()


if __name__ == "__main__":
    main()
