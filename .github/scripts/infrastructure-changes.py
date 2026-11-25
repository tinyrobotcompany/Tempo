#!/usr/bin/env python3
"""Select infrastructure validation using the full Git diff, including deletions."""
import json
import os
from pathlib import Path
import subprocess


def affects_infrastructure(path):
    return (path.startswith(('infra/certificates/', 'scripts/infrastructure/')) or
            (path.startswith('.github/workflows/infrastructure-') and path.endswith('.yaml')) or
            path in {'.github/scripts/infrastructure-changes.py', '.github/workflows/ci.yaml',
                     '.github/scripts/firmware-changes.py', 'tests/test_infrastructure_delivery.py',
                     'tests/test_infrastructure_changes.py', 'tests/test_infrastructure_migration.py'})


def changed_paths(event, head):
    pr = event.get('pull_request')
    if pr:
        args = ['git', 'diff', '--no-renames', '--name-only', '-z', pr['base']['sha']+'...'+pr['head']['sha']]
    else:
        base = event.get('before', '')
        args = (['git', 'diff', '--no-renames', '--name-only', '-z', base, head]
                if base and set(base) != {'0'} else ['git', 'ls-tree', '-r', '--name-only', '-z', head])
    return subprocess.check_output(args).decode().rstrip('\0').split('\0')


def main():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    selected = [p for p in changed_paths(event, os.environ['GITHUB_SHA']) if affects_infrastructure(p)]
    value = str(bool(selected)).lower()
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write('infrastructure='+value+'\n')
    print('Infrastructure inputs changed: '+value)
    print('\n'.join(selected))


if __name__ == '__main__':
    main()
