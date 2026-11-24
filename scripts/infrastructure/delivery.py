#!/usr/bin/env python3
"""GitHub-only infrastructure delivery. No local apply path or arbitrary target."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SUBSCRIPTION = '923dd002-44bc-431c-b986-464d2ca524a2'
TENANT = '17ca2540-dd3e-4204-b2f7-a3e3ad209719'
ROOT = Path(__file__).resolve().parents[2]
CONCERNS = ('network', 'data', 'security', 'ops', 'compute')
REGISTRY = 'acrtempo22bw55qnmg6pm.azurecr.io/tempo-certificates'


def az(*args):
    result = subprocess.run(['az', *args, '--only-show-errors', '-o', 'json'],
                            text=True, capture_output=True, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def guard():
    if (os.environ.get('GITHUB_ACTIONS') != 'true' or
            os.environ.get('GITHUB_REPOSITORY') != 'tinyrobotcompany/Tempo' or
            os.environ.get('GITHUB_REF') != 'refs/heads/main' or
            os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch'):
        raise ValueError('Cloud operations run only in the manually dispatched main-branch workflow')
    account = az('account', 'show')
    if account['id'] != SUBSCRIPTION or account['tenantId'] != TENANT:
        raise ValueError('Wrong Azure account scope')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    remote = subprocess.check_output(['git', 'ls-remote', 'origin', 'refs/heads/main'], text=True).split()[0]
    if head != os.environ['GITHUB_SHA'] or head != remote:
        raise ValueError('Main advanced or checkout differs: start a fresh reviewed run')
    return head


def parameters(image, acme):
    if acme not in ('staging', 'production'):
        raise ValueError('Invalid certificate authority environment')
    if image and not re.fullmatch(re.escape(REGISTRY) + r'@sha256:[0-9a-f]{64}', image):
        raise ValueError('Image must be an immutable digest in the existing Tempo registry')
    return {'deployCompute': {'value': bool(image)}, 'image': {'value': image},
            'acmeEnvironment': {'value': acme}}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(kind, directory):
    return ('deployment', 'sub', kind, '--subscription', SUBSCRIPTION, '--location', 'westeurope',
            '--name', 'tempo-infrastructure', '--template-file', str(directory / 'template.json'),
            '--parameters', '@' + str(directory / 'parameters.json'))


def safe_preview(value):
    """Never publish resolved workspace/credential values in a public-repo artifact."""
    if isinstance(value, list):
        return [safe_preview(item) for item in value]
    if isinstance(value, dict):
        if any(word in str(value.get('path', '')).lower() for word in ('sharedkey', 'secrets', 'password', 'connectionstring', 'accesskey')):
            return {'path': value['path'], 'redacted_sha256': hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()}
        return {key: ({'redacted_sha256': hashlib.sha256(json.dumps(item, sort_keys=True).encode()).hexdigest()}
                      if key.lower() in ('sharedkey', 'secrets', 'password', 'connectionstring', 'accesskey', 'primarykey', 'secondarykey')
                      else safe_preview(item)) for key, item in value.items()}
    return value


def changes(result):
    if result.get('status') != 'Succeeded':
        raise ValueError('Azure what-if did not succeed')
    entries = result.get('changes')
    if not isinstance(entries, list):
        raise ValueError('Azure what-if changes are missing or malformed')
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get('resourceId'), str) or not entry['resourceId'] or not isinstance(entry.get('changeType'), str):
            raise ValueError('Malformed Azure what-if change')
        if entry['changeType'].lower() == 'delete':
            raise ValueError('Normal deployment cannot delete resources; use reviewed migration')
    return sorted(safe_preview(entries), key=lambda c: c['resourceId'].lower())


def require_migrated():
    for concern in CONCERNS:
        group = 'rg-tempo-staging-westeurope-' + concern
        if az('group', 'exists', '--subscription', SUBSCRIPTION, '--name', group):
            raise ValueError('Complete the approved group migration before normal deployment')


def plan(directory, image, acme):
    head = guard()
    require_migrated()
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run(['az', 'bicep', 'build', '--file', str(ROOT / 'infra/certificates/main.bicep'),
                    '--outfile', str(directory / 'template.json')], check=True)
    (directory / 'parameters.json').write_text(json.dumps({'parameters': parameters(image, acme)}, indent=2))
    preview = az(*command('what-if', directory), '--result-format', 'FullResourcePayloads')
    (directory / 'what-if.json').write_text(json.dumps(safe_preview(preview), indent=2))
    normalized = changes(preview)
    # ARM may not resolve role assignment IDs from newly created identity outputs.
    # Surface diagnostics; never pretend that What-If fully validated these resources.
    manifest = {'sha': head, 'template': digest(directory / 'template.json'),
                'parameters': digest(directory / 'parameters.json'), 'changes': normalized}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    summary = Path(os.environ['GITHUB_STEP_SUMMARY'])
    with summary.open('a') as out:
        out.write(f'## Tempo infrastructure preview\nCommit: `{head}`\n\n')
        for item in normalized:
            out.write(f"- {item['changeType']}: `{item['resourceId']}`\n")
        out.write('\nReview the full what-if artifact and its diagnostics before approving Apply.\n')


def apply(directory):
    head = guard()
    manifest = json.loads((directory / 'manifest.json').read_text())
    if (manifest['sha'] != head or manifest['template'] != digest(directory / 'template.json') or
            manifest['parameters'] != digest(directory / 'parameters.json')):
        raise ValueError('Approved plan integrity check failed')
    require_migrated()
    preview = az(*command('what-if', directory), '--result-format', 'FullResourcePayloads')
    if changes(preview) != manifest['changes']:
        raise ValueError('Azure changed since preview: review a fresh plan')
    require_migrated()
    result = az(*command('create', directory))
    if result['properties']['provisioningState'] != 'Succeeded':
        raise ValueError('Deployment failed')
    (directory / 'deployment.json').write_text(json.dumps(result, indent=2))
    verify()


def verify():
    for concern in CONCERNS:
        group = 'rg-tempo-westeurope-' + concern
        for resource in az('resource', 'list', '--subscription', SUBSCRIPTION, '--resource-group', group):
            if resource['type'].lower() in ('microsoft.storage/storageaccounts', 'microsoft.keyvault/vaults'):
                detail = az('resource', 'show', '--ids', resource['id'])
                if detail['properties'].get('publicNetworkAccess') != 'Disabled':
                    raise ValueError('Private data resource became public: ' + resource['id'])
    print('PASS: resource inventory and private storage/vault access policy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('plan', 'apply', 'verify'))
    parser.add_argument('--directory', type=Path, default=ROOT / 'build-host/infrastructure')
    parser.add_argument('--image', default='')
    parser.add_argument('--acme', choices=('staging', 'production'), default='staging')
    args = parser.parse_args()
    if args.operation == 'plan':
        plan(args.directory, args.image, args.acme)
    elif args.operation == 'apply':
        apply(args.directory)
    else:
        guard()
        verify()


if __name__ == '__main__':
    main()
