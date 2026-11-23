import json
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from main import create_server, SimulationEngine


class WebServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.server = create_server(port=0, data_dir=cls.temp.name)
        cls.server.engine.start_worker()
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = 'http://127.0.0.1:' + str(cls.server.server_port)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.server.engine.shutdown()
        cls.thread.join(timeout=1)
        cls.temp.cleanup()

    def get(self, path):
        with urlopen(self.base + path, timeout=5) as resp:
            return resp.status, resp.headers, resp.read()

    def post(self, body, header=True):
        headers = {'Content-Type': 'application/json'}
        if header:
            headers['X-DriveTrace-Client'] = 'dashboard'
        req = Request(self.base + '/api/action', data=json.dumps(body).encode(),
                      headers=headers, method='POST')
        with urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())

    def test_static_site_and_health(self):
        for path in ('/', '/static/app.js', '/static/style.css', '/static/favicon.svg'):
            status, _, content = self.get(path)
            self.assertEqual(status, 200)
            self.assertGreater(len(content), 100)
        _, _, health = self.get('/api/health')
        self.assertEqual(json.loads(health)['mode'], 'SIMULATION')

    def test_simulated_stop_export_and_verification(self):
        self.post({'action':'reset'})
        self.post({'action':'set_limit','value':50})
        for _ in range(6):
            self.post({'action':'accelerate'})
        self.post({'action':'brake'})
        self.post({'action':'stop'})
        _, _, content = self.get('/api/state')
        state = json.loads(content)
        self.assertEqual(state['speed_kmh'], 0)
        self.assertEqual(state['latest_event']['speed_before_stop_kmh'], 50)
        self.assertEqual(state['latest_event']['maximum_recent_speed_kmh'], 60)
        self.assertTrue(state['latest_event']['overspeed_detected'])
        self.assertTrue(state['latest_integrity'])
        _, _, content = self.get('/api/events')
        stop = next(v for v in json.loads(content) if v['type'] == 'STOP')
        self.assertEqual(stop['integrity'], 'PASS')
        _, headers, content = self.get('/api/event/' + stop['filename'])
        self.assertIn('attachment', headers['Content-Disposition'])
        self.assertEqual(json.loads(content)['payload']['event_type'],'STOP')
        self.assertTrue(self.post({'action':'verify_latest'})['verified'])


    def test_hardware_telemetry_ingest(self):
        engine = SimulationEngine(self.temp.name)
        result = engine.ingest_hardware({
            "type": "telemetry", "speed_kmh": 64.5, "wheel_pulses": 12,
            "imu_valid": True, "imu_ax_g": 0.01
        })
        self.assertTrue(result["ok"])
        state = engine.state()
        self.assertEqual(state["speed_kmh"], 64.5)
        self.assertTrue(state["hardware_connected"])
        self.assertEqual(state["mode"], "HARDWARE")
        engine.ingest_hardware({"type": "telemetry", "speed_kmh": 0})
        event = engine.recorder.last_event
        self.assertEqual(event["payload"]["source"], "HARDWARE")
        self.assertTrue(engine.recorder.verify_file(engine.recorder.last_event_path))

    def test_tamper_and_reset_keeps_history(self):
        self.post({'action':'tamper'})
        _,_,content = self.get('/api/events')
        count_before = len(json.loads(content))
        self.post({'action':'reset'})
        _,_,content = self.get('/api/events')
        self.assertEqual(len(json.loads(content)),count_before)
        self.assertTrue(any(x['type']=='TAMPER' for x in json.loads(content)))

    def test_bad_limit_and_cross_site_header(self):
        for val in (-1, 0, 201, True, 'abc', None):
            with self.assertRaises(HTTPError) as cm:
                self.post({'action':'set_limit','value':val})
            self.assertEqual(cm.exception.code,400)
        with self.assertRaises(HTTPError) as cm:
            self.post({'action':'start'},header=False)
        self.assertEqual(cm.exception.code,403)
        with self.assertRaises(HTTPError) as cm:
            self.get('/api/event/../../data/secret.key')
        self.assertEqual(cm.exception.code,404)


if __name__ == '__main__':
    unittest.main()
