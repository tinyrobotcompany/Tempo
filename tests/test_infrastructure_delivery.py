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


class DeliveryTests(unittest.TestCase):
    def test_federation_matches_observed_immutable_github_subject(self):
        # Regression fixture: repository OIDC settings and the rejected live token
        # in run 45629123208. Name-only subjects cannot authenticate this repo.
        observed = {'use_immutable_subject': True,
                    'sub_claim_prefix': 'repo:tinyrobotcompany@326183923/Tempo@1458582269'}
        template = (ROOT / 'infra/certificates/delivery-identity.bicep').read_text()
        subject = observed['sub_claim_prefix'] + ':environment:tempo-infrastructure'
        self.assertIn("subject: '" + subject + "'", template)
        self.assertNotIn("subject: 'repo:tinyrobotcompany/Tempo:", template)
        self.assertIn("issuer: 'https://token.actions.githubusercontent.com'", template)
        self.assertIn("audiences: ['api://AzureADTokenExchange']", template)

    def test_normal_plan_requires_migration_before_compilation(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(delivery, 'guard', return_value='same'), patch.object(delivery, 'az', return_value=True) as azure, patch.object(delivery.subprocess, 'run') as process:
                with self.assertRaisesRegex(ValueError, 'group migration'):
                    delivery.plan(Path(directory), '', 'staging')
                process.assert_not_called()
                self.assertEqual(azure.call_args.args[:2], ('group', 'exists'))
        with patch.object(delivery, 'az', return_value=False) as azure:
            delivery.require_migrated()
            self.assertEqual(azure.call_count, 5)

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

    def test_main_push_and_dispatch_allowed_but_pr_context_rejected(self):
        env = {'GITHUB_ACTIONS':'true', 'GITHUB_REPOSITORY':'tinyrobotcompany/Tempo',
               'GITHUB_REF':'refs/heads/main', 'GITHUB_SHA':'a'*40}
        for event in ('push', 'workflow_dispatch', 'pull_request', 'pull_request_target'):
            with patch.dict(os.environ, dict(env, GITHUB_EVENT_NAME=event), clear=True), patch.object(delivery, 'az', return_value={'id':delivery.SUBSCRIPTION,'tenantId':delivery.TENANT}) as azure, patch.object(delivery.subprocess, 'check_output', side_effect=['a'*40, 'a'*40+'\trefs/heads/main']):
                if event in ('push', 'workflow_dispatch'):
                    self.assertEqual(delivery.guard(), 'a'*40)
                else:
                    with self.assertRaises(ValueError): delivery.guard()
                    azure.assert_not_called()

    def test_automatic_deployment_uses_reviewed_config(self):
        config = json.loads((ROOT/'infra/certificates/deployment.json').read_text())
        with patch.object(sys, 'argv', ['delivery.py', 'plan']), patch.object(delivery, 'plan') as plan:
            delivery.main()
            self.assertEqual(plan.call_args.args[1:], (config['image'], config['acme']))
        workflow = (ROOT/'.github/workflows/infrastructure-deploy.yaml').read_text()
        self.assertIn('  push:\n    branches: [main]\n    paths:', workflow)
        self.assertIn("github.event_name == 'push'", workflow)
        self.assertNotIn('workflow_dispatch:', workflow)
        self.assertNotIn('inputs.operation', workflow)
        self.assertIn("      - 'infra/certificates/**/*.bicep'", workflow)
        self.assertIn('infrastructure-changes.py --deployment', workflow)
        self.assertIn('needs: validate', workflow)
        self.assertEqual(workflow.count('environment: tempo-infrastructure'), 2)
        self.assertNotIn('migrate.py', workflow)

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
            with patch.object(delivery, 'require_migrated'), patch.object(delivery, 'guard', return_value='same'), patch.object(delivery, 'az', return_value={'status': 'Succeeded', 'changes': [{'resourceId': '/new', 'changeType': 'Modify'}]}) as azure:
                with self.assertRaises(ValueError):
                    delivery.apply(directory)
                self.assertEqual(azure.call_count, 1)
                self.assertIn('what-if', azure.call_args.args)

    def test_old_group_blocks_apply_before_what_if(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            for file in ('template.json', 'parameters.json'):
                (directory / file).write_text('{}')
            manifest = {'sha': 'same', 'template': delivery.digest(directory / 'template.json'),
                        'parameters': delivery.digest(directory / 'parameters.json'), 'changes': []}
            (directory / 'manifest.json').write_text(json.dumps(manifest))
            with patch.object(delivery, 'guard', return_value='same'), patch.object(delivery, 'az', return_value=True) as azure:
                with self.assertRaisesRegex(ValueError, 'group migration'):
                    delivery.apply(directory)
                self.assertEqual(azure.call_count, 1)
                self.assertEqual(azure.call_args.args[:2], ('group', 'exists'))

    def test_delete_and_malformed_azure_previews_are_rejected(self):
        for kind in ('Delete', 'delete', 'DELETE'):
            with self.assertRaisesRegex(ValueError, 'cannot delete'):
                delivery.changes({'status': 'Succeeded', 'changes': [{'resourceId': '/resource', 'changeType': kind}]})
        for entries in (None, {}, [None], [{}], [{'resourceId': '/resource'}]):
            with self.assertRaises(ValueError):
                delivery.changes({'status': 'Succeeded', 'changes': entries})

    def test_sensitive_preview_values_are_not_published(self):
        value = {'properties': {'sharedKey': 'secret-value', 'secrets': [{'value': 'another-secret'}]}}
        value['delta'] = [{'path': 'properties.logAnalyticsConfiguration.sharedKey', 'before': 'secret-value', 'after': 'another-secret'}]
        safe = json.dumps(delivery.safe_preview(value))
        self.assertNotIn('secret-value', safe)
        self.assertNotIn('another-secret', safe)
        self.assertNotEqual(delivery.safe_preview(value), delivery.safe_preview({'properties': {'sharedKey': 'rotated'}}))

    def test_github_only_entrypoints_and_pinned_actions(self):
        workflow = (ROOT / '.github/workflows/infrastructure-deploy.yaml').read_text()
        self.assertNotIn('pull_request_target', workflow)
        self.assertIn('environment: tempo-infrastructure', workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn('cancel-in-progress: false', workflow)
        self.assertIn("needs.preview.result == 'success'", workflow)
        import re
        for action in re.findall(r'uses: ([^\s]+)', workflow):
            if not action.startswith('./'):
                self.assertRegex(action, r'@[0-9a-f]{40}$')


if __name__ == '__main__':
    unittest.main()
