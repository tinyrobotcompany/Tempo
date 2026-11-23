"""One-time ACME account creation INSIDE Azure; never generates device keys.

Run as the restricted bootstrap Container Apps job after an operator accepts the
exact agreement URL. Account material is stored directly in Key Vault, not files.
"""
import os
import json

from acme import client, messages
from azure.core.exceptions import ResourceNotFoundError
from azure.identity import DefaultAzureCredential
from azure.keyvault.secrets import SecretClient
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
import josepy

from .issuer import DIRECTORIES


def main():
    credential = DefaultAzureCredential(managed_identity_client_id=os.environ["AZURE_CLIENT_ID"])
    vault = SecretClient(os.environ["KEY_VAULT_URL"], credential, logging_enable=False)
    if os.environ.get("SPACESHIP_CREDENTIALS"):
        credentials = json.loads(os.environ["SPACESHIP_CREDENTIALS"])
        if set(credentials) != {"key", "secret"} or not all(isinstance(v, str) and v for v in credentials.values()):
            raise ValueError("Invalid DNS credential object")
        vault.set_secret("spaceship-credentials", json.dumps(credentials))
    try:
        pem = vault.get_secret("acme-account-key").value
    except ResourceNotFoundError:
        # A single authorized bootstrap job is allowed; provisioning is not exposed
        # on the device API. Persist before ACME registration to allow safe retries.
        key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption()).decode()
        vault.set_secret("acme-account-key", pem)
    key = josepy.JWKRSA.load(pem.encode())
    net = client.ClientNetwork(key, timeout=15)
    directory = messages.Directory.from_json(net.get(DIRECTORIES[os.environ["ACME_ENVIRONMENT"]]).json())
    if directory.meta.terms_of_service != os.environ.get("ACCEPTED_ACME_TERMS_URL"):
        raise RuntimeError("Operator must accept the current ACME subscriber agreement before registration")
    account = client.ClientV2(directory, net).new_account(messages.NewRegistration.from_data(
        email=os.environ["ACME_CONTACT_EMAIL"], terms_of_service_agreed=True))
    vault.set_secret("acme-account-uri", account.uri)
    print("ACME service account stored in Azure Key Vault. No device key was generated.")


if __name__ == "__main__":
    main()
