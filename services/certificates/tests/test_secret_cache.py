from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from acme import messages

from tempo_certificates.dns import Spaceship
from tempo_certificates.runtime import CloudIssuer, run_cycle
from tempo_certificates.secret_cache import SecretCache


def test_cache_expires_and_does_not_serve_stale_secrets_on_vault_failure():
    now = [0]
    client = Mock()
    old, new = SimpleNamespace(value="old"), SimpleNamespace(value="new")
    client.get_secret.side_effect = [old, RuntimeError("unavailable"), new]
    cache = SecretCache(client, clock=lambda: now[0])
    assert cache.get_secret("spaceship-credentials") is old
    now[0] = 299
    assert cache.get_secret("spaceship-credentials") is old
    assert client.get_secret.call_count == 1
    now[0] = 300
    with pytest.raises(RuntimeError):
        cache.get_secret("spaceship-credentials")
    assert not cache.entries
    assert cache.get_secret("spaceship-credentials") is new
    with pytest.raises(ValueError):
        cache.get_secret("arbitrary-key")
    assert client.get_secret.call_count == 3


@pytest.mark.parametrize("status", [401, 403, 429, 500])
def test_dns_authentication_failure_invalidates_without_replaying_mutation(status):
    session, invalidated = Mock(), Mock()
    session.request.return_value.status_code = status
    dns = Spaceship("example.com", "tempo.example.com", "key", "secret",
                    session=session, authentication_failed=invalidated)
    with pytest.raises(RuntimeError, match="dns_provider_error"):
        dns.call("PUT", json={})
    session.request.assert_called_once()
    assert invalidated.call_count == (1 if status in (401, 403) else 0)


def test_worker_cycles_reuse_dns_secret_and_refresh_after_auth_failure(monkeypatch):
    client = Mock()
    client.get_secret.return_value = SimpleNamespace(value='{"key":"k","secret":"s"}')
    cache = SecretCache(client)
    worker = Mock()
    monkeypatch.setattr("tempo_certificates.runtime.Worker", worker)
    environment = {"DNS_DOMAIN": "example.com", "DEVICE_SUFFIX": "tempo.example.com",
                   "ACME_ENVIRONMENT": "production"}
    for _ in range(10):
        run_cycle(Mock(), cache, environment)
    client.get_secret.assert_called_once_with("spaceship-credentials")
    dns = worker.call_args.args[2]
    dns.authentication_failed()
    run_cycle(Mock(), cache, environment)
    assert client.get_secret.call_count == 2


@pytest.mark.parametrize("code", ["unauthorized", "accountDoesNotExist", "rateLimited"])
def test_acme_auth_failure_refreshes_account_without_duplicate_order(monkeypatch, code):
    vault, issuer = Mock(), Mock()
    issuer.issue.side_effect = messages.Error.with_code(code)
    constructor = Mock(return_value=issuer)
    monkeypatch.setattr("tempo_certificates.runtime.AcmeIssuer", constructor)
    with pytest.raises(messages.Error):
        CloudIssuer(vault, "production", Mock()).issue("name", "csr", Mock())
    issuer.issue.assert_called_once()
    if code == "rateLimited":
        vault.invalidate.assert_not_called()
    else:
        vault.invalidate.assert_called_once_with("acme-account-key", "acme-account-uri")
