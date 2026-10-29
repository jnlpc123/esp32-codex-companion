import http.client,json,pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.management_server import ManagementServer

class FakeQueue:
    def __init__(self):self.calls=[];self.capabilities=['storage_v1','usb_media_v1','wifi_management_v1']
    def snapshot(self):return dict(device_id='A',transport='usb',capabilities=self.capabilities)
    def submit(self,device,op,fields,timeout=5):self.calls.append((device,op,fields));return dict(ok=True,device_id='A',config={})

class ServerTests(unittest.TestCase):
    def setUp(self):
        self.queue=FakeQueue();self.server=ManagementServer(self.queue,pathlib.Path(__file__).resolve().parents[2]/'web',port=0);self.server.start()
        self.port=self.server.port;self.host=f'127.0.0.1:{self.port}'
    def tearDown(self):self.server.stop()
    def request(self,path,body=None,headers=None,method=None):
        c=http.client.HTTPConnection('127.0.0.1',self.port,timeout=3)
        try:
            c.request(method or ('POST' if body is not None else 'GET'),path,body,headers or {});r=c.getresponse();return r.status,r.read()
        finally:c.close()
    def auth(self):return {'X-Vibe-Key':self.server.token,'X-Vibe-Device':'A','Origin':'http://'+self.host}
    def test_bootstrap_and_static(self):
        status,raw=self.request('/api/local');self.assertEqual(status,200);self.assertEqual(json.loads(raw)['device_id'],'A')
        self.assertNotIn(self.server.token,self.server.url);self.assertEqual(self.request('/')[0],200)
    def test_dedicated_wifi_page_assets(self):
        for path in ('/wifi','/wifi.html','/wifi.js','/network.js'):
            status,raw=self.request(path)
            self.assertEqual(status,200,path)
            self.assertTrue(raw)
        self.assertIn(b'id="wifiForm"',self.request('/wifi')[1])
        self.assertNotIn(b'id="wifiForm"',self.request('/')[1])

    def test_requires_token_and_matching_device(self):
        body=json.dumps(dict(op='config',config={'brightness':40}))
        self.assertEqual(self.request('/api/companion',body)[0],403)
        headers=self.auth();headers['X-Vibe-Device']='B'
        self.assertEqual(self.request('/api/companion',body,headers)[0],409)
        self.assertEqual(self.queue.calls,[])
    def test_factory_reset_requires_capability_and_bound_confirmation(self):
        body=json.dumps(dict(op='factory_reset',confirm='FACTORY_RESET',device_id='A'))
        self.assertEqual(self.request('/api/companion',body,self.auth())[0],409)
        self.assertEqual(self.queue.calls,[])
        self.queue.capabilities.append('factory_reset_v1')
        for fields in (dict(confirm='RESET',device_id='A'),dict(confirm='FACTORY_RESET',device_id='B'),dict(confirm='FACTORY_RESET')):
            self.assertEqual(self.request('/api/companion',json.dumps(dict(op='factory_reset',**fields)),self.auth())[0],400)
        self.assertEqual(self.queue.calls,[])
        self.assertEqual(self.request('/api/companion',body,self.auth())[0],200)
        self.assertEqual(self.queue.calls,[('A','factory_reset',dict(confirm='FACTORY_RESET',device_id='A'))])
    def test_rejects_foreign_origin_and_host(self):
        headers=self.auth();headers['Origin']='https://evil.example'
        self.assertEqual(self.request('/api/companion','{}',headers)[0],403)
        headers=self.auth();headers['Host']='evil.example'
        self.assertEqual(self.request('/api/local',headers=headers)[0],403)
    def test_no_arbitrary_paths_or_operations(self):
        self.assertEqual(self.request('/../../companion_host/config.json')[0],404)
        self.assertEqual(self.request('/api/companion',json.dumps(dict(op='file_chunk')),self.auth())[0],400)
        self.assertEqual(self.request('/api/companion','x'*4097,self.auth())[0],413)
    def test_config_routes_through_queue(self):
        self.assertEqual(self.request('/api/companion',json.dumps(dict(op='config',config={'brightness':40})),self.auth())[0],200)
        self.assertEqual(self.queue.calls,[('A','config',{'config':{'brightness':40}})])
    def test_state_test_is_manual_not_host_state(self):
        self.assertEqual(self.request('/api/companion',json.dumps(dict(op='state',state='working')),self.auth())[0],200)
        self.assertEqual(self.queue.calls[0][1],'manual_state')
    def test_storage_report_polls_without_returning_partial_totals(self):
        answers=iter([dict(ok=True,pending=True),dict(ok=True,storage={'files':261})])
        self.queue.submit=lambda *args,**kwargs:next(answers)
        status,raw=self.request('/api/storage')
        self.assertEqual(status,200)
        self.assertEqual(json.loads(raw).get('storage'),{'files':261})

    def test_usb_wifi_configure_routes_credentials_without_echo(self):
        def submit(device,op,fields,timeout=5):
            self.queue.calls.append((device,op,fields))
            return dict(ok=True,wifi=dict(setup=dict(state='connecting',job_id=7)))
        self.queue.submit=submit
        packet=dict(ssid='新网络',password='test-pass-123')
        code,raw=self.request('/api/wifi/configure',json.dumps(packet),self.auth())
        self.assertEqual(code,200)
        self.assertEqual(json.loads(raw)['wifi']['setup']['state'],'connecting')
        self.assertNotIn(b'test-pass-123',raw)
        self.assertEqual(self.queue.calls,[('A','wifi_configure',packet)])
    def test_usb_wifi_write_still_requires_token_and_matching_device(self):
        packet=json.dumps(dict(ssid='home',password='12345678'))
        self.assertEqual(self.request('/api/wifi/configure',packet)[0],403)
        h=self.auth();h['X-Vibe-Device']='B'
        self.assertEqual(self.request('/api/wifi/configure',packet,h)[0],409)
        self.assertEqual(self.queue.calls,[])
    def test_usb_wifi_status_reports_physical_connection(self):
        self.queue.capabilities=['wifi_management_v1']
        self.queue.submit=lambda *a,**k:dict(ok=True,wifi=dict(connected=True,ip='192.0.2.8',ssid='test',ready=True))
        code,raw=self.request('/api/wifi/status')
        self.assertEqual(code,200);self.assertEqual(json.loads(raw)['ip'],'192.0.2.8')
    def test_usb_wifi_old_firmware_rejects_without_sending_password(self):
        self.queue.capabilities=['storage_v1']
        code,raw=self.request('/api/wifi/configure',json.dumps(dict(ssid='home',password='12345678')),self.auth())
        self.assertEqual(code,409);self.assertEqual(self.queue.calls,[])
        self.assertNotIn(b'12345678',raw)
    def test_usb_wifi_rejects_invalid_credentials_before_delivery(self):
        for packet in (dict(ssid='',password='12345678'),dict(ssid='中'*11,password='12345678'),
                       dict(ssid='home',password='short'),dict(ssid='home',password='g'*64),
                       dict(ssid='home',password=None),dict(ssid='home\x00other',password='12345678')):
            code,raw=self.request('/api/wifi/configure',json.dumps(packet),self.auth())
            self.assertEqual(code,400,packet)
        self.assertEqual(self.queue.calls,[])

    def test_wifi_ignore_requires_boolean_and_routes_to_device(self):
        code,raw=self.request('/api/wifi/ignore',json.dumps(dict(ignored=True)),self.auth())
        self.assertEqual(code,200)
        self.assertEqual(self.queue.calls,[('A','wifi_ignore',{'ignored':True})])
        self.assertEqual(self.request('/api/wifi/ignore',json.dumps(dict(ignored=1)),self.auth())[0],400)
        self.assertEqual(len(self.queue.calls),1)

    def test_original_ap_picture_served_without_reading_device_storage(self):
        code,raw=self.request('/network/ap-mode.jpg')
        self.assertEqual(code,200);self.assertTrue(raw.startswith(b'\xff\xd8'));self.assertEqual(len(raw),5608)
        self.assertEqual(self.queue.calls,[])

if __name__=='__main__':unittest.main()
