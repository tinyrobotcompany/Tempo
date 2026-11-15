import importlib.util
import json
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('logs',Path(__file__).resolve().parents[1]/'scripts/summarize-log.py')
logs=importlib.util.module_from_spec(spec);spec.loader.exec_module(logs)
class LogTests(unittest.TestCase):
    def test_reset_failure_latency_and_no_sensitive_lines(self):
        text='''App version: 0.8.16
station: Reset reason: 3
station: UI stopped responding; restart
network_time: Wi-Fi disconnected: reason=201
station: Display health: heartbeat=20, submitted_frames=20, uptime=60 s
station: Internal memory: free=123, largest=100, minimum=90
weather: Fetch complete: HTTP 503, 0 bytes, FAIL, reason=http, elapsed=12000 ms, generation=4
transport: Departures HTTP 200, 400 bytes, complete=1, oversized=0, elapsed=30 ms
password=SECRET token=PRIVATE http://192.168.1.2 latitude=48.7
'''
        data=logs.summarize(text)
        self.assertEqual(data['ui_watchdog_restarts'],1)
        self.assertEqual(data['weather_requests'][0]['elapsed_ms'],12000)
        self.assertTrue(data['transport_requests'][0]['complete'])
        self.assertEqual(data['last_internal_memory']['minimum'],90)
        for secret in ('SECRET','PRIVATE','192.168','48.7'):
            self.assertNotIn(secret,json.dumps(data))
    def test_empty_capture_does_not_claim_health(self):
        data=logs.summarize('')
        self.assertIsNone(data['last_display_health'])
        self.assertIn('do not prove health',data['limitations'])
if __name__=='__main__':unittest.main()
