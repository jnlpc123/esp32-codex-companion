import base64,pathlib,sys,unittest,zlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.media_transfer import MediaTransfer

class Device:
    def __init__(self):self.device='A';self.calls=[];self.content=bytearray();self.lose=False
    def __call__(self,device,op,fields,timeout=5):
        if device!=self.device:raise ValueError('device changed')
        self.calls.append((device,op,fields))
        if op=='import_begin':return {'token':'T'}
        if op=='file_begin':self.content.clear();return {'file_token':'F'}
        if op=='file_chunk':
            raw=base64.b64decode(fields['data']);assert zlib.crc32(raw)==fields['crc32'];assert len(raw)<=768
            if fields['offset']==len(self.content):self.content.extend(raw)
            if self.lose:self.lose=False;raise TimeoutError('lost acknowledgement')
            return {'offset':len(self.content)}
        if op=='file_status':return {'offset':len(self.content)}
        return {'ok':True}

class TransferTests(unittest.TestCase):
    def test_exact_chunked_bytes(self):
        d=Device();t=MediaTransfer(d);t.action('A','begin','v12345678');raw=bytes(range(256))*8
        t.upload('A','v12345678/0001.a5',raw);self.assertEqual(bytes(d.content),raw)
        self.assertEqual(d.calls[-1][1],'file_end')
    def test_lost_chunk_ack_uses_status(self):
        d=Device();t=MediaTransfer(d);t.action('A','begin','v12345678');d.lose=True
        t.upload('A','v12345678/0001.a5',b'x'*1000)
        self.assertEqual(d.content,b'x'*1000);self.assertIn('file_status',[x[1] for x in d.calls])
    def test_no_redirect_to_another_board(self):
        d=Device();t=MediaTransfer(d);t.action('A','begin','v12345678');d.device='B'
        with self.assertRaises(ValueError):t.upload('B','v12345678/0001.a5',b'x')
        self.assertEqual(len(d.calls),1)
    def test_path_and_size_bounds(self):
        d=Device();t=MediaTransfer(d)
        with self.assertRaises(ValueError):t.action('A','begin','../etc')
        t.action('A','begin','v12345678')
        for path in ['../config.json','v12345678/_upload.txt','v12345678/config.json','v87654321/0001.jpg']:
            with self.assertRaises(ValueError):t.upload('A',path,b'x')
        with self.assertRaises(ValueError):t.upload('A','v12345678/0001.a5',b'x'*262145)
    def test_pending_import_exclusive(self):
        d=Device();t=MediaTransfer(d);t.action('A','begin','v12345678')
        with self.assertRaises(ValueError):t.action('A','begin','v87654321')
        t.action('A','abort','v12345678');self.assertIsNone(t.transaction)
    def test_reboot_invalidates_old_import(self):
        d=Device();t=MediaTransfer(d);t.observe('A',1);t.action('A','begin','v12345678')
        t.observe('A',2);self.assertIsNone(t.transaction)
        t.action('A','begin','v87654321');self.assertEqual(t.transaction['directory'],'v87654321')
    def test_changed_board_drops_host_token_without_aborting_new_board(self):
        d=Device();t=MediaTransfer(d);t.observe('A',1);t.action('A','begin','v12345678');d.device='B'
        t.observe('B',1);t.action('B','begin','v87654321')
        self.assertEqual([x[1] for x in d.calls],['import_begin','import_begin'])
    def test_lost_abort_ack_does_not_hold_host_ownership(self):
        d=Device()
        def send(device,op,fields,timeout=5):
            if op=='import_abort':raise TimeoutError('lost abort ACK')
            return d(device,op,fields,timeout)
        t=MediaTransfer(send);t.action('A','begin','v12345678')
        with self.assertRaises(TimeoutError):t.action('A','abort','v12345678')
        self.assertIsNone(t.transaction)

if __name__=='__main__':unittest.main()
