"""Worker entry point. Managed identity reads service secrets from Key Vault."""
import logging
import json
import os
import time

from azure.identity import DefaultAzureCredential
from azure.keyvault.secrets import SecretClient
from cryptography import x509

from .app import azure_store
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


def run_cycle(store, vault, environment):
    # Housekeeping precedes external secret/provider access and job enumeration.
    cleanup_nonces(store, int(time.time()))
    credentials = json.loads(vault.get_secret("spaceship-credentials").value)
    dns = Spaceship(environment["DNS_DOMAIN"], environment["DEVICE_SUFFIX"],
                    credentials["key"], credentials["secret"])
    Worker(store, CloudIssuer(vault, environment["ACME_ENVIRONMENT"], dns), dns).run_jobs()


def main():
    logging.basicConfig(level=logging.WARNING)
    logging.getLogger("tempo.certificates").setLevel(logging.INFO)
    credential = DefaultAzureCredential(managed_identity_client_id=os.environ.get("AZURE_CLIENT_ID"))
    vault = SecretClient(os.environ["KEY_VAULT_URL"], credential, logging_enable=False)
    while True:
        try:
            store = azure_store()
            run_cycle(store, vault, os.environ)
        except Exception:
            logging.getLogger("tempo.certificates").warning("worker_cycle_failed")
        time.sleep(30)


if __name__ == "__main__":
    main()
