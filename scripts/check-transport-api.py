#!/usr/bin/env python3
"""Check PRIM access from the host Terminal without displaying the API key."""
import json
import http.client
import socket
import sys
import urllib.error
import urllib.request
from pathlib import Path


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # Never forward credentials to a redirected destination.


def inspect_response(response):
    """Print response metadata only; never echo server content or request headers."""
    print(f'PRIM HTTP status: {response.status}')
    content_type = response.headers.get_content_type()
    encoding = response.headers.get('Content-Encoding', 'identity').lower()
    print('Response format: ' + ('JSON' if 'json' in content_type else
                               'HTML' if content_type == 'text/html' else 'other'))
    print('Response encoding: ' + ('identity' if encoding == 'identity' else 'compressed/other'))
    limit = 4 * 1024 * 1024
    raw = response.read(limit + 1)
    print(f'Response bytes read: {len(raw)}')
    if len(raw) > limit:
        print('Response exceeds the 4 MiB diagnostic limit; JSON was not parsed.')
        return 3
    if encoding != 'identity':
        print('Server returned an encoded response despite requesting identity encoding.')
        return 3
    if not raw:
        print('Response body is empty.')
        return 3
    try:
        body = json.loads(raw)
    except json.JSONDecodeError as error:
        print(f'Invalid JSON at character {error.pos}: {error.msg}.')
        if content_type == 'text/html':
            print('The API returned an HTML page, not traffic JSON.')
        return 3
    except UnicodeDecodeError:
        print('Response is not valid JSON text encoding.')
        return 3
    if not isinstance(body, dict) or not isinstance(body.get('line_reports'), list):
        print('Valid JSON, but expected line_reports array is absent.')
        print('API error object present: ' + str(isinstance(body, dict) and 'error' in body))
        return 3
    print('PRIM OK: token accepted and traffic API returned valid data.')
    print(f'Traffic reports returned: {len(body["line_reports"])}')
    print('Nearby-stop and departure API access still needs its own integration check.')
    return 0


def main():
    path = Path(__file__).resolve().parents[1] / '.env'
    if not path.is_file():
        print('Missing project .env file.'); return 1
    keys = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        name, value = line.split('=', 1)
        if name.strip().removeprefix('export ') == 'IDFM_PRIM_API_KEY':
            keys.append(value.strip().strip('\"\''))
    if len(keys) != 1 or not keys[0] or any(c.isspace() for c in keys[0]):
        print('Expected one non-empty IDFM_PRIM_API_KEY without whitespace.'); return 1
    host = 'prim.iledefrance-mobilites.fr'
    try:
        socket.getaddrinfo(host, 443, type=socket.SOCK_STREAM)
    except OSError:
        print('DNS FAILED: cannot resolve PRIM from this Terminal. No token was sent.'); return 2
    print('DNS OK: PRIM resolved.')
    url = ('https://' + host + '/marketplace/v2/navitia/line_reports/'
           'lines/line%3AIDFM%3AC01727/line_reports?count=1')
    request = urllib.request.Request(url, headers={'apikey': keys[0], 'Accept': 'application/json',
                                                  'Accept-Encoding': 'identity'})
    try:
        with urllib.request.build_opener(NoRedirect).open(request, timeout=15) as response:
            return inspect_response(response)
    except urllib.error.HTTPError as error:
        hints = {401: 'Token rejected.', 403: 'Access denied; check API subscription/permissions.',
                 429: 'API quota/rate limit reached.'}
        print(f'PRIM HTTP {error.code}: ' + hints.get(error.code, 'API request unsuccessful.'))
    except urllib.error.URLError as error:
        print('Connection/TLS failed: ' + type(error.reason).__name__)
    except http.client.IncompleteRead as error:
        print(f'Connection ended early: received {len(error.partial)} bytes in the incomplete read.')
    except TimeoutError:
        print('Timed out while connecting to or reading PRIM (15-second timeout).')
    except (ValueError, OSError) as error:
        print('Response processing failed: ' + type(error).__name__)
    return 3


if __name__ == '__main__':
    sys.exit(main())
