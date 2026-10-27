"""Loopback-only UI. No serial ownership, network-device proxy or account access."""
import json
import mimetypes
from pathlib import Path
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlsplit
from companion_host.media_transfer import MediaTransfer

ASSETS={'/':'index.html','/index.html':'index.html','/companion.css':'companion.css',
        '/companion.js':'companion.js','/alpha-codec.js':'alpha-codec.js','/vendor/gifuct.js':'vendor/gifuct.js',
        '/network/ap-mode.jpg':'../main/wifi/assets/ap-mode.jpg',
        '/network/wifi-connecting.jpg':'../main/wifi/assets/wifi-connecting.jpg',
        '/network/wifi-connected.jpg':'../main/wifi/assets/wifi-connected.jpg'}
COMMANDS=frozenset(('get','get_catalog','storage_info','storage_list','config','activate','ack','tone','state','clear_media','cleanup_status','prune_cache'))

def wifi_credentials(request):
    ssid,password=request.get('ssid'),request.get('password')
    if not isinstance(ssid,str) or not isinstance(password,str) or '\0' in ssid+password:
        raise ValueError('Invalid Wi-Fi credentials')
    try:slen,plen=len(ssid.encode('utf8')),len(password.encode('utf8'))
    except UnicodeError:raise ValueError('Invalid Wi-Fi credentials') from None
    if not 1<=slen<=32 or not (plen==0 or 8<=plen<=63 or plen==64 and all(c in '0123456789abcdefABCDEF' for c in password)):
        raise ValueError('Invalid Wi-Fi credentials: SSID 1-32 bytes, password 8-63 bytes or empty')
    return dict(ssid=ssid,password=password)

