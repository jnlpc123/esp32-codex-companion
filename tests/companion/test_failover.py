import pathlib,sys,unittest,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.transport import HttpProtocol,FailoverLink

class Peer:
 def __init__(self,kind):self.kind=kind;self.calls=[];self.broken=False
 def request(self,op,**fields):
  if self.broken:raise OSError('link lost')
  self.calls.append((op,fields))
  return dict(ok=True,product='vibe-companion',device_id='board-a',wifi_ip='10.10.10.37')
 def close(self):pass

class FailoverTests(unittest.TestCase):
 def setUp(self):self.usb=Peer('usb');self.wifi=Peer('wifi');self.available=True;self.link=FailoverLink(lambda:self.usb if self.available else None,lambda:self.wifi)
 def test_usb_first_and_no_wifi_during_healthy_usb(self):
  self.link.tick('working',0);self.assertEqual(self.link.kind,'usb');self.assertEqual(self.wifi.calls,[])
 def test_usb_failure_switches_wifi_silently_and_returns_usb(self):
  self.link.tick('waiting',0);self.usb.broken=True;self.available=False;self.link.tick('waiting',2)
  self.assertEqual(self.link.kind,'wifi');self.assertFalse(self.wifi.calls[-1][1]['notify'])
  self.link.tick('done',3);self.assertTrue(self.wifi.calls[-1][1]['notify'])
  self.usb.broken=False;self.available=True;self.link.tick('done',5)
  self.assertEqual(self.link.kind,'usb');self.assertFalse(self.usb.calls[-1][1]['notify'])
 def test_wifi_failure_does_not_stop_usb_retry(self):
  self.available=False;self.link.tick('working',0);self.wifi.broken=True;self.link.tick('working',2)
  self.assertEqual(self.link.kind,'none');self.available=True;self.link.tick('working',4);self.assertEqual(self.link.kind,'usb')
 def test_wifi_heartbeat_renews_state_without_sound(self):
  self.available=False;self.link.tick('done',0);self.link.tick('done',2)
  self.assertEqual(self.wifi.calls[-1],('sync',dict(state='done',notify=False)))
 def test_unchanged_state_does_not_send_every_tick(self):
  self.link.tick('working',0);count=len(self.usb.calls);self.link.tick('working',.1);self.assertEqual(len(self.usb.calls),count)

class HttpTests(unittest.TestCase):
 def exchange(self,reply,status=200):
  test=self
  class Connection:
   def __init__(self,*a,**k):self.status=status;test.closed=False
   def request(self,method,path,packet,headers):test.packet=json.loads(packet);test.assertEqual(path,'/api/companion')
   def getresponse(self):return self
   def read(self,limit):return json.dumps(reply).encode()[:limit]
   def close(self):test.closed=True
  return HttpProtocol('10.10.10.37',expected_device='board-a',connection_factory=Connection)
 def test_bounded_sync_and_identity(self):
  p=self.exchange(dict(id=1,ok=True,product='vibe-companion',device_id='board-a'))
  p.request('sync',state='working',notify=False);self.assertEqual(self.packet['op'],'sync');self.assertTrue(self.closed)
 def test_rejects_wrong_device_or_request_id(self):
  for reply in (dict(id=1,ok=True,product='vibe-companion',device_id='other'),dict(id=999,ok=True,product='vibe-companion',device_id='board-a')):
   with self.assertRaises(ValueError):self.exchange(reply).request('hello')
   self.assertTrue(self.closed)
 def test_rejects_redirect_and_large_reply(self):
  with self.assertRaises(ValueError):self.exchange({},302).request('hello')
  with self.assertRaises(ValueError):self.exchange(dict(padding='x'*5000)).request('hello')
 def test_rejects_host_with_credentials_or_path(self):
  for host in ('user:password@host','http://host','host/path'):
   with self.assertRaises(ValueError):HttpProtocol(host)

if __name__=='__main__':unittest.main()
