#!/usr/bin/env python3
"""Fail closed on missing first-party C instrumentation or <80% line coverage."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {
    'firmware/components/station_core/cities.inc': 'Generated city data, not executable code.',
    'firmware/main/main.c': 'ESP-IDF/BSP startup, physical touch/display and reset watchdog; device acceptance required.',
    'firmware/components/station_core/timezones.c': 'Generated IANA table; tested but excluded from percentage to avoid inflating it.',
}
BINARIES = ['test_core', 'test_settings', 'test_network', 'test_weather', 'test_weather_service',
            'test_transport', 'test_transport_service', 'test_transport_json_filter',
            'test_phone_protocol', 'test_phone_server', 'test_phone_server_installer',
            'ui/test_ui', 'ui/test_map_tiles']


def evaluate(files, expected, threshold=80):
    rows = []
    for path in sorted(expected):
        if path not in files or files[path]['count'] == 0:
            raise ValueError('Missing executable coverage for ' + path)
        row = dict(path=path, **files[path])
        row['percent'] = 100 * row['covered'] / row['count']
        rows.append(row)
    failed = [r['path'] for r in rows if r['percent'] < threshold]
    return rows, failed


def main():
    output = ROOT / 'build-host/coverage'
    args = ['xcrun', 'llvm-cov', 'export', '-instr-profile=' + str(output / 'coverage.profdata')]
    for name in BINARIES:
        args += ['-object', str(ROOT / 'build-host' / name)]
    exported = json.loads(subprocess.check_output(args))
    expected = {str(p.relative_to(ROOT)) for base in ('firmware/main', 'firmware/components')
                for p in (ROOT / base).rglob('*') if p.suffix in ('.c', '.inc')
                and 'fonts' not in p.parts and str(p.relative_to(ROOT)) not in EXCLUDED}
    files = {}
    for record in exported['data'][0]['files']:
        path = Path(record['filename']).resolve()
        if path.is_relative_to(ROOT):
            files[str(path.relative_to(ROOT))] = record['summary']['lines']
    rows, failed = evaluate(files, expected)
    count = sum(r['count'] for r in rows)
    covered = sum(r['covered'] for r in rows)
    report = {'metric': 'executable line coverage (host unit and integration tests)',
              'minimum_per_file': 80, 'count': count, 'covered': covered,
              'percent': 100 * covered / count, 'files': rows, 'exclusions': EXCLUDED,
              'other_exclusions': ['generated fonts', 'third-party SDK/LVGL/cJSON', 'test code']}
    (output / 'firmware.json').write_text(json.dumps(report, indent=2) + '\n')
    for r in rows:
        print(f"{r['percent']:6.2f}% {r['path']}")
    print(f"Firmware host coverage: {report['percent']:.2f}% ({covered}/{count}); minimum 80% for EVERY included file")
    show = args.copy();show[2] = 'show'
    del show[4]  # llvm-cov show requires its first binary as a positional argument
    show += ['-format=html', '-output-dir=' + str(output / 'html')] + [str(ROOT / p) for p in sorted(expected)]
    subprocess.run(show, stdout=subprocess.DEVNULL, check=True)
    unit_files = {p for p in expected if p.startswith('firmware/components/station_core/')
                  or p.startswith('firmware/components/station_weather/')
                  or p.startswith('firmware/components/station_transport/')
                  or p == 'firmware/main/phone_protocol.c'}
    unit_args = args.copy()
    unit_args[3] = '-instr-profile=' + str(output / 'unit.profdata')
    unit_data = json.loads(subprocess.check_output(unit_args))
    unit_records = {str(Path(f['filename']).resolve().relative_to(ROOT)): f['summary']['lines']
                    for f in unit_data['data'][0]['files'] if Path(f['filename']).resolve().is_relative_to(ROOT)}
    unit_rows, unit_failed = evaluate(unit_records, unit_files)
    unit_count = sum(r['count'] for r in unit_rows)
    unit_covered = sum(r['covered'] for r in unit_rows)
    unit_report = {'metric': 'core unit-test executable line coverage (integration profiles excluded)',
                   'minimum_per_file': 80, 'percent': 100 * unit_covered / unit_count, 'files': unit_rows}
    (output / 'unit.json').write_text(json.dumps(unit_report, indent=2) + '\n')
    print(f"Core unit-only coverage: {unit_report['percent']:.2f}%")
    failed += unit_failed
    if failed:
        raise SystemExit('Coverage below 80%: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
