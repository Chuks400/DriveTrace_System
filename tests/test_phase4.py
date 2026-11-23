import csv
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from journey_store import JourneyStore, read_csv, compute_summary
from main import SimulationEngine, create_server

SAMPLE = Path(__file__).resolve().parents[1] / 'samples' / 'teacher_ride_sdcard.csv'


class JourneyStoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.store = JourneyStore(Path(self.tmp.name) / 'rides', b'Z' * 32)

    def tearDown(self):
        self.store.finish()
        self.tmp.cleanup()

    def test_full_ride_saved_and_integrity(self):
        t = datetime(2026, 11, 23, 10, tzinfo=timezone.utc).timestamp()
        for i, speed in enumerate((12, 42, 70, 55, 20, 0)):
            self.store.record(speed, timestamp=t+i, limit=50)
        saved=self.store.list()
        self.assertEqual(len(saved),1)
        self.assertEqual(saved[0]['peak_kmh'], 70)
        self.assertEqual(saved[0]['samples'],6)
        self.assertEqual(saved[0]['overspeed_sample_count'],2)
        self.assertEqual(saved[0]['verification'],'VERIFIED_LOCAL')
        self.assertGreater(saved[0]['distance_km_estimate'],0)
        self.assertEqual(len(self.store.detail(saved[0]['id'])['samples']),6)

    def test_tampering_invalidates_local_record(self):
        t=datetime(2026,11,23,tzinfo=timezone.utc).timestamp()
        self.store.record(30,timestamp=t)
        self.store.record(0,timestamp=t+2)
        id=self.store.list()[0]['id']
        path=self.store.csv_path(id)
        path.write_text(path.read_text().replace('30.00','80.00'))
        self.assertEqual(self.store.list()[0]['verification'],'CHANGED_OR_KEY_MISSING')

    def test_import_full_microSD_trip(self):
        meta=self.store.import_csv(SAMPLE.read_bytes(),limit=50)
        self.assertEqual(meta['origin'],'MICROSD_IMPORT')
        self.assertEqual(meta['samples'],151)
        self.assertGreaterEqual(meta['peak_kmh'],70)
        self.assertGreater(meta['distance_km_estimate'],1.0)
        self.assertEqual(self.store.list()[0]['verification'],'UNVERIFIED_IMPORT')

    def test_reject_spreadsheet_injection_and_bad_format(self):
        for content in [b'timestamp_utc,speed_kmh\n=SUM(1,2),5\n',
                        b'timestamp_utc,speed_kmh\n2026-11-23T12:00:00+00:00,NaN\n',
                        b'timestamp_utc,speed_kmh\n2026-11-23T12:00:00+00:00,-1\n',
                        b'timestamp_utc,speed_kmh\n2026-11-23T12:00:00+00:00,20\n2026-11-23T11:00:00+00:00,20\n',
                        b'hello,world\nxx,xx\n']:
            with self.subTest(content=content), self.assertRaises(ValueError):
                self.store.import_csv(content)

    def test_preserve_records_between_restarts(self):
        t=datetime(2026,11,23,tzinfo=timezone.utc).timestamp()
        self.store.record(65,t)
        self.store.record(0,t+2)
        second=JourneyStore(Path(self.tmp.name)/'rides', b'Z'*32)
        self.assertEqual(second.list()[0]['verification'],'VERIFIED_LOCAL')

    def test_safe_ride_path(self):
        with self.assertRaises(ValueError):
            self.store.csv_path('../../secrets')


class WholeStackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        cls.server=create_server(port=0,data_dir=cls.tmp.name)
        cls.server.engine.start_worker()
        cls.thread=threading.Thread(target=cls.server.serve_forever,daemon=True)
        cls.thread.start()
        cls.base=f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown();cls.server.server_close();cls.server.engine.shutdown()
        cls.thread.join(2);cls.tmp.cleanup()

    def post(self,path,data=None,ctype=None,client=True):
        headers={'X-DriveTrace-Client':'dashboard'} if client else {}
        if ctype:headers['Content-Type']=ctype
        request=Request(self.base+path,data=data,method='POST',headers=headers)
        with urlopen(request,timeout=5) as res:
            return json.loads(res.read())

    def get(self,path):
        with urlopen(self.base+path,timeout=5) as res:
            return json.loads(res.read())

    def test_teacher_demo_one_click(self):
        data=self.post('/api/journeys/demo',data=b'')
        self.assertTrue(data['ok'])
        self.assertEqual(data['journey']['samples'],151)
        results=self.get('/api/journeys')
        self.assertTrue(any(j['origin']=='MICROSD_IMPORT' for j in results))
        result=self.get('/api/journey/'+data['journey']['id'])
        self.assertGreater(len(result['samples']),100)

    def test_browser_import_api_with_real_csv_bytes(self):
        body=SAMPLE.read_bytes()
        data=self.post('/api/journeys/import',body,'text/csv')
        self.assertEqual(data['journey']['verification'] if 'verification' in data['journey'] else data['journey']['integrity'], 'UNVERIFIED_IMPORT')
        name=data['journey']['id']
        with urlopen(self.base+'/api/journey/'+name+'.csv') as r:
            rows=r.read().decode()
        self.assertIn('speed_kmh',rows)
        self.assertIn('MICROSD_IMPORT',rows)
        with self.assertRaises(HTTPError) as e:
            self.post('/api/journeys/import',body,'text/csv',client=False)
        self.assertEqual(e.exception.code,403)

    def test_simulation_creates_long_trip_file(self):
        self.server.engine.action('reset')
        for _ in range(7):self.server.engine.action('accelerate')
        self.assertTrue(self.get('/api/state')['journey_recording'])
        self.server.engine.action('stop')
        self.assertFalse(self.get('/api/state')['journey_recording'])
        journeys=self.get('/api/journeys')
        trip=next(x for x in journeys if x['origin']=='PC_LOGGER')
        self.assertGreaterEqual(trip['peak_kmh'],70)
        self.assertEqual(trip['verification'],'VERIFIED_LOCAL')

if __name__ == '__main__':unittest.main()
