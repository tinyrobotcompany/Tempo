#!/usr/bin/env python3
"""Prepare Tempo releases locally; never commit, tag, publish or flash."""
import argparse
from datetime import date
from pathlib import Path
import re
import sys

VERSION = re.compile(r'project\(weather_station VERSION (\d+\.\d+\.\d+)\)')
NOTE = re.compile(r'\A---\s*\n["\']?tempo["\']?: (patch|minor|major)\s*\n---\s*\n(.+)', re.S)
SEMVER = re.compile(r'0|[1-9]\d*')


def version_tuple(value):
    parts = value.split('.')
    if len(parts) != 3 or not all(SEMVER.fullmatch(p) for p in parts):
        raise ValueError('Expected a stable version X.Y.Z without leading zeroes')
    return tuple(map(int, parts))


def pending(root):
    notes = []
    for path in sorted((root / '.changeset').glob('*.md')):
        if path.name == 'README.md':
            continue
        match = NOTE.fullmatch(path.read_text().strip())
        if not match or not match[2].strip():
            raise ValueError(f'Invalid change note: {path.name}')
        notes.append((path, match[1], match[2].strip()))
    return notes


def current(root):
    cmake = root / 'firmware/CMakeLists.txt'
    text = cmake.read_text()
    matches = list(VERSION.finditer(text))
    if len(matches) != 1:
        raise ValueError('Expected exactly one firmware project version')
    value = matches[0][1]
    version_tuple(value)
    return cmake, text, value


def check(root, tag=None):
    _, _, version = current(root)
    notes = pending(root)
    if tag is not None:
        if tag != 'v' + version:
            raise ValueError('Release tag must match the firmware version')
        if notes:
            raise ValueError('Prepare the release first: pending change notes remain')
        path = root / 'docs/releases' / (version + '.md')
        if not path.is_file() or not path.read_text().startswith(f'# Tempo {version}\n'):
            raise ValueError('Missing or mismatched versioned release notes')
        if '## ' + version + ' — ' not in (root / 'CHANGELOG.md').read_text():
            raise ValueError('Release is missing from CHANGELOG.md')
    return version, notes


def prepare(root, today=None):
    cmake, text, version = current(root)
    notes = pending(root)
    if not notes:
        raise ValueError('No pending change notes; no version bump performed')
    bump = max((n[1] for n in notes), key=['patch', 'minor', 'major'].index)
    major, minor, patch = version_tuple(version)
    if bump == 'major':
        major, minor, patch = major + 1, 0, 0
    elif bump == 'minor':
        minor, patch = minor + 1, 0
    else:
        patch += 1
    new = f'{major}.{minor}.{patch}'
    stamp = today or date.today().isoformat()
    dest = root / 'docs/releases' / (new + '.md')
    changelog = root / 'CHANGELOG.md'
    old = changelog.read_text() if changelog.exists() else '# Changelog\n'
    if dest.exists() or re.search(r'^## ' + re.escape(new) + r'\b', old, re.M):
        raise ValueError(f'Release notes already exist for {new}; refusing overwrite')
    if not old.startswith('# Changelog\n'):
        raise ValueError('Unexpected changelog format')
    changes = '\n\n'.join('### ' + path.stem.replace('-', ' ') + '\n\n' + body
                            for path, _, body in notes)
    body = (f'Prepared: {stamp}\n\n{changes}\n\n'
            '## Compatibility and update status\n\n'
            '- Target: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1, 32 MB flash/PSRAM.\n'
            '- This is a source release. No distributable firmware image is attached.\n'
            '- OTA delivery is not available; use the documented wired update procedure.\n'
            '- Preparing these notes does not validate hardware or publish the release.\n'
            '- Record breaking changes, migration steps and known issues in the change notes before release.\n')
    # All validation precedes edits. Git provides the reviewable preparation diff.
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(f'# Tempo {new}\n\n' + body)
    changelog.write_text('# Changelog\n\n' + f'## {new} — {stamp}\n\n' + body + '\n' + old[len('# Changelog\n'):].lstrip())
    cmake.write_text(VERSION.sub(f'project(weather_station VERSION {new})', text, count=1))
    readme = root / 'README.md'
    if readme.exists():
        content = readme.read_text()
        content = re.sub(r'firmware-\d+\.\d+\.\d+-blue', 'firmware-' + new + '-blue', content)
        content = re.sub(r'Current firmware: \*\*\d+\.\d+\.\d+\*\*', 'Current firmware: **' + new + '**', content)
        readme.write_text(content)
    for path, _, _ in notes:
        path.unlink()
    return new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'prepare'])
    parser.add_argument('--tag', help='Validate release readiness for an exact vX.Y.Z tag')
    args = parser.parse_args()
    if args.tag and args.action != 'check':
        parser.error('--tag is only valid with check')
    root = Path(__file__).resolve().parents[1]
    try:
        if args.action == 'prepare':
            print('Prepared Tempo ' + prepare(root) + '; review and test before committing.')
        else:
            version, notes = check(root, args.tag)
            print(f'Tempo {version}: {len(notes)} valid pending change notes.')
    except (ValueError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
