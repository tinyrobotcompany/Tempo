import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/infrastructure'))
import migrate

class MigrationTests(unittest.TestCase):
    def test_push_cannot_migrate(self):
        with patch.dict(os.environ, {'GITHUB_EVENT_NAME':'push'}), patch.object(migrate, 'delivery_guard') as guard:
            with self.assertRaises(ValueError): migrate.guard()
            guard.assert_not_called()
        with patch.dict(os.environ, {'GITHUB_EVENT_NAME':'workflow_dispatch'}), patch.object(migrate, 'delivery_guard', return_value='head'):
            self.assertEqual(migrate.guard(), 'head')
        workflow=(ROOT/'.github/workflows/infrastructure-migrate.yaml').read_text()
        self.assertIn('  workflow_dispatch:',workflow)
        self.assertNotIn('  push:',workflow)
        self.assertEqual(workflow.count('environment: tempo-infrastructure'),2)

    def test_migration_refuses_unknown_resources_and_nonempty_groups(self):
        unknown = {'type': 'Microsoft.Compute/virtualMachines', 'name': 'customer-vm', 'resourceGroup': migrate.OLD + 'compute'}
        self.assertFalse(migrate.known(unknown, 'compute'))
        with patch.object(migrate, 'az', return_value=[unknown]) as azure:
            with self.assertRaises(ValueError):
                migrate.assert_empty(migrate.OLD + 'compute')
            self.assertEqual(azure.call_count, 1)
        with patch.object(migrate, 'az') as azure:
            with self.assertRaises(ValueError):
                migrate.assert_empty('rg-another-application')
            azure.assert_not_called()

    def test_duplicate_or_missing_resources_abort_inventory(self):
        with patch.object(migrate, 'az', side_effect=lambda *args: True if args[:2] == ('group', 'exists') else []):
            with self.assertRaises(ValueError):
                migrate.inventory()

    def test_endpoint_and_managed_nic_deletion_is_confirmed(self):
        items = {migrate.OLD + 'security': [
            {'type': 'Microsoft.Network/privateEndpoints', 'name': 'tempo-table-endpoint', 'id': '/endpoint'},
            {'type': 'Microsoft.Network/networkInterfaces', 'name': 'managed-nic', 'id': '/nic'}]}
        with tempfile.TemporaryDirectory() as name, patch.object(migrate, 'DIRECTORY', Path(name)), patch.object(migrate, 'az') as azure:
            migrate.detach_endpoints(items)
            self.assertTrue((Path(name) / 'migration-progress.json').exists())
            calls = [call.args for call in azure.call_args_list]
            self.assertEqual(calls[0][:3], ('network', 'private-endpoint', 'delete'))
            self.assertEqual(calls[1][:3], ('network', 'private-endpoint', 'wait'))
            self.assertEqual(calls[2][:2], ('resource', 'wait'))
            for call in calls[1:]:
                self.assertIn('--deleted', call)
                self.assertIn('--timeout', call)
            azure.reset_mock()
            azure.side_effect = [None, RuntimeError('deletion timeout')]
            with self.assertRaisesRegex(RuntimeError, 'deletion timeout'):
                migrate.detach_endpoints(items)
            self.assertEqual(azure.call_count, 2)

    def test_source_roles_cannot_satisfy_destination_verification(self):
        source = {'scope': '/subscriptions/id/resourceGroups/RG-TEMPO-STAGING-WESTEUROPE-DATA/providers/type/name',
                  'principalId': 'principal', 'roleDefinitionId': '/role'}
        expected = migrate.expected_destination_roles([source])
        self.assertIn('/resourcegroups/rg-tempo-westeurope-data/', expected[0]['scope'])
        with patch.object(migrate, 'resource_roles', return_value=[]) as roles:
            with self.assertRaises(ValueError):
                migrate.verify_roles(expected)
            roles.assert_called_once_with(destination_only=True)
        with patch.object(migrate, 'resource_roles', return_value=expected):
            migrate.verify_roles(expected)

    def test_mixed_case_roles_match_actual_destination_inventory(self):
        source = {'scope': '/SUBSCRIPTIONS/ID/RESOURCEGROUPS/RG-TEMPO-STAGING-WESTEUROPE-DATA/PROVIDERS/TYPE/NAME',
                  'principalId': 'ABC-DEF', 'roleDefinitionId': '/SUBSCRIPTIONS/ID/PROVIDERS/MICROSOFT.AUTHORIZATION/ROLEDEFINITIONS/ABC'}
        expected = migrate.expected_destination_roles([source])
        actual = {key: value.lower() for key, value in source.items()}
        actual['scope'] = '/subscriptions/id/resourcegroups/rg-tempo-westeurope-data/providers/type/name'
        self.assertEqual(expected, [actual])
        with patch.object(migrate, 'az', return_value=[actual]):
            migrate.verify_roles(expected)

    def test_real_plan_records_runtime_and_destination_delivery_identities(self):
        groups = {migrate.OLD + 'security': [], migrate.NEW + 'security': []}
        expected = {}
        for name in migrate.PRESERVED_IDENTITIES:
            group = (migrate.NEW if name == 'tempo-infrastructure-delivery' else migrate.OLD) + 'security'
            groups[group].append({'type': 'Microsoft.ManagedIdentity/userAssignedIdentities', 'name': name, 'id': name})
            expected[name] = name + '-principal'
        roles = [{'scope': '/resourcegroups/' + migrate.OLD + 'data/providers/type/' + str(i), 'principalId': 'principal', 'roleDefinitionId': '/role'} for i in range(11)]
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            with patch.object(migrate, 'DIRECTORY', directory), patch.object(migrate, 'guard', return_value='same'), patch.object(migrate, 'inventory', return_value=groups), patch.object(migrate, 'resource_roles', return_value=roles), patch.dict(os.environ, {'GITHUB_STEP_SUMMARY': str(directory / 'summary')}), patch.object(migrate, 'az', side_effect=lambda *args: {'properties': {'principalId': args[-1] + '-principal'}}):
                migrate.plan()
                manifest = json.loads((directory / 'migration.json').read_text())
                self.assertEqual(manifest['identities'], expected)
                groups[migrate.NEW + 'security'].clear()
                with self.assertRaisesRegex(ValueError, 'destination delivery identity'):
                    migrate.plan()

    def test_migration_deletes_groups_only_after_successful_reconciliation(self):
        for reconcile_succeeds in (False, True):
            with self.subTest(reconcile_succeeds=reconcile_succeeds), tempfile.TemporaryDirectory() as name:
                directory = Path(name)
                groups = {prefix + c: [] for prefix in (migrate.OLD, migrate.NEW) for c in migrate.CONCERNS}
                for concern, resources in migrate.EXPECTED.items():
                    for kind, resource_name in resources:
                        group = migrate.OLD + concern
                        groups[group].append({'type': kind, 'name': resource_name, 'resourceGroup': group,
                                              'id': '/subscriptions/id/resourceGroups/' + group + '/providers/' + kind + '/' + resource_name})
                snapshot = {'sha': 'same', 'layout': migrate.layout(groups), 'roles': [], 'destination_roles': [], 'identities': {name: name + '-principal' for name in ['tempo-staging-api', 'tempo-staging-worker', 'tempo-staging-factory', 'tempo-staging-bootstrap', 'tempo-infrastructure-delivery']}}
                groups[migrate.NEW + 'security'].append({'type': 'Microsoft.ManagedIdentity/userAssignedIdentities', 'name': 'tempo-infrastructure-delivery', 'id': '/delivery'})
                snapshot['layout'] = migrate.layout(groups)
                (directory / 'migration.json').write_text(json.dumps(snapshot))
                events = []

                def azure(*args):
                    verb = args[:2]
                    def value(flag):
                        return args[args.index(flag) + 1]
                    if verb == ('group', 'exists'):
                        return value('--name') in groups
                    if verb == ('resource', 'list'):
                        return list(groups[value('--resource-group')])
                    if verb == ('resource', 'move'):
                        destination = value('--destination-group')
                        for identifier in args[args.index('--ids') + 1:]:
                            for source, rows in list(groups.items()):
                                found = next((r for r in rows if r['id'] == identifier), None)
                                if found:
                                    rows.remove(found)
                                    found = dict(found, id=identifier.replace(source, destination), resourceGroup=destination)
                                    groups[destination].append(found)
                                    break
                        events.append('move')
                        return None
                    if args[:3] == ('deployment', 'sub', 'create'):
                        events.append('reconcile')
                        return {'properties': {'provisioningState': 'Succeeded' if reconcile_succeeds else 'Failed'}}
                    if args[:3] == ('network', 'private-endpoint', 'create') or args[:4] == ('network', 'private-endpoint', 'dns-zone-group', 'create'):
                        events.append('reconnect')
                        return {}
                    if args[:3] == ('network', 'private-endpoint', 'show'):
                        return {'provisioningState': 'Succeeded', 'privateLinkServiceConnections': [{'privateLinkServiceConnectionState': {'status': 'Approved'}}]}
                    if args[:4] == ('network', 'private-endpoint', 'dns-zone-group', 'list'):
                        return [{'id': '/resourceGroups/' + migrate.NEW + 'security/dns'}]
                    if verb == ('identity', 'show'):
                        self.assertEqual(value('--resource-group'), migrate.NEW + 'security')
                        events.append('identity:' + value('--name'))
                        return {'principalId': value('--name') + '-principal'}
                    if verb == ('group', 'delete'):
                        group = value('--name')
                        self.assertFalse(groups[group])
                        self.assertIn('verified', events)
                        events.append('delete:' + group)
                        del groups[group]
                        return None
                    if verb == ('group', 'list'):
                        return [{'name': group} for group in groups]
                    self.fail('Unexpected Azure operation: ' + str(args))

                with patch.object(migrate, 'DIRECTORY', directory), patch.object(migrate, 'guard', return_value='same'), patch.object(migrate, 'inventory', return_value=groups), patch.object(migrate, 'resource_roles', return_value=[]), patch.object(migrate, 'az', side_effect=azure), patch.object(migrate, 'verify', side_effect=lambda: events.append('verified')), patch.object(migrate.subprocess, 'run'):
                    if reconcile_succeeds:
                        migrate.apply()
                        self.assertEqual(sum(e.startswith('delete:') for e in events), 5)
                        self.assertEqual(sum(e.startswith('identity:') for e in events), 5)
                        self.assertIn('identity:tempo-infrastructure-delivery', events)
                        self.assertTrue((directory / 'migration-complete.json').exists())
                    else:
                        with self.assertRaises(ValueError):
                            migrate.apply()
                        self.assertFalse(any(e.startswith('delete:') for e in events))
                        self.assertIn('reconnect', events)
                        self.assertFalse((directory / 'migration-complete.json').exists())


    def run_with_endpoint_failure(self, fail):
        """Run apply() against a stateful fake Azure where `fail(args, groups)` may raise."""
        directory = Path(self.enterContext(tempfile.TemporaryDirectory()))
        groups = {prefix + c: [] for prefix in (migrate.OLD, migrate.NEW) for c in migrate.CONCERNS}
        def add(group, kind, name):
            groups[group].append({'type': kind, 'name': name, 'resourceGroup': group,
                                  'id': '/subscriptions/id/resourceGroups/' + group + '/providers/' + kind + '/' + name})
        for concern, resources in migrate.EXPECTED.items():
            for kind, name in resources:
                add(migrate.OLD + concern, kind, name)
        for name in sorted(migrate.ENDPOINTS):
            add(migrate.OLD + 'security', 'Microsoft.Network/privateEndpoints', name)
        add(migrate.NEW + 'security', 'Microsoft.ManagedIdentity/userAssignedIdentities', 'tempo-infrastructure-delivery')
        identities = {name: name + '-principal' for name in migrate.PRESERVED_IDENTITIES}
        (directory / 'migration.json').write_text(json.dumps({'sha': 'same', 'layout': migrate.layout(groups), 'roles': [],
                                                              'destination_roles': [], 'identities': identities}))
        events = []
        def azure(*args):
            def value(flag):
                return args[args.index(flag) + 1]
            fail(args, groups)
            if args[:2] == ('group', 'exists'):
                return value('--name') in groups
            if args[:2] == ('resource', 'list'):
                return list(groups[value('--resource-group')])
            if args[:3] == ('network', 'private-endpoint', 'delete'):
                rows = groups[value('--resource-group')]
                rows.remove(next(r for r in rows if r['name'] == value('--name')))
                events.append('detach:' + value('--name'))
                return None
            if args[:3] == ('network', 'private-endpoint', 'wait') or args[:2] == ('resource', 'wait'):
                return None
            if args[:2] == ('resource', 'move'):
                destination = value('--destination-group')
                for identifier in args[args.index('--ids') + 1:]:
                    for source, rows in list(groups.items()):
                        found = next((r for r in rows if r['id'] == identifier), None)
                        if found:
                            rows.remove(found)
                            groups[destination].append(dict(found, id=identifier.replace(source, destination), resourceGroup=destination))
                return None
            if args[:3] == ('network', 'private-endpoint', 'create'):
                add(value('--resource-group'), 'Microsoft.Network/privateEndpoints', value('--name'))
                events.append(('create', value('--resource-group'), value('--name'), value('--subnet'),
                               value('--private-connection-resource-id'), value('--group-id')))
                return {}
            if args[:4] == ('network', 'private-endpoint', 'dns-zone-group', 'create'):
                events.append(('zone', value('--endpoint-name'), value('--private-dns-zone')))
                return {}
            if args[:3] == ('deployment', 'sub', 'create'):
                return {'properties': {'provisioningState': 'Succeeded'}}
            if args[:2] == ('group', 'delete'):
                events.append('delete:' + value('--name'))
                return None
            self.fail('Unexpected Azure operation: ' + str(args))
        with patch.object(migrate, 'DIRECTORY', directory), patch.object(migrate, 'guard', return_value='same'), \
                patch.object(migrate, 'inventory', side_effect=lambda: {g: list(rows) for g, rows in groups.items()}), \
                patch.object(migrate, 'resource_roles', return_value=[]), \
                patch.object(migrate, 'az', side_effect=azure), patch.object(migrate.subprocess, 'run'), \
                patch.object(migrate, 'verify', side_effect=RuntimeError('verification failed')):
            with self.assertRaises(Exception) as raised:
                migrate.apply()
        self.assertFalse(any(isinstance(e, str) and e.startswith('delete:') for e in events), 'source groups must be retained')
        record = json.loads((directory / 'migration-recovery.json').read_text())
        return raised.exception, record, events, groups

    def test_failed_move_after_detach_recreates_endpoints_at_current_locations(self):
        def fail(args, groups):
            if args[:2] == ('resource', 'move') and args[args.index('--destination-group') + 1] == migrate.NEW + 'security':
                raise RuntimeError('move rejected')
        error, record, events, groups = self.run_with_endpoint_failure(fail)
        self.assertEqual(str(error), 'move rejected')  # The original failure is reported, not hidden.
        self.assertEqual(record['recovery'], 'reconnected')
        created = {e[2]: e for e in events if e[0] == 'create'}
        self.assertEqual(set(created), migrate.ENDPOINTS)
        table, vault = created['tempo-table-endpoint'], created['tempo-vault-endpoint']
        # Data and network moved before the failure; the vault did not.
        self.assertIn('/resourceGroups/' + migrate.NEW + 'data/', table[4])
        self.assertIn('/resourceGroups/' + migrate.OLD + 'security/', vault[4])
        self.assertTrue(table[3].startswith('/subscriptions/id/resourceGroups/' + migrate.NEW + 'network/') and table[3].endswith('/subnets/private'))
        self.assertEqual((table[1], table[5], vault[5]), (migrate.OLD + 'security', 'table', 'vault'))
        zones = {e[1]: e[2] for e in events if e[0] == 'zone'}
        self.assertTrue(zones['tempo-table-endpoint'].endswith('/privatelink.table.core.windows.net'))
        self.assertTrue(zones['tempo-vault-endpoint'].endswith('/privatelink.vaultcore.azure.net'))
        # The recreated endpoints sit where the next reviewed preview accepts them.
        for row in groups[migrate.OLD + 'security']:
            self.assertTrue(migrate.known(row, 'security'))

    def test_partial_detach_recreates_only_the_deleted_endpoint(self):
        def fail(args, groups):
            if args[:3] == ('network', 'private-endpoint', 'delete') and args[args.index('--name') + 1] == 'tempo-vault-endpoint':
                raise RuntimeError('delete timeout')
        error, record, events, _ = self.run_with_endpoint_failure(fail)
        self.assertEqual(str(error), 'delete timeout')
        self.assertEqual(record['recreated'], [{'endpoint': 'tempo-table-endpoint', 'resourceGroup': migrate.OLD + 'security'}])

    def test_failure_after_reconciliation_leaves_reconciled_endpoints_alone(self):
        def fail(args, groups):
            if args[:3] == ('deployment', 'sub', 'create'):
                for name in migrate.ENDPOINTS:
                    groups[migrate.NEW + 'security'].append({'type': 'Microsoft.Network/privateEndpoints', 'name': name,
                                                             'resourceGroup': migrate.NEW + 'security', 'id': '/reconciled/' + name})
        error, record, events, _ = self.run_with_endpoint_failure(fail)
        self.assertEqual(str(error), 'verification failed')
        self.assertEqual((record['recovery'], record['recreated']), ('reconnected', []))
        self.assertFalse(any(e[0] == 'create' for e in events if isinstance(e, tuple)))

    def test_failed_recovery_still_reports_the_original_failure(self):
        def fail(args, groups):
            if args[:2] == ('resource', 'move'):
                raise RuntimeError('move rejected')
            if args[:3] == ('network', 'private-endpoint', 'create'):
                raise RuntimeError('create failed')
        error, record, _, _ = self.run_with_endpoint_failure(fail)
        self.assertEqual(str(error), 'move rejected')
        self.assertEqual(record['recovery'], 'failed')
        self.assertIn('create failed', record['recovery_error'])


if __name__=='__main__': unittest.main()
