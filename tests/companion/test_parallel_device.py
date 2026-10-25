"""Bounded real-device regression. Run with production bridge paused."""
import json,sys,time,pathlib,struct
from http.client import HTTPConnection
root=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(root),str(root/'companion_host/vendor')]
import serial
from companion_host.transport import UsbProtocol
from esptool.reset import HardReset
checks=[];evidence={};port=None;rpc=None;original=None
def check(name,ok):
 assert ok,name
 checks.append(name);print('PASS',name,flush=True)
def connect():
 global port,rpc
 end=time.monotonic()+18
 while time.monotonic()<end:
  try:
   port=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=1);port.dtr=port.rts=False;port.port='COM10';port.open()
   rpc=UsbProtocol(port);return rpc.request('hello')
  except (OSError,TimeoutError,ValueError):
   if port:port.close()
   time.sleep(.5)
 raise RuntimeError('Device did not reconnect')
def send(w=0,y=0,r=0,seq=1,**extra):
 return rpc.request('state',state='waiting' if y else 'working' if w else 'done' if r else 'idle',counts=dict(working=w,waiting=y,done=r),completion_seq=seq,scope=dict(mode='all',target=''),notify=False,**extra)
def rejected(op,**fields):
 try:rpc.request(op,**fields)
 except ValueError:return True
 return False
def settle():
 end=time.monotonic()+8
 previous=None;stable=0
 while time.monotonic()<end:
  now=rpc.request('ping')['frames'];stable=stable+1 if previous==now else 0;previous=now
  if stable>=3:return
  time.sleep(.3)
 raise AssertionError('inactive frames did not settle')
