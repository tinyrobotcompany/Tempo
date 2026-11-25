"""Worker entry point. Managed identity reads service secrets from Key Vault."""
import logging
import json
import os
import time
from concurrent.futures import ThreadPoolExecutor

from azure.identity import DefaultAzureCredential
from azure.keyvault.secrets import SecretClient
from cryptography import x509

from .app import create_azure_store
from .dns import Spaceship
from .issuer import AcmeIssuer
from .worker import Worker, cleanup_nonces


class CloudIssuer:
    """Load CA state only for certificate jobs; CA outages must not block DNS."""
    def __init__(self, vault, environment, dns):
        self.vault, self.environment, self.dns = vault, environment, dns

    def issue(self, name, csr, checkpoint):
        read = lambda key: self.vault.get_secret(key).value
        roots = (x509.load_pem_x509_certificates(read("acme-staging-roots").encode())
                 if self.environment == "staging" else None)
        issuer = AcmeIssuer(read("acme-account-key"), read("acme-account-uri"),
                            self.environment, self.dns, roots)
        return issuer.issue(name, csr, checkpoint)


def run_cycle(store, vault, environment, lane=None):
    # Housekeeping precedes external secret/provider access and job enumeration.
    cleanup_nonces(store, int(time.time()))
    credentials = json.loads(vault.get_secret("spaceship-credentials").value)
    dns = Spaceship(environment["DNS_DOMAIN"], environment["DEVICE_SUFFIX"],
                    credentials["key"], credentials["secret"],
                    waveform_suffix=environment.get("WAVEFORM_DEVICE_SUFFIX", ""))
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
        vault = SecretClient(os.environ["KEY_VAULT_URL"], credential, logging_enable=False)
        store = None
        while True:
            try:
                if store is None:
                    store = create_azure_store()
                run_cycle(store, vault, os.environ, lane)
            except Exception:
                logging.getLogger("tempo.certificates").warning("worker_cycle_failed lane=%s", lane)
            time.sleep(30)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(run_lane, lane) for lane in ("dns", "device")]
        for job in jobs:
            job.result()


if __name__ == "__main__":
    main()
