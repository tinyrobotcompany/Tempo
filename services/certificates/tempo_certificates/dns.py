"""Spaceship adapter. Mutations are restricted to one registered device name."""
import re
import ssl

import requests
from requests.adapters import HTTPAdapter

from .protocol import validate_address


class TLSAdapter(HTTPAdapter):
    def init_poolmanager(self, *args, **kwargs):
        context = ssl.create_default_context()
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        kwargs["ssl_context"] = context
        return super().init_poolmanager(*args, **kwargs)


class Spaceship:
    def __init__(self, domain, suffix, api_key, api_secret, session=None, waveform_suffix=""):
        self.suffixes = (suffix, waveform_suffix) if waveform_suffix else (suffix,)
        if any(not value.endswith("." + domain) for value in self.suffixes):
            raise ValueError("Device suffix must be below DNS domain")
        if not suffix.endswith("." + domain):
            raise ValueError("Device suffix must be below DNS domain")
        self.domain, self.suffix = domain, suffix
        self.session = session or requests.Session()
        self.session.trust_env = False
        self.session.mount("https://", TLSAdapter())
        self.session.headers.update({"X-API-Key": api_key, "X-API-Secret": api_secret})
        self.url = "https://spaceship.dev/api/v1/dns/records/" + domain

    def label(self, name):
        if not any(re.fullmatch(r"d-[0-9a-f]{32}\." + re.escape(suffix), name) for suffix in self.suffixes):
            raise ValueError("DNS mutation outside device namespace")
        return name[:-(len(self.domain) + 1)]

    def call(self, method, **kwargs):
        response = self.session.request(method, self.url, timeout=(5, 15), allow_redirects=False, **kwargs)
        if response.status_code not in (200, 204):
            # Do not log provider response bodies/headers, which may contain account data.
            raise RuntimeError("dns_provider_error")
        return response.json() if response.status_code == 200 else None

    def records(self, label):
        # Spaceship supports name ordering, but no exact-name query. Binary search
        # sorted pages rather than downloading every unrelated fleet record.
        cache = {}
        total = None
        def page(index):
            nonlocal total
            if index not in cache:
                reply = self.call("GET", params={"take": 500, "skip": index * 500, "orderBy": "name"})
                count, items = reply["total"], reply["items"]
                if type(count) is not int or count < 0 or count > 2147483647:
                    raise RuntimeError("dns_pagination_error")
                if total is not None and count != total:
                    raise RuntimeError("dns_zone_changed_retry")
                total = count
                names = [r["name"].lower() for r in items]
                if names != sorted(names) or len(items) != min(500, max(0, count-index*500)):
                    raise RuntimeError("dns_pagination_error")
                cache[index] = items
            return cache[index]
        page(0)
        pages = (total + 499) // 500
        low, high = 0, pages
        label = label.lower()
        while low < high:
            middle = (low + high) // 2
            items = page(middle)
            if items[-1]["name"].lower() < label:
                low = middle + 1
            else:
                high = middle
        records = []
        # Bound pathological duplicate sets without imposing a fleet-size ceiling.
        for index in range(low, min(pages, low + 8)):
            items = page(index)
            records.extend(r for r in items if r["name"].lower() == label)
            if items[-1]["name"].lower() > label or index == pages-1:
                return records
        if low == pages:
            return records
        raise RuntimeError("dns_record_set_too_large")

    def set_address(self, name, address):
        label = self.label(name)
        address = validate_address(address)
        records = self.records(label)
        if any(r["type"] not in ("A", "TXT") for r in records):
            raise RuntimeError("dns_record_conflict")
        # Add before deleting so a failed provider call retains the old endpoint.
        # Firmware suppresses the QR during the brief mixed-address transition.
        if not any(r["type"] == "A" and r["address"] == address for r in records):
            self.call("PUT", json={"force": False, "items": [
                {"type": "A", "name": label, "address": address, "ttl": 60}]})
        old = [{"type": "A", "name": label, "address": r["address"]}
               for r in records if r["type"] == "A" and r["address"] != address]
        if old:
            self.call("DELETE", json=old)
        current = self.records(label)
        if (any(r["type"] not in ("A", "TXT") for r in current) or
                [r["address"] for r in current if r["type"] == "A"] != [address]):
            raise RuntimeError("dns_address_not_converged")

    def put_challenge(self, name, value):
        label = "_acme-challenge." + self.label(name)
        if not re.fullmatch(r"[A-Za-z0-9_-]{43}", value):
            raise ValueError("Invalid DNS-01 value")
        self.call("PUT", json={"force": False, "items": [
            {"type": "TXT", "name": label, "value": value, "ttl": 60}]})

    def remove_challenge(self, name, value):
        label = "_acme-challenge." + self.label(name)
        self.call("DELETE", json=[{"type": "TXT", "name": label, "value": value}])
