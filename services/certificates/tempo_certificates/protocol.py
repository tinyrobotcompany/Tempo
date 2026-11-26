"""Versioned, signed device protocol shared by the API and manufacturing tools."""
import base64
import hashlib
import ipaddress
import json
import re
from dataclasses import dataclass

from cryptography import x509
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID, ExtensionOID

CONTEXT = b"TEMPO-DEVICE-V1\nPOST\n/v1/device\n"
MAX_BODY = 6144
MAX_CLOCK_SKEW = 300
PRIVATE_NETWORKS = tuple(ipaddress.ip_network(n) for n in
                         ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))


class Rejected(Exception):
    def __init__(self, code, status=400):
        super().__init__(code)
        self.code, self.status = code, status


def public_key(pem):
    try:
        key = serialization.load_pem_public_key(pem.encode())
        if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
            raise ValueError()
        return key
    except (ValueError, TypeError) as error:
        raise Rejected("invalid_public_key") from error


def device_id(pem):
    der = public_key(pem).public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return hashlib.sha256(der).hexdigest()[:32]


def hostname(identifier, suffix):
    if not re.fullmatch(r"[0-9a-f]{32}", identifier):
        raise Rejected("invalid_device_id")
    if not re.fullmatch(r"[a-z0-9-]+(?:\.[a-z0-9-]+)+", suffix) or len(suffix) > 60:
        raise ValueError("Invalid configured DNS suffix")
    return "d-" + identifier + "." + suffix


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise Rejected("duplicate_field")
        result[key] = value
    return result


def decode(body):
    if not body or len(body) > MAX_BODY:
        raise Rejected("request_too_large", 413)
    try:
        data = json.loads(body, object_pairs_hook=no_duplicates)
    except (ValueError, UnicodeError) as error:
        raise Rejected("invalid_json") from error
    if not isinstance(data, dict):
        raise Rejected("invalid_request")
    common = {"device_id", "action", "nonce", "issued_at"}
    extra = {"certificate": {"csr"}, "address": {"address"}, "status": set()}
    action = data.get("action")
    if not isinstance(action, str) or action not in extra or set(data) != common | extra[action]:
        raise Rejected("invalid_fields")
    if not isinstance(data["device_id"], str) or not re.fullmatch(r"[0-9a-f]{32}", data["device_id"]):
        raise Rejected("invalid_device_id")
    if not isinstance(data["nonce"], str) or not re.fullmatch(r"[0-9a-f]{64}", data["nonce"]):
        raise Rejected("invalid_nonce")
    if type(data["issued_at"]) is not int:
        raise Rejected("invalid_time")
    return data


def authenticate(data, body, signature, registered_pem, now):
    if abs(now - data["issued_at"]) > MAX_CLOCK_SKEW:
        raise Rejected("stale_request", 401)
    try:
        if len(signature) > 104:
            raise ValueError()
        encoded = base64.b64decode(signature, validate=True)
        public_key(registered_pem).verify(encoded, CONTEXT + body, ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError, TypeError) as error:
        raise Rejected("invalid_signature", 401) from error


def validate_csr(pem, expected_hostname):
    try:
        if not isinstance(pem, str) or not 0 < len(pem) <= 4096:
            raise ValueError()
        csr = x509.load_pem_x509_csr(pem.encode())
        key = csr.public_key()
        if not csr.is_signature_valid or not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
            raise ValueError()
        if csr.subject != x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, expected_hostname)]):
            raise ValueError()
        if len(csr.extensions) != 1 or csr.extensions[0].oid != ExtensionOID.SUBJECT_ALTERNATIVE_NAME:
            raise ValueError()
        if list(csr.extensions[0].value) != [x509.DNSName(expected_hostname)]:
            raise ValueError()
        return csr
    except (ValueError, TypeError, x509.ExtensionNotFound) as error:
        raise Rejected("invalid_csr") from error


def validate_address(value):
    try:
        if not isinstance(value, str):
            raise ValueError()
        address = ipaddress.IPv4Address(value)
        if not any(address in network for network in PRIVATE_NETWORKS):
            raise ValueError()
        return str(address)
    except ValueError as error:
        raise Rejected("invalid_lan_address") from error


@dataclass
class Reply:
    body: dict
    status: int = 200
