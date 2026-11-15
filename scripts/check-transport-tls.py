#!/usr/bin/env python3
"""Check PRIM TLS compatibility without reading or sending API credentials."""
import json
import socket
import ssl
from pathlib import Path

HOST = 'prim.iledefrance-mobilites.fr'
# These ECDHE/AES-GCM suites are enabled in the device's current sdkconfig.
DEVICE_SUITES = ('ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:'
                 'ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384')


def probe(name, version, suites=None, alpn=False):
    result = {'name': name}
    try:
        context = ssl.create_default_context()
        context.minimum_version = version
        context.maximum_version = version
        if suites:
            context.set_ciphers(suites)
        if alpn:
            context.set_alpn_protocols(['http/1.1'])
        with socket.create_connection((HOST, 443), timeout=10) as connection:
            with context.wrap_socket(connection, server_hostname=HOST) as tls:
                result.update(ok=True, protocol=tls.version(), cipher=tls.cipher()[0],
                              alpn=tls.selected_alpn_protocol())
                # Record public certificate metadata only, after normal trust
                # and hostname verification succeeded. Compare the presented
                # chain with the chain the desktop trust store constructed.
                result['peer_certificate'] = tls.getpeercert()
                for kind in ('unverified', 'verified'):
                    getter = getattr(tls._sslobj, f'get_{kind}_chain', None)
                    if getter:
                        chain = getter()
                        result[f'{kind}_chain'] = [cert.get_info() for cert in chain]
                        result[f'{kind}_chain_pem'] = [cert.public_bytes() for cert in chain]
    except (OSError, ValueError) as error:
        result.update(ok=False, error=str(error))
    return result


def main():
    results = [
        probe('TLS 1.2 / desktop suites', ssl.TLSVersion.TLSv1_2),
        probe('TLS 1.2 / device AES-GCM suites', ssl.TLSVersion.TLSv1_2, DEVICE_SUITES),
        probe('TLS 1.2 / device suites / HTTP ALPN', ssl.TLSVersion.TLSv1_2, DEVICE_SUITES, True),
        probe('TLS 1.3', ssl.TLSVersion.TLSv1_3),
    ]
    target = Path(__file__).resolve().parents[1] / 'build-host/transport-tls.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({'openssl': ssl.OPENSSL_VERSION, 'results': results}, indent=2) + '\n')
    for result in results:
        detail = result.get('cipher') if result['ok'] else result['error']
        print(f"{result['name']}: {'OK' if result['ok'] else 'FAILED'} — {detail}")
        if result['ok']:
            for cert in result.get('verified_chain', []):
                print('  Certificate subject:', cert.get('subject'))
                print('  Issuer:', cert.get('issuer'))
    print(f'Report saved: {target.relative_to(target.parents[1])}')
    print('Certificate and hostname verification stayed enabled. No API token was used.')


if __name__ == '__main__':
    main()
