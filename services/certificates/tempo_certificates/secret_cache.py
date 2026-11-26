"""Small, process-local secret cache. Expired values are never served on errors."""
import time


class SecretCache:
    NAMES = frozenset({"spaceship-credentials", "acme-account-key", "acme-account-uri",
                       "acme-staging-roots"})

    def __init__(self, client, clock=time.monotonic, lifetime=300):
        if not 0 < lifetime <= 300:
            raise ValueError("Secret cache lifetime must be at most five minutes")
        self.client, self.clock, self.lifetime = client, clock, lifetime
        self.entries = {}

    def get_secret(self, name):
        if name not in self.NAMES:
            raise ValueError("Unexpected worker secret")
        entry = self.entries.get(name)
        if entry is not None and self.clock() < entry[0]:
            return entry[1]
        self.invalidate(name)
        secret = self.client.get_secret(name)
        self.entries[name] = (self.clock() + self.lifetime, secret)
        return secret

    def invalidate(self, *names):
        for name in names:
            self.entries.pop(name, None)
