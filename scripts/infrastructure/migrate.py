#!/usr/bin/env python3
"""One-time, approved migration of the known Tempo foundation, never arbitrary groups.

Private endpoints must be recreated; data, vault, identities and registry move
in place. On failure retain all groups and state. Re-plan to resume after reviewing
current locations; source groups are deleted only after verification.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

from delivery import ROOT, SUBSCRIPTION, CONCERNS, az, guard, verify

DIRECTORY = ROOT / 'build-host/infrastructure'
OLD = 'rg-tempo-staging-westeurope-'
NEW = 'rg-tempo-westeurope-'
EXPECTED = {
    'network': {
        ('Microsoft.Network/privateDnsZones', 'privatelink.table.core.windows.net'),
        ('Microsoft.Network/privateDnsZones', 'privatelink.vaultcore.azure.net'),
        ('Microsoft.Network/virtualNetworks', 'tempo-staging-vnet'),
    },
    'data': {
        ('Microsoft.Storage/storageAccounts', 'sttempo22bw55qnmg6pm'),
        ('Microsoft.ContainerRegistry/registries', 'acrtempo22bw55qnmg6pm'),
    },
    'security': {('Microsoft.KeyVault/vaults', 'kv-tempo-22bw55qnmg6pm')} | {
        ('Microsoft.ManagedIdentity/userAssignedIdentities', 'tempo-staging-' + role)
        for role in ('api', 'worker', 'factory', 'bootstrap')
    },
    'ops': {('Microsoft.OperationalInsights/workspaces', 'tempo-staging-logs')},
    'compute': set(),
}
ENDPOINTS = {'tempo-table-endpoint', 'tempo-vault-endpoint'}


def known(resource, concern):
    pair = resource['type'], resource['name']
    if pair in EXPECTED[concern]:
        return True
    if concern == 'network' and pair[0] == 'Microsoft.Network/privateDnsZones/virtualNetworkLinks':
        return pair[1] in ('privatelink.table.core.windows.net/tempo-staging-table',
                           'privatelink.vaultcore.azure.net/tempo-staging-vault')
    if concern == 'security':
        if pair == ('Microsoft.ManagedIdentity/userAssignedIdentities/federatedIdentityCredentials', 'tempo-infrastructure-delivery/github-approved-infrastructure'):
            return resource['resourceGroup'] == NEW + concern
        if pair == ('Microsoft.ManagedIdentity/userAssignedIdentities', 'tempo-infrastructure-delivery'):
            return resource['resourceGroup'] == NEW + concern
        if pair[0] == 'Microsoft.Network/privateEndpoints':
            return pair[1] in ENDPOINTS
        if pair[0] == 'Microsoft.Network/networkInterfaces':
            return any(pair[1].startswith(name + '.nic.') for name in ENDPOINTS)
    return False


def inventory():
    result = {}
    for concern in CONCERNS:
        for prefix in (OLD, NEW):
            group = prefix + concern
            exists = az('group', 'exists', '--subscription', SUBSCRIPTION, '--name', group)
            result[group] = az('resource', 'list', '--subscription', SUBSCRIPTION,
                               '--resource-group', group) if exists else []
            for resource in result[group]:
                if not known(resource, concern):
                    raise ValueError('Unexpected resource; refuse migration: ' + resource['id'])
        actual = [(r['type'], r['name']) for prefix in (OLD, NEW) for r in result[prefix + concern]
                  if (r['type'], r['name']) in EXPECTED[concern]]
        if len(actual) != len(EXPECTED[concern]) or set(actual) != EXPECTED[concern]:
            raise ValueError('Missing or duplicate foundation resources: ' + concern)
    return result


def resource_roles(destination_only=False):
    roles = az('role', 'assignment', 'list', '--subscription', SUBSCRIPTION, '--all')
    # Resource-scoped assignments only. Bootstrap group grants deliberately stay
    # with their groups and disappear when the empty source groups are deleted.
    return sorted([{'scope': r['scope'].lower(),
                    'principalId': r['principalId'], 'roleDefinitionId': r['roleDefinitionId'].lower()}
                   for r in roles if '/providers/' in r['scope'].lower()
                   and any('/resourcegroups/' + prefix in r['scope'].lower() for prefix in ((NEW,) if destination_only else (OLD, NEW)))],
                  key=lambda r: (r['scope'], r['principalId'], r['roleDefinitionId']))


def expected_destination_roles(roles):
    expected = set()
    for role in roles:
        scope = role['scope'].lower()
        # Translate only the resource-group path segment, never a substring of a resource name.
        scope = scope.replace('/resourcegroups/' + OLD.lower(), '/resourcegroups/' + NEW.lower(), 1)
        expected.add((scope, role['principalId'], role['roleDefinitionId'].lower()))
    return [dict(scope=scope, principalId=principal, roleDefinitionId=definition)
            for scope, principal, definition in sorted(expected)]


def verify_roles(expected):
    if resource_roles(destination_only=True) != expected:
        raise ValueError('Destination runtime permissions differ; retain source groups')


def layout(items):
    return {g: sorted(r['id'].lower() for r in rows) for g, rows in items.items()}


def plan():
    sha = guard()
    items = inventory()
    identities = {}
    for rows in items.values():
        for r in rows:
            if r['type'] == 'Microsoft.ManagedIdentity/userAssignedIdentities':
                identities[r['name']] = az('resource', 'show', '--ids', r['id'])['properties']['principalId']
    roles = resource_roles()
    # This migration is scoped to the inspected foundation, not a running fleet.
    if len(expected_destination_roles(roles)) != 11:
        raise ValueError('Runtime role inventory differs from reviewed foundation')
    DIRECTORY.mkdir(parents=True, exist_ok=True)
    manifest = {'sha': sha, 'layout': layout(items), 'identities': identities, 'roles': roles, 'destination_roles': expected_destination_roles(roles)}
    (DIRECTORY / 'migration.json').write_text(json.dumps(manifest, indent=2))
    with Path(os.environ['GITHUB_STEP_SUMMARY']).open('a') as out:
        out.write('## Resource-group migration\n\n')
        out.write('Move the known foundation in place; preserve resource names, data and identity principal IDs.\n\n')
        out.write('Recreate ONLY the two private endpoints and their managed NICs/DNS bindings. Public access stays disabled.\n\n')
        out.write('Azure validates each move at execution after endpoint removal. An unsupported move stops the run; no source group is deleted on failure.\n\n')
        for group, ids in manifest['layout'].items():
            out.write(f'- `{group}`: {len(ids)} resources\n')
        out.write('\nAfter access/network verification, delete only the five EMPTY old Tempo groups.\n')


def assert_empty(group):
    if group not in {OLD + c for c in CONCERNS}:
        raise ValueError('Deletion outside approved source groups')
    if az('resource', 'list', '--subscription', SUBSCRIPTION, '--resource-group', group):
        raise ValueError('Refuse deletion of nonempty group: ' + group)


def detach_endpoints(items):
    (DIRECTORY / 'migration-progress.json').write_text(json.dumps(layout(items), indent=2))
    # Removing these connections temporarily closes the already-private data path;
    # it never opens storage or Key Vault to the internet. No application is deployed.
    for group, rows in items.items():
        for resource in rows:
            if resource['type'] == 'Microsoft.Network/privateEndpoints' and group.startswith(OLD):
                az('network', 'private-endpoint', 'delete', '--subscription', SUBSCRIPTION,
                   '--resource-group', group, '--name', resource['name'])
                az('network', 'private-endpoint', 'wait', '--subscription', SUBSCRIPTION,
                   '--resource-group', group, '--name', resource['name'],
                   '--deleted', '--interval', '10', '--timeout', '600')
    # Managed NIC deletion can lag behind deletion of the parent endpoint.
    for group, rows in items.items():
        for resource in rows:
            if resource['type'] == 'Microsoft.Network/networkInterfaces' and group.startswith(OLD):
                az('resource', 'wait', '--ids', resource['id'],
                   '--deleted', '--interval', '10', '--timeout', '600')


def apply():
    sha = guard()
    approved = json.loads((DIRECTORY / 'migration.json').read_text())
    items = inventory()
    if approved['sha'] != sha or approved['layout'] != layout(items) or approved['roles'] != resource_roles():
        raise ValueError('Migration changed since preview: start a fresh run')
    detach_endpoints(items)
    for concern in ('data', 'ops', 'network', 'security'):
        rows = az('resource', 'list', '--subscription', SUBSCRIPTION, '--resource-group', OLD + concern) \
            if az('group', 'exists', '--subscription', SUBSCRIPTION, '--name', OLD + concern) else []
        ids = [r['id'] for r in rows if (r['type'], r['name']) in EXPECTED[concern]]
        if ids:
            # Azure's move operation performs provider validation and waits for completion.
            az('resource', 'move', '--subscription', SUBSCRIPTION, '--destination-group', NEW + concern, '--ids', *ids)
        (DIRECTORY / 'migration-progress.json').write_text(json.dumps(layout(inventory()), indent=2))
    # Reapply the reviewed foundation to recreate connections and correctly scoped
    # role assignments. Never deploy the service as part of the group migration.
    subprocess.run(['az', 'deployment', 'sub', 'what-if', '--subscription', SUBSCRIPTION,
                    '--location', 'westeurope', '--name', 'tempo-migration-reconnect',
                    '--template-file', str(ROOT / 'infra/certificates/main.bicep'),
                    '--parameters', 'deployCompute=false'], check=True)
    result = az('deployment', 'sub', 'create', '--subscription', SUBSCRIPTION,
                '--location', 'westeurope', '--name', 'tempo-migration-reconnect',
                '--template-file', str(ROOT / 'infra/certificates/main.bicep'),
                '--parameters', 'deployCompute=false')
    if result['properties']['provisioningState'] != 'Succeeded':
        raise ValueError('Foundation reconciliation failed; retain source groups')
    verify()
    verify_roles(approved['destination_roles'])
    for name, principal in approved['identities'].items():
        identity = az('identity', 'show', '--subscription', SUBSCRIPTION,
                      '--resource-group', NEW + 'security', '--name', name)
        if identity['principalId'] != principal:
            raise ValueError('Identity changed during migration')
    for name in ENDPOINTS:
        endpoint = az('network', 'private-endpoint', 'show', '--subscription', SUBSCRIPTION,
                      '--resource-group', NEW + 'security', '--name', name)
        if endpoint['provisioningState'] != 'Succeeded' or any(
                c['privateLinkServiceConnectionState']['status'] != 'Approved'
                for c in endpoint['privateLinkServiceConnections']):
            raise ValueError('Private endpoint is not ready')
        zones = az('network', 'private-endpoint', 'dns-zone-group', 'list', '--subscription', SUBSCRIPTION,
                   '--resource-group', NEW + 'security', '--endpoint-name', name)
        if not zones or any('/' + OLD in json.dumps(z) for z in zones):
            raise ValueError('Private DNS binding was not migrated')
    # No group deletion can be reached until all preceding checks succeed.
    for concern in CONCERNS:
        group = OLD + concern
        if az('group', 'exists', '--subscription', SUBSCRIPTION, '--name', group):
            assert_empty(group)
            az('group', 'delete', '--subscription', SUBSCRIPTION, '--name', group, '--yes')
    groups = az('group', 'list', '--subscription', SUBSCRIPTION)
    if any(g['name'].startswith(OLD) for g in groups):
        raise ValueError('Source group deletion incomplete')
    (DIRECTORY / 'migration-complete.json').write_text(json.dumps({'sha': sha, 'groups': [NEW + c for c in CONCERNS]}, indent=2))
    print('PASS: migrated foundation; old Tempo groups removed; resource names preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('plan', 'apply'))
    args = parser.parse_args()
    plan() if args.operation == 'plan' else apply()
