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
from .worker import Worker


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


def main():
    logging.basicConfig(level=logging.WARNING)
    logging.getLogger("tempo.certificates").setLevel(logging.INFO)
    credential = DefaultAzureCredential(managed_identity_client_id=os.environ.get("AZURE_CLIENT_ID"))
    vault = SecretClient(os.environ["KEY_VAULT_URL"], credential, logging_enable=False)
    while True:
        try:
            # Refresh secrets each pass so rotation does not require a firmware update.
            read = lambda name: vault.get_secret(name).value
            credentials = json.loads(read("spaceship-credentials"))
            dns = Spaceship(os.environ["DNS_DOMAIN"], os.environ["DEVICE_SUFFIX"],
                            credentials["key"], credentials["secret"])
            issuer = CloudIssuer(vault, os.environ["ACME_ENVIRONMENT"], dns)
            Worker(azure_store(), issuer, dns).run_once()
        except Exception:
            logging.getLogger("tempo.certificates").warning("worker_cycle_failed")
        time.sleep(30)


if __name__ == "__main__":
    main()
