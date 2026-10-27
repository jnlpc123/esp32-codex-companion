import http.client,json,pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.management_server import ManagementServer

class FakeQueue:
    def __init__(self):self.calls=[]
    def snapshot(self):return dict(device_id='A',transport='usb',capabilities=['storage_v1','usb_media_v1'])
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
    def test_requires_token_and_matching_device(self):
        body=json.dumps(dict(op='config',config={'brightness':40}))
        self.assertEqual(self.request('/api/companion',body)[0],403)
        headers=self.auth();headers['X-Vibe-Device']='B'
        self.assertEqual(self.request('/api/companion',body,headers)[0],409)
        self.assertEqual(self.queue.calls,[])
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

if __name__=='__main__':unittest.main()