class ManagementServer:
    def __init__(self,queue,web_root,bind='127.0.0.1',port=17831):
        if bind!='127.0.0.1':raise ValueError('Management must bind to IPv4 loopback')
        self.queue,self.web_root,self.bind,self.port=queue,Path(web_root),bind,port
        self.token=secrets.token_urlsafe(32);self.http=None;self.thread=None
        self.transfer=MediaTransfer(queue.submit)
    @property
    def url(self):return f'http://127.0.0.1:{self.port}/'
    def start(self):
        if self.http:return self.url
        owner=self
        class Handler(BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self,*args):pass
            def send(self,status,data,content_type='application/json'):
                if not isinstance(data,bytes):data=json.dumps(data,ensure_ascii=False).encode('utf8')
                self.send_response(status);self.send_header('Content-Type',content_type)
                self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store')
                self.send_header('X-Content-Type-Options','nosniff');self.end_headers()
                try:self.wfile.write(data)
                except (BrokenPipeError,ConnectionResetError):pass
            def safe_origin(self):
                host=self.headers.get('Host','')
                if host not in (f'127.0.0.1:{owner.port}',f'localhost:{owner.port}'):return False
                expected='http://'+host;origin=self.headers.get('Origin');referer=self.headers.get('Referer')
                return (not origin or origin==expected) and (not referer or referer.startswith(expected+'/'))
            def device(self):
                state=owner.queue.snapshot()
                owner.transfer.observe(state.get('device_id'),state.get('boot_id'))
                if state['transport']!='usb' or not state['device_id']:raise ValueError('USB device not connected')
                return state
            def query(self,op,fields=None,expected_device=None):
                state=self.device()
                if expected_device is not None and state['device_id']!=expected_device:raise ValueError('USB device changed before command delivery')
                if op in ('wifi_status','wifi_configure','wifi_ignore') and 'wifi_management_v1' not in state['capabilities']:
                    raise ValueError('Device firmware needs USB Wi-Fi upgrade')
                if op not in ('get','get_catalog','config','ack','tone','manual_state','wifi_status','wifi_configure','wifi_ignore') and 'storage_v1' not in state['capabilities']:
                    raise ValueError('Device firmware needs USB management upgrade')
                deadline=time.monotonic()+8
                while True:
                    result=owner.queue.submit(state['device_id'],op,fields or {})
                    if op!='storage_info' or not result.get('pending'):return result
                    if time.monotonic()>=deadline:raise TimeoutError('Storage inventory timed out; retry')
                    time.sleep(.08)
                    current=self.device()
                    if current['device_id']!=state['device_id'] or current.get('boot_id')!=state.get('boot_id'):
                        raise ValueError('USB device changed during storage inventory')
            def do_GET(self):
                if not self.safe_origin():self.send(403,dict(ok=False,error='Invalid Host or Origin'));return
                path=self.path
                try:
                    if path=='/api/local':
                        state=owner.queue.snapshot();owner.transfer.observe(state.get('device_id'),state.get('boot_id'));self.send(200,dict(ok=True,token=owner.token,**state));return
                    if path in ASSETS:
                        file=owner.web_root/ASSETS[path]
                        if not file.is_file():self.send(404,dict(ok=False,error='Web asset missing'));return
                        self.send(200,file.read_bytes(),mimetypes.guess_type(str(file))[0] or 'application/octet-stream');return
                    if path=='/api/companion':self.send(200,self.query('get'));return
                    if path=='/api/sysinfo':self.send(200,self.query('sysinfo'));return
                    if path=='/api/storage':self.send(200,self.query('storage_info'));return
                    if path=='/api/wifi/status':self.send(200,self.query('wifi_status',expected_device=self.headers.get('X-Vibe-Device'))['wifi']);return
                    if path.startswith('/api/') or '..' in path or '%' in path or '?' in path or '#' in path:
                        self.send(404,dict(ok=False,error='Not found'));return
                    state=self.device();raw=owner.transfer.read(state['device_id'],'/data'+path)
                    self.send(200,raw,mimetypes.guess_type(path)[0] or 'application/octet-stream')
                except (ValueError,OSError,TimeoutError) as exc:
                    self.send(404 if 'path' in str(exc).lower() else 503,dict(ok=False,error=str(exc)))
            def do_POST(self):
                if not self.safe_origin() or not secrets.compare_digest(self.headers.get('X-Vibe-Key',''),owner.token):
                    self.close_connection=True;self.send(403,dict(ok=False,error='Invalid local access token or Origin'));return
                try:
                    state=self.device();device=self.headers.get('X-Vibe-Device','')
                    if device!=state['device_id']:self.close_connection=True;self.send(409,dict(ok=False,error='USB device changed; refresh before writing'));return
                    try:length=int(self.headers.get('Content-Length','-1'))
                    except ValueError:length=-1
                    limit=262144 if self.path.startswith('/upload/') else 4096
                    if length<1 or length>limit:
                        self.close_connection=True;self.send(413,dict(ok=False,error='Invalid request length'));return
                    self.connection.settimeout(10);body=self.rfile.read(length)
                    if len(body)!=length:raise ValueError('Incomplete request body')
                    if self.path.startswith('/upload/'):
                        if 'usb_media_v1' not in state['capabilities']:raise ValueError('Device firmware needs USB media upgrade')
                        result=owner.transfer.upload(device,self.path[len('/upload/'):],body)
                    else:
                        try:request=json.loads(body)
                        except (ValueError,UnicodeError):raise ValueError('Invalid JSON') from None
                        if not isinstance(request,dict):raise ValueError('Invalid JSON command')
                        op=request.get('op')
                        if self.path=='/api/wifi/ignore':
                            if not isinstance(request.get('ignored'),bool):raise ValueError('Invalid Wi-Fi ignore flag')
                            result=self.query('wifi_ignore',dict(ignored=request['ignored']),device)
                        elif self.path=='/api/wifi/configure':
                            result=self.query('wifi_configure',wifi_credentials(request),device)
                        elif self.path=='/api/import':
                            if 'usb_media_v1' not in state['capabilities']:raise ValueError('Device firmware needs USB media upgrade')
                            result=owner.transfer.action(device,op,request.get('directory'))
                        elif self.path=='/api/companion' and op in COMMANDS:
                            fields={k:v for k,v in request.items() if k not in ('v','id','op')}
                            if op=='state':op='manual_state'
                            result=self.query(op,fields,device)
                        else:raise ValueError('Unsupported management operation')
                    self.send(200,result)
                except (ValueError,OSError,TimeoutError) as exc:
                    self.send(400 if 'Unsupported' in str(exc) or 'Invalid' in str(exc) else 409,dict(ok=False,error=str(exc)))
        self.http=ThreadingHTTPServer((self.bind,self.port),Handler);self.http.daemon_threads=True
        self.port=self.http.server_address[1]
        self.thread=threading.Thread(target=self.http.serve_forever,name='companion-management',daemon=True);self.thread.start()
        return self.url
    def stop(self):
        if self.http:self.http.shutdown();self.http.server_close();self.http=None
        if self.thread:self.thread.join(timeout=3);self.thread=None
