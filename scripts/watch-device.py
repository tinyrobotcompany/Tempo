#!/usr/bin/env python3
"""Build and app-flash changed firmware from a normal local Terminal session."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent

def fingerprint(root=ROOT):
    firmware = root / 'firmware'
    files = []
    for folder in ('main', 'components', 'vendor'):
        files.extend(p for p in (firmware / folder).rglob('*')
                     if p.is_file() and p.suffix in ('.c', '.h', '.inc', '.txt', '.yml', '.cmake', '.html', '.js', '.css'))
    files.extend(p for p in firmware.iterdir() if p.is_file() and
                 (p.name.startswith('sdkconfig') and p.suffix != '.old' or
                  p.name in ('CMakeLists.txt', 'partitions.csv')))
    credential = root / '.env'
    if credential.exists():
        files.append(credential)
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(str(path.relative_to(root)).encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()

def build_and_flash(port, run=subprocess.run, root=ROOT):
    # App-only updates must retain the partition table used by the previous build.
    table = root / 'firmware/build/partition_table/partition-table.bin'
    if not table.exists():
        print('No existing partition table: complete the documented first flash before using the watcher.', flush=True)
        return False
    baseline = root / 'build-host/watch-partition-table.bin'
    if not baseline.exists():
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_bytes(table.read_bytes())
    original_table = baseline.read_bytes()
    before = fingerprint(root)
    command = [str(root / 'scripts/idf.sh')]
    if run(command + ['build'], cwd=root).returncode:
        print('Build failed; nothing flashed.', flush=True)
        return False
    if fingerprint(root) != before:
        print('Sources/configuration changed during build; rebuilding before any flash.', flush=True)
        return False
    if table.read_bytes() != original_table:
        print('Partition layout changed; app-only flashing refused. A deliberate full flash is required.', flush=True)
        return False
    if run(command + ['-p', port, 'app-flash'], cwd=root).returncode:
        print('Flash failed. Close other serial monitors and check the USB connection; press Enter to retry.', flush=True)
        return False
    print('Updated device successfully. Watching for further changes. Ctrl+C stops.', flush=True)
    return True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/cu.usbmodem5C940599581')
    args = parser.parse_args()
    print('Close any existing serial monitor. Build/flash output is shown here.', flush=True)
    previous = None
    try:
        while True:
            current = fingerprint()
            if current != previous:
                time.sleep(2)
                if fingerprint() != current:
                    continue
                build_and_flash(args.port)
                # sdkconfig can be updated by a successful configure; recheck once.
                previous = current
            # Enter explicitly retries a failed build/flash without editing files.
            import select
            import sys
            if select.select([sys.stdin], [], [], 1)[0]:
                if sys.stdin.readline() == '':
                    time.sleep(1)
                else:
                    previous = None
    except KeyboardInterrupt:
        print('\nWatcher stopped.', flush=True)

if __name__ == '__main__':
    main()