def hash_frame(data,bg,gray):
 w,h=struct.unpack_from('<HH',data,4);p=24;pixels=[]
 def grayscale(c):
  y=(((c>>11)*255//31)*77+(((c>>5)&63)*255//63)*150+((c&31)*255//31)*29+128)>>8
  return ((y>>3)<<11)|((y>>2)<<5)|(y>>3)
 while p<len(data):
  token,=struct.unpack_from('<H',data,p);p+=2;k,n=token>>14,token&16383
  if k==0:pixels.extend([bg]*n)
  elif k==1:
   c=int.from_bytes(data[p:p+2],'big');p+=2;pixels.extend([grayscale(c) if gray else c]*n)
  else:
   for _ in range(n):
    c=int.from_bytes(data[p:p+2],'big');p+=2
    if gray:c=grayscale(c)
    if k==3:
     a=data[p];p+=1;i=255-a
     c=((((c>>11)*a+(bg>>11)*i+127)//255)<<11)|(((((c>>5)&63)*a+((bg>>5)&63)*i+127)//255)<<5)|(((c&31)*a+(bg&31)*i+127)//255)
    pixels.append(c)
 assert len(pixels)==w*h
 v=2166136261
 for c in pixels:
  for b in (c>>8,c&255):v=((v^b)*16777619)&0xffffffff
 return v
try:
 hello=connect();original=hello['config'];evidence['original_config']=original
 check('multitask firmware',hello.get('multitask_version')==1)
 rpc.request('config',config={'layout':'traffic','scope':dict(mode='all',target=''),'colors':[0xf800,0xffe0,0x07e0]})
 check('top red middle yellow bottom green',send(2,1,3)['backgrounds']==[0x7800,0xffe0,0x07e0])
 all_done=send(r=3,seq=7);check('all complete full red',all_done['backgrounds']==[0xf800,0x0841,0x0841] and all_done['all_done'])
 ack=rpc.request('ack')['ack'];check('ack captures watermark and scope',ack['through']==7 and ack['scope']==dict(mode='all',target=''))
 send();settle();a=rpc.request('ping')['frames'];time.sleep(1);b=rpc.request('ping')['frames'];check('inactive GIFs stop rendering',a==b)
 send(w=2);time.sleep(.5);a=rpc.request('ping')['frames'];time.sleep(1);b=rpc.request('ping')['frames'];check('only working screen resumes',a[:2]==b[:2] and b[2]>a[2])
 send(y=2);time.sleep(.5);a=rpc.request('ping')['frames'];time.sleep(1);b=rpc.request('ping')['frames'];check('only waiting screen resumes',a[0]==b[0] and a[2]==b[2] and b[1]>a[1])
 send(r=6);check('overflow count retained',rpc.request('get')['counts']['done']==6)
 custom=[0x001f,0xf81f,0x07ff];rpc.request('config',config={'colors':custom})
 check('custom colors independently applied',send(1,1,1)['backgrounds']==[0x000f,0xf81f,0x07ff])
 check('invalid colors rejected atomically',rejected('config',config={'colors':[1,2,65536]}) and rpc.request('get')['config']['colors']==custom)
 check('invalid fractional counts rejected',rejected('state',state='working',counts=dict(working=1.5,waiting=0,done=0),completion_seq=0))
 check('state count mismatch rejected',rejected('state',state='done',counts=dict(working=1,waiting=0,done=0),completion_seq=1))
 cat=rpc.request('catalog',projects=[dict(id='p1',label='测试项目')],sessions=[dict(id='s1',label='任务一')]);got=rpc.request('get_catalog')
 check('catalog is separate bounded response',got['catalog']['projects'][0]['label']=='测试项目')
 rpc.request('config',config={'scope':dict(mode='project',target='p1')});check('scope changes hide old summary',rpc.request('get')['counts']==dict(working=0,waiting=0,done=0))
 matched=rpc.request('state',state='working',counts=dict(working=2,waiting=0,done=0),completion_seq=0,scope=dict(mode='project',target='p1'),notify=False)
 check('matching scope accepted',matched['counts']['working']==2)
 rpc.request('config',config={'scope':dict(mode='all',target=''),'colors':[0xf800,0xffe0,0x07e0]})
 first=rpc.request('state',state='waiting',counts=dict(working=1,waiting=1,done=1),completion_seq=9,notify=True,notice_kind='waiting',notice_id='1234567890abcdef')
 duplicate=rpc.request('state',state='waiting',counts=dict(working=1,waiting=1,done=1),completion_seq=9,notify=True,notice_kind='waiting',notice_id='1234567890abcdef')
 check('notice id deduplicates',first['notification_count']==duplicate['notification_count'])
 conn=HTTPConnection('10.10.10.37',timeout=4);conn.request('GET',original['media'][0].replace('/data','',1)+'/0001.a5');response=conn.getresponse();data=response.read();conn.close()
 check('source transparent frame readable',response.status==200 and data[:4]==b'VAF1')
 for bg in (0x0841,0xf800,0x07e0):
  for gray in (False,True):
   got=rpc.request('frame_info',path=original['media'][0]+'/0001.a5',background=bg,gray=gray)['frame']['pixel_hash']
   check(f'alpha grayscale-before-blend golden {bg}/{gray}',got==hash_frame(data,bg,gray))
 benchmark='/data/vb444b777';rpc.request('config',config={'foreground_size':112,'media':[benchmark]*3});send(1,1,1)
 time.sleep(1);a=rpc.request('ping');start=time.monotonic();time.sleep(4);b=rpc.request('ping');elapsed=time.monotonic()-start
 fps=[(y-x)/elapsed for x,y in zip(a['frames'],b['frames'])];evidence['three_screen_fps']=fps
 check('three active screens at least 29 FPS',all(x>=29 for x in fps))
 rpc.request('config',config={'foreground_size':144});send(1,1,1);time.sleep(.5);check('144px foreground supported above dot footer',rpc.request('get')['config']['foreground_size']==144)
 rpc.request('config',config={'colors':custom});HardReset(port,uses_usb=True)();port.close();hello=connect()
 check('custom colors persist across reboot',hello['config']['colors']==custom)
 send();settle();a=rpc.request('ping')['frames'];time.sleep(.5);check('boot without tasks remains paused',a==rpc.request('ping')['frames'])
finally:
 if rpc and original:
  try:rpc.request('config',config=original)
  finally:
   if port:
    HardReset(port,uses_usb=True)();port.close()
 elif port:port.close()
 evidence['checks']=checks
 (root.parent.parent/'outputs/parallel-device-tests.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
print(len(checks),'device checks passed')
