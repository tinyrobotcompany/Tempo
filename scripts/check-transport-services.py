#!/usr/bin/env python3
"""Capture bounded PRIM contracts for transport integration, without exposing the key."""
import argparse
import http.client
import json
import math
import os
from pathlib import Path
import re
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://prim.iledefrance-mobilites.fr/marketplace/'
LIMIT = 4 * 1024 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def load_key():
    values = []
    for line in (ROOT / '.env').read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        name, value = line.split('=', 1)
        if name.strip().removeprefix('export ') == 'IDFM_PRIM_API_KEY':
            values.append(value.strip().strip('\"\''))
    if len(values) != 1 or not values[0] or any(c.isspace() for c in values[0]):
        raise ValueError('Expected one non-empty IDFM_PRIM_API_KEY without whitespace.')
    return values[0]


def scrub(value, key):
    if isinstance(value, dict):
        return {k: scrub(v, key) for k, v in value.items()
                if k.lower() not in ('apikey', 'api_key', 'authorization', 'token')}
    if isinstance(value, list):
        return [scrub(item, key) for item in value]
    return value.replace(key, '[REDACTED]') if isinstance(value, str) else value


def save_json(path, value):
    # Fixtures stay local and owner-readable; no credential or full URL logging.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write('\n')


def nearby_stops(data):
    stops = []
    for place in data.get('places_nearby', []):
        if not isinstance(place, dict) or place.get('embedded_type') != 'stop_point':
            continue
        stop = place.get('stop_point') or {}
        identifier = stop.get('id', place.get('id', ''))
        # Numeric platform identifiers have a documented SIRI StopPoint mapping.
        match = re.fullmatch(r'stop_point:IDFM:(\d+)', identifier)
        if not match:
            continue
        if any(existing['id'] == identifier for existing in stops):
            continue
        stops.append({'id': identifier, 'monitoring_ref': 'STIF:StopPoint:Q:' + match[1] + ':',
                      'name': stop.get('name', place.get('name', '')), 'lines': stop.get('lines', [])})
    return stops


def siri_summary(data):
    siri = data.get('Siri') or data.get('siri') or {}
    delivery = siri.get('ServiceDelivery') or {}
    deliveries = delivery.get('StopMonitoringDelivery', [])
    if isinstance(deliveries, dict):
        deliveries = [deliveries]
    return {'delivery_count': len(deliveries),
            'visits': sum(len(d.get('MonitoredStopVisit', [])) for d in deliveries),
            'provider_errors': sum(bool(d.get('ErrorCondition')) or d.get('Status') is False
                                   for d in deliveries)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--latitude', type=float, required=True)
    parser.add_argument('--longitude', type=float, required=True)
    parser.add_argument('--radius', type=int, default=2000)
    args = parser.parse_args()
    if not (math.isfinite(args.latitude) and -90 <= args.latitude <= 90 and
            math.isfinite(args.longitude) and -180 <= args.longitude <= 180 and 100 <= args.radius <= 3000):
        parser.error('Invalid coordinates or radius (100–3000 metres).')
    try:
        key = load_key()
    except (OSError, ValueError):
        print('Check the project .env contains exactly one valid IDFM_PRIM_API_KEY.'); return 1
    destination = ROOT / 'build-host/transport-preflight'
    destination.mkdir(parents=True, exist_ok=True)
    report = {'checks': []}
    opener = urllib.request.build_opener(NoRedirect)

    def fetch(label, path, parameters):
        entry = {'service': label}
        report['checks'].append(entry)
        request = urllib.request.Request(BASE + path + '?' + urllib.parse.urlencode(parameters),
                                         headers={'apikey': key, 'Accept': 'application/json',
                                                  'Accept-Encoding': 'identity'})
        try:
            with opener.open(request, timeout=20) as response:
                entry['http'] = response.status
                raw = response.read(LIMIT + 1)
                entry['bytes'] = len(raw)
                if len(raw) > LIMIT:
                    entry['error'] = 'response_over_4MiB'
                    print(f'{label}: HTTP {response.status}, exceeds 4 MiB; stopped safely.'); return None
                data = json.loads(raw)
                if not isinstance(data, dict):
                    entry['error'] = 'unexpected_json_shape'; return None
                entry['json'] = True
                # Raw content is never printed. Redact credentials before local capture.
                save_json(destination / (label + '.json'), scrub(data, key))
                print(f'{label}: HTTP {response.status}, {len(raw)} bytes, JSON saved.')
                return data
        except urllib.error.HTTPError as error:
            entry['http'] = error.code
            print(f'{label}: HTTP {error.code}. Access/quota/endpoint needs checking.')
        except (urllib.error.URLError, OSError, ValueError, http.client.HTTPException) as error:
            entry['error'] = type(error).__name__
            print(f'{label}: {type(error).__name__}; no secret or response contents printed.')
        return None

    nearby = fetch('nearby', f'v2/navitia/coord/{args.longitude:.6f};{args.latitude:.6f}/places_nearby',
                   {'type[]': 'stop_point', 'count': 8, 'distance': args.radius, 'depth': 2,
                    'disable_geojson': 'true', 'disable_disruption': 'true'})
    stops = nearby_stops(nearby) if nearby else []
    report['nearby_mappable_platforms'] = len(stops)
    print(f'Nearby platforms with documented SIRI identifiers: {len(stops)}')
    for index, stop in enumerate(stops[:2]):
        data = fetch(f'departures-{index+1}', 'stop-monitoring', {'MonitoringRef': stop['monitoring_ref']})
        if data:
            summary = siri_summary(data)
            report['checks'][-1].update(summary)
            print(f'Departures {index+1}: {summary["visits"]} visits; {summary["provider_errors"]} provider errors.')
    # If discovery cannot map a platform, independently check the documented
    # Châtelet example to distinguish API permission from identifier issues.
    if not stops:
        data = fetch('departures-reference', 'stop-monitoring', {'MonitoringRef': 'STIF:StopPoint:Q:463158:'})
        if data:
            report['checks'][-1].update(siri_summary(data))
    line_id = next((line.get('id') for stop in stops for line in stop['lines']
                    if isinstance(line, dict) and re.fullmatch(r'line:IDFM:C\d+', line.get('id', ''))), None)
    report['traffic_uses_nearby_line'] = bool(line_id)
    # Known RER C endpoint tests compact responses even if nearby discovery fails.
    selected_line = line_id or 'line:IDFM:C01727'
    traffic = fetch('traffic-compact', 'v2/navitia/line_reports/lines/' + urllib.parse.quote(selected_line, safe='') + '/line_reports',
                    {'count': 1, 'depth': 0, 'disable_geojson': 'true'})
    if traffic:
        lines = [r.get('line', {}).get('id') for r in traffic.get('line_reports', [])]
        report['traffic_line_matches_request'] = selected_line in lines
        report['traffic_report_count'] = len(lines)
        report['disruption_count'] = len(traffic.get('disruptions', []))
    save_json(destination / 'report.json', report)
    print('Diagnostic capture complete: build-host/transport-preflight/report.json')
    print('This checks access and payload contracts; it does not flash or change the display.')
    departures_ok = all(check.get('http') == 200 and check.get('delivery_count', 0) > 0 and
                        check.get('provider_errors', 0) == 0
                        for check in report['checks'] if check['service'].startswith('departures-'))
    return 0 if nearby and stops and traffic and departures_ok and report.get('traffic_line_matches_request') else 2


if __name__ == '__main__':
    sys.exit(main())
