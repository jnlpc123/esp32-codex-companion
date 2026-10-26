import pathlib,sys,threading,time,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.management_queue import ManagementQueue

class Peer:
    def __init__(self):self.calls=[]
    def request(self,op,**fields):self.calls.append((op,fields));return dict(ok=True,op=op)

class QueueTests(unittest.TestCase):
    def test_only_owner_executes(self):
        q=ManagementQueue();q.set_device('A','usb',['storage_v1']);p=Peer();result=[]
        t=threading.Thread(target=lambda:result.append(q.submit('A','storage_info',{},1)));t.start()
        time.sleep(.02);self.assertEqual(p.calls,[]);q.process_one(p,'A','usb');t.join();self.assertTrue(result[0]['ok'])
    def test_changed_board_cancels_without_write(self):
        q=ManagementQueue();q.set_device('A','usb',[]);errors=[];p=Peer()
        def submit():
            try:q.submit('A','config',{'config':{'brightness':40}},1)
            except Exception as e:errors.append(str(e))
        t=threading.Thread(target=submit);t.start();time.sleep(.02);q.set_device('B','usb',[]);q.process_one(p,'B','usb');t.join()
        self.assertTrue(errors);self.assertEqual(p.calls,[])
    def test_timeout_has_no_late_side_effect(self):
        q=ManagementQueue();q.set_device('A','usb',[]);p=Peer()
        with self.assertRaises(TimeoutError):q.submit('A','config',{},.01)
        q.process_one(p,'A','usb');self.assertEqual(p.calls,[])
    def test_wifi_not_management_transport(self):
        q=ManagementQueue();q.set_device('A','wifi',[])
        with self.assertRaises(ValueError):q.submit('A','get',{},.01)
    def test_unknown_op_rejected(self):
        q=ManagementQueue();q.set_device('A','usb',[])
        with self.assertRaises(ValueError):q.submit('A','read_arbitrary_computer_file',{},.01)
    def test_boot_generation_in_snapshot(self):
        q=ManagementQueue();q.set_device('A','usb',[],boot_id=123)
        self.assertEqual(q.snapshot()['boot_id'],123)
    def test_transfer_reply_keeps_short_poll_window(self):
        q=ManagementQueue();q.set_device('A','usb',[]);p=Peer()
        t=threading.Thread(target=lambda:q.submit('A','file_chunk',{},1));t.start()
        time.sleep(.02);q.process_one(p,'A','usb');t.join()
        self.assertTrue(q.pending)
    def test_usb_reply_refreshes_boot_without_reconnect(self):
        from companion_host.transport import UsbProtocol
        class Port:
            def write(self,data):pass
            def readline(self):return b'VIBE {"id":1,"ok":true,"boot_id":2}\n'
        peer=UsbProtocol(Port());peer.boot_id=1;peer.request('get')
        self.assertEqual(peer.boot_id,2)

if __name__=='__main__':unittest.main()
