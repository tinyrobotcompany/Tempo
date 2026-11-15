import contextlib
import importlib.util
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('transport_probe', Path(__file__).parents[1] / 'scripts/check-transport-services.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class PreflightTests(unittest.TestCase):
    def test_platform_mapping_rejects_ambiguous_ids(self):
        data = {'places_nearby': [
            {'embedded_type': 'stop_point', 'stop_point': {'id': 'stop_point:IDFM:123', 'name': 'École'}},
            {'embedded_type': 'stop_point', 'stop_point': {'id': 'stop_point:IDFM:123'}},
            {'embedded_type': 'stop_point', 'stop_point': {'id': 'stop_point:IDFM:monomodalStopPlace:456'}},
            {'embedded_type': 'stop_area', 'stop_area': {'id': 'stop_area:IDFM:789'}},
        ]}
        result = probe.nearby_stops(data)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['monitoring_ref'], 'STIF:StopPoint:Q:123:')
        self.assertEqual(result[0]['name'], 'École')

    def test_provider_error_is_not_mistaken_for_empty_service(self):
        result = probe.siri_summary({'Siri': {'ServiceDelivery': {'StopMonitoringDelivery': [
            {'Status': False, 'ErrorCondition': {}}, {'MonitoredStopVisit': [{}]}]}}})
        self.assertEqual(result, {'delivery_count': 2, 'visits': 1, 'provider_errors': 1})

    def test_key_loading_parses_in_memory_configuration(self):
        contents = '# comment\nOTHER=value\nexport IDFM_PRIM_API_KEY="test-only-placeholder"\n'
        with patch.object(Path, 'read_text', autospec=True, return_value=contents) as read:
            self.assertEqual(probe.load_key(), 'test-only-placeholder')
        read.assert_called_once_with(probe.ROOT / '.env')

    def test_key_loading_rejects_missing_empty_duplicate_and_whitespace_values(self):
        for contents in ['', 'IDFM_PRIM_API_KEY=',
                         'IDFM_PRIM_API_KEY=one\nIDFM_PRIM_API_KEY=two',
                         'IDFM_PRIM_API_KEY=contains whitespace']:
            with self.subTest(contents=contents), patch.object(Path, 'read_text', return_value=contents):
                with self.assertRaises(ValueError):
                    probe.load_key()

    def test_successful_capture_is_filtered_bounded_and_redacted(self):
        secret = 'test-secret-do-not-print'
        nearby = {'places_nearby': [{'embedded_type': 'stop_point', 'stop_point': {
            'id': 'stop_point:IDFM:123', 'lines': [{'id': 'line:IDFM:C00123'}]}}], 'apikey': secret}
        replies = [nearby, {'Siri': {'ServiceDelivery': {'StopMonitoringDelivery': [
            {'Status': True, 'MonitoredStopVisit': [{}]}]}}},
            {'line_reports': [{'line': {'id': 'line:IDFM:C00123'}}], 'disruptions': [], 'diagnostic': secret}]
        requests = []
        class Response(io.BytesIO):
            status = 200
        class Opener:
            def open(self, request, timeout):
                requests.append(request)
                return Response(json.dumps(replies.pop(0)).encode())
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            # Keep credentials in memory; key parsing is covered independently above.
            with patch.object(probe, 'load_key', return_value=secret), patch.object(probe, 'ROOT', root), patch.object(probe.urllib.request, 'build_opener', return_value=Opener()), patch('sys.argv', ['probe', '--latitude', '48.8', '--longitude', '2.2']):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    self.assertEqual(probe.main(), 0)
            self.assertNotIn(secret, output.getvalue())
            self.assertFalse((root / '.env').exists())
            captures = list((root / 'build-host/transport-preflight').glob('*.json'))
            self.assertEqual({path.name for path in captures},
                             {'nearby.json', 'departures-1.json', 'traffic-compact.json', 'report.json'})
            for path in captures:
                self.assertNotIn(secret, path.read_text())
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(len(requests), 3)
        self.assertIn('disable_geojson=true', requests[0].full_url)
        self.assertIn('disable_disruption=true', requests[0].full_url)
        self.assertIn('STIF%3AStopPoint%3AQ%3A123%3A', requests[1].full_url)
        self.assertIn('line%3AIDFM%3AC00123', requests[2].full_url)
        self.assertIn('depth=0', requests[2].full_url)
        self.assertTrue(all(request.get_header('Apikey') == secret for request in requests))


if __name__ == '__main__':
    unittest.main()
