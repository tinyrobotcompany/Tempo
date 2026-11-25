#!/usr/bin/env python3
"""TEST FIXTURE ONLY: ephemeral CA for the loopback HTTPS probe test.

No manufacturing or device provisioning entry point. Production keys are generated
on the ESP; this helper is used only with TemporaryDirectory test fixtures.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import sys

OPENSSL = os.environ.get("TEMPO_OPENSSL") or next((str(p) for p in (Path("/opt/homebrew/opt/openssl@3/bin/openssl"), Path("/usr/local/opt/openssl@3/bin/openssl")) if p.is_file()), "openssl")


def run(*args, data=None):
    if args[0] == "openssl": args = (OPENSSL, *args[1:])
    result = subprocess.run(args, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError('Identity operation failed: ' + args[0] + ' ' + args[1])
    return result.stdout


def hostname(value):
    if len(value) > 96 or not re.fullmatch(r'[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?', value):
        raise ValueError('Use a lowercase DNS hostname, at most 96 characters.')
    labels = value.split('.')
    if len(labels) < 2 or all(label.isdigit() for label in labels) or any(
        not label or len(label) > 63 or label.startswith('-') or label.endswith('-') for label in labels
    ):
        raise ValueError('Use a DNS hostname, not an IP address or wildcard.')
    return value


def private_directory(path):
    path = path.resolve()
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    if path.stat().st_mode & 0o077:
        raise ValueError('Identity directory must have permissions 0700.')
    return path


def create(directory, name):
    name = hostname(name)
    directory = private_directory(directory)
    if any(directory.iterdir()):
        raise ValueError('Use an empty directory; existing identities are never overwritten.')
    run('openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt', 'ec_param_enc:named_curve', '-out', str(directory/'device.key'))
    (directory/'device.key').chmod(0o600)
    run('openssl', 'req', '-new', '-sha256', '-key', str(directory/'device.key'), '-subj', '/CN='+name,
        '-addext', 'subjectAltName=DNS:'+name, '-out', str(directory/'device.csr'))
    (directory/'identity.json').write_text(json.dumps({'schema': 1, 'hostname': name})+'\n')


def development_certificate(directory):
    """Private test CA, distinct from the device key. Never a production issuer."""
    name = hostname(json.loads((directory/'identity.json').read_text())['hostname'])
    if (directory/'ca.key').exists() or (directory/'fullchain.pem').exists():
        raise ValueError('Refusing to replace an existing CA or certificate.')
    run('openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-nodes', '-sha256', '-days', '365',
        '-subj', '/CN=Tempo development trust ONLY', '-addext', 'basicConstraints=critical,CA:TRUE,pathlen:0',
        '-addext', 'keyUsage=critical,keyCertSign,cRLSign', '-addext', 'subjectKeyIdentifier=hash',
        '-keyout', str(directory/'ca.key'), '-out', str(directory/'ca.pem'))
    (directory/'ca.key').chmod(0o600)
    extensions = directory/'extensions.cnf'
    extensions.write_text('subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:'+name+'\n')
    run('openssl', 'x509', '-req', '-in', str(directory/'device.csr'), '-CA', str(directory/'ca.pem'),
        '-CAkey', str(directory/'ca.key'), '-CAcreateserial', '-days', '30', '-sha256',
        '-extfile', str(extensions), '-out', str(directory/'fullchain.pem'))
