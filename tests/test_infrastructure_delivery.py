import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/infrastructure'))
import delivery
import migrate


class DeliveryTests(unittest.TestCase):
    def test_cli_boolean_json_output(self):
        # Azure CLI -o json emits JSON booleans, which json.loads preserves.
        for output, expected in [('true\n', True), ('false\n', False), ('  true\n', True)]:
            result = delivery.subprocess.CompletedProcess([], 0, stdout=output)
            with patch.object(delivery.subprocess, 'run', return_value=result) as run:
                self.assertIs(delivery.az('group', 'exists', '--name', 'example'), expected)
                self.assertEqual(run.call_args.args[0][-2:], ['-o', 'json'])
        with patch.object(delivery.subprocess, 'run', return_value=delivery.subprocess.CompletedProcess([], 0, stdout='unexpected')):
            with self.assertRaises(json.JSONDecodeError):
                delivery.az('group', 'exists', '--name', 'example')

    def test_no_local_or_branch_cloud_write(self):
        for context in ({}, {'GITHUB_ACTIONS': 'true', 'GITHUB_REPOSITORY': 'tinyrobotcompany/Tempo',
                             'GITHUB_REF': 'refs/heads/feature/test', 'GITHUB_EVENT_NAME': 'workflow_dispatch'}):
            with patch.dict(os.environ, context, clear=True), patch.object(delivery, 'az') as azure:
                with self.assertRaises(ValueError):
                    delivery.guard()
                azure.assert_not_called()

    def test_wrong_subscription_and_stale_main_fail(self):
        env = {'GITHUB_ACTIONS': 'true', 'GITHUB_REPOSITORY': 'tinyrobotcompany/Tempo',
               'GITHUB_REF': 'refs/heads/main', 'GITHUB_EVENT_NAME': 'workflow_dispatch', 'GITHUB_SHA': 'a' * 40}
        with patch.dict(os.environ, env, clear=True), patch.object(delivery, 'az', return_value={'id': 'wrong', 'tenantId': delivery.TENANT}):
            with self.assertRaises(ValueError):
                delivery.guard()
        with patch.dict(os.environ, env, clear=True), patch.object(delivery, 'az', return_value={'id': delivery.SUBSCRIPTION, 'tenantId': delivery.TENANT}), patch.object(delivery.subprocess, 'check_output', side_effect=['a' * 40, 'b' * 40 + '\trefs/heads/main']):
            with self.assertRaises(ValueError):
                delivery.guard()

    def test_only_pinned_tempo_image_and_known_ca(self):
        valid = delivery.REGISTRY + '@sha256:' + 'a' * 64
        self.assertTrue(delivery.parameters(valid, 'staging')['deployCompute']['value'])
        self.assertFalse(delivery.parameters('', 'staging')['deployCompute']['value'])
        for image in ('evil.azurecr.io/x@sha256:' + 'a' * 64, delivery.REGISTRY + ':latest', '$(id)'):
            with self.assertRaises(ValueError):
                delivery.parameters(image, 'production')
        with self.assertRaises(ValueError):
            delivery.parameters('', 'unknown')

    def test_tampered_plan_never_reaches_azure(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            (directory / 'template.json').write_text('{}')
            (directory / 'parameters.json').write_text('{}')
            (directory / 'manifest.json').write_text(json.dumps({'sha': 'same', 'template': 'wrong', 'parameters': 'wrong'}))
            with patch.object(delivery, 'guard', return_value='same'), patch.object(delivery, 'az') as azure:
                with self.assertRaises(ValueError):
                    delivery.apply(directory)
                azure.assert_not_called()

    def test_drift_prevents_apply(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            for file in ('template.json', 'parameters.json'):
                (directory / file).write_text('{}')
            manifest = {'sha': 'same', 'template': delivery.digest(directory / 'template.json'),
                        'parameters': delivery.digest(directory / 'parameters.json'), 'changes': []}
            (directory / 'manifest.json').write_text(json.dumps(manifest))
            with patch.object(delivery, 'guard', return_value='same'), patch.object(delivery, 'az', return_value={'status': 'Succeeded', 'changes': [{'resourceId': '/new', 'changeType': 'Modify'}]}) as azure:
                with self.assertRaises(ValueError):
                    delivery.apply(directory)
                self.assertEqual(azure.call_count, 1)
                self.assertIn('what-if', azure.call_args.args)

    def test_sensitive_preview_values_are_not_published(self):
        value = {'properties': {'sharedKey': 'secret-value', 'secrets': [{'value': 'another-secret'}]}}
        value['delta'] = [{'path': 'properties.logAnalyticsConfiguration.sharedKey', 'before': 'secret-value', 'after': 'another-secret'}]
        safe = json.dumps(delivery.safe_preview(value))
        self.assertNotIn('secret-value', safe)
        self.assertNotIn('another-secret', safe)
        self.assertNotEqual(delivery.safe_preview(value), delivery.safe_preview({'properties': {'sharedKey': 'rotated'}}))

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
                snapshot = {'sha': 'same', 'layout': migrate.layout(groups), 'roles': [], 'destination_roles': [], 'identities': {}}
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
                    if args[:3] == ('network', 'private-endpoint', 'show'):
                        return {'provisioningState': 'Succeeded', 'privateLinkServiceConnections': [{'privateLinkServiceConnectionState': {'status': 'Approved'}}]}
                    if args[:4] == ('network', 'private-endpoint', 'dns-zone-group', 'list'):
                        return [{'id': '/resourceGroups/' + migrate.NEW + 'security/dns'}]
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
                        self.assertTrue((directory / 'migration-complete.json').exists())
                    else:
                        with self.assertRaises(ValueError):
                            migrate.apply()
                        self.assertFalse(any(e.startswith('delete:') for e in events))
                        self.assertFalse((directory / 'migration-complete.json').exists())

    def test_github_only_entrypoints_and_pinned_actions(self):
        workflow = (ROOT / '.github/workflows/infrastructure-deploy.yaml').read_text()
        self.assertNotIn('pull_request_target', workflow)
        self.assertIn('environment: tempo-infrastructure', workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn('cancel-in-progress: false', workflow)
        self.assertIn("needs.preview.result == 'success'", workflow)
        import re
        for action in re.findall(r'uses: ([^\s]+)', workflow):
            self.assertRegex(action, r'@[0-9a-f]{40}$')


if __name__ == '__main__':
    unittest.main()
