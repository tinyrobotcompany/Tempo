"""ACME DNS-01 issuance using a service account key from Azure Key Vault."""
import datetime
import time

import certifi
import dns.resolver
import josepy
from acme import client, messages, challenges
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.x509.verification import PolicyBuilder, Store

from .protocol import validate_csr

DIRECTORIES = {
    "staging": "https://acme-staging-v02.api.letsencrypt.org/directory",
    "production": "https://acme-v02.api.letsencrypt.org/directory",
}


def verify_chain(pem, csr_pem, name, now, roots=None):
    if not 0 < len(pem.encode()) < 8192:
        raise ValueError("certificate_size")
    chain = x509.load_pem_x509_certificates(pem.encode())
    csr = validate_csr(csr_pem, name)
    encode = lambda key: key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    if encode(chain[0].public_key()) != encode(csr.public_key()):
        raise ValueError("certificate_key_mismatch")
    if list(chain[0].extensions.get_extension_for_class(x509.SubjectAlternativeName).value) != [x509.DNSName(name)]:
        raise ValueError("certificate_name_mismatch")
    if roots is None:
        with open(certifi.where(), "rb") as bundle:
            roots = x509.load_pem_x509_certificates(bundle.read())
    verifier = PolicyBuilder().store(Store(roots)).time(datetime.datetime.fromtimestamp(now, datetime.timezone.utc)).build_server_verifier(x509.DNSName(name))
    verifier.verify(chain[0], chain[1:])
    start = int(chain[0].not_valid_before_utc.timestamp())
    end = int(chain[0].not_valid_after_utc.timestamp())
    if end - now < 86400:
        raise ValueError("certificate_lifetime")
    # Lifetime-relative renewal also supports shorter-lived CA profiles.
    return {"certificate": pem, "expires_at": end, "renew_after": max(now + 3600, start + (end - start) // 2)}


class AcmeIssuer:
    def __init__(self, account_key_pem, account_uri, environment, dns_provider, roots=None):
        self.key = josepy.JWKRSA.load(account_key_pem.encode())
        directory_url = DIRECTORIES[environment]
        expected_account_prefix = directory_url.removesuffix("directory") + "acct/"
        if not account_uri.startswith(expected_account_prefix):
            raise ValueError("ACME account does not match configured environment")
        self.net = client.ClientNetwork(self.key, account=messages.RegistrationResource(
            uri=account_uri, body=messages.Registration()), timeout=15)
        self.client = client.ClientV2(messages.Directory.from_json(self.net.get(directory_url).json()), self.net)
        self.dns, self.roots = dns_provider, roots

    def issue(self, name, csr_pem, checkpoint):
        validate_csr(csr_pem, name)
        order = self.client.new_order(csr_pem.encode())
        if len(order.authorizations) != 1 or order.authorizations[0].body.identifier.value != name:
            raise ValueError("unexpected_acme_identifier")
        authorization = order.authorizations[0]
        challenge = next(c for c in authorization.body.challenges if isinstance(c.chall, challenges.DNS01))
        response, value = challenge.response_and_validation(self.key)
        # Persist before the external mutation: a restart knows exactly what to clean.
        checkpoint({"challenge": value})
        self.dns.put_challenge(name, value)
        deadline = time.monotonic() + 150
        resolver = dns.resolver.Resolver()
        while time.monotonic() < deadline:
            try:
                answers = resolver.resolve("_acme-challenge." + name, "TXT", lifetime=5)
                if value in {b"".join(r.strings).decode() for r in answers}:
                    break
            except dns.exception.DNSException:
                pass
            time.sleep(5)
        else:
            raise TimeoutError("dns_propagation_timeout")
        self.client.answer_challenge(challenge, response)
        order = self.client.poll_and_finalize(order, deadline=datetime.datetime.now() + datetime.timedelta(seconds=90))
        result = verify_chain(order.fullchain_pem, csr_pem, name, time.time(), self.roots)
        return result
