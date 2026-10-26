"""Opt-in real-device smoke test. Pause the bridge first; never clears user media."""
from pathlib import Path
import argparse,base64,json,secrets,sys,time,zlib
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT),str(ROOT/'companion_host/vendor')]
import serial
from companion_host.transport import UsbProtocol

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--expected-device',required=True,help='Exact device_id from a trusted handshake')
parser.add_argument('--temporary-upload',action='store_true',help='Write, verify and abort only a new unselected test import')
args=parser.parse_args()
port=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=1)
port.dtr=port.rts=False;port.port=args.port;port.open()
port.write(b'\n') # Discard a partial line left by a prior serial owner.
rpc=UsbProtocol(port,timeout=3)
token=None
def inventory():
    files=[];cursor=0;revision=None
    while True:
        page=rpc.request('storage_list',cursor=cursor,limit=8)
        assert revision is None or revision==page['revision'],'Inventory changed; rerun while storage is idle'
        revision=page['revision'];files+=page['files']
        if page['complete']:return sorted((f['name'],f['bytes']) for f in files)
        assert page['next_cursor']>cursor;cursor=page['next_cursor']
try:
    hello=rpc.request('hello');assert hello['device_id']==args.expected_device,'Wrong board'
    assert 'usb_media_v1' in hello.get('capabilities',[]),'Upgrade firmware first'
    before=inventory();info=rpc.request('sysinfo');deadline=time.monotonic()+10
    while True:
        report=rpc.request('storage_info')
        if not report.get('pending'):break
        assert time.monotonic()<deadline,'Inventory timed out';time.sleep(.08)
    assert report['storage']['files']==len(before)
    if args.temporary_upload:
        directory='v'+secrets.token_hex(4)
        token=rpc.request('import_begin',directory=directory)['token']
        raw=bytes(range(256))*8+b'!'
        file=rpc.request('file_begin',token=token,name='_preview.png',length=len(raw),crc32=zlib.crc32(raw))['file_token']
        for offset in range(0,len(raw),768):
            block=raw[offset:offset+768]
            result=rpc.request('file_chunk',file_token=file,offset=offset,data=base64.b64encode(block).decode(),crc32=zlib.crc32(block))
            assert result['offset']==offset+len(block)
        rpc.request('file_end',file_token=file);rpc.request('file_end',file_token=file)
        received=bytearray()
        while True:
            reply=rpc.request('file_read',path='/data/'+directory+'/_preview.png',offset=len(received),size=768)
            received.extend(base64.b64decode(reply['data'],validate=True))
            if reply['eof']:break
        assert bytes(received)==raw
        rpc.request('import_abort',token=token);token=None
        assert inventory()==before,'Unexpected file-list change'
    assert rpc.request('get')['config']==hello['config'],'Settings changed'
    print(json.dumps(dict(ok=True,version=info['app_version'],files=len(before),temporary_upload=args.temporary_upload)))
finally:
    if token:
        try:rpc.request('import_abort',token=token)
        except (ValueError,OSError,TimeoutError):pass
    port.close()
