import pathlib,sys,time,json
root=pathlib.Path(__file__).resolve().parents[2]
sys.path[:0]=[str(root),str(root/'companion_host/vendor')]
import serial
from companion_host.transport import UsbProtocol
from esptool.reset import HardReset

port=None;rpc=None;original=None;checks=[];evidence={}
def connect():
 global port,rpc
 end=time.monotonic()+18;last=None
 while time.monotonic()<end:
  candidate=None
  try:
   candidate=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=1)
   candidate.dtr=candidate.rts=False;candidate.port='COM10';candidate.open()
   protocol=UsbProtocol(candidate)
   hello=protocol.request('hello')
   assert hello['product']=='vibe-companion'
   port,rpc=candidate,protocol
   return hello
  except (OSError,TimeoutError,ValueError) as e:
   last=e
   if candidate:candidate.close()
   time.sleep(.5)
 raise RuntimeError('USB boot/reconnect failed') from last

def check(name,condition):
 assert condition,name
 checks.append(name);print('PASS:',name,flush=True)

def rejected(op,**fields):
 try:rpc.request(op,**fields)
 except ValueError:return True
 return False

try:
 hello=connect();original=hello['config'];evidence['hello']=hello
 a=hello['frames'];ready_deadline=time.monotonic()+8
 while True:
  time.sleep(.2);b=rpc.request('ping')
  if all(y>x for x,y in zip(a,b['frames'])) or time.monotonic()>ready_deadline:break
 check('automatic playback without a PLAY command',all(y>x for x,y in zip(a,b['frames'])))
 work=rpc.request('state',state='working',notify=False)
 check('working is green on screen3',work['backgrounds']==[0x0841,0x0841,0x07e0])
 wait=rpc.request('state',state='waiting',notify=True)
 check('waiting is yellow on screen2',wait['backgrounds']==[0x0841,0xffe0,0x0841] and wait['last_tone']=='waiting')
 again=rpc.request('state',state='waiting',notify=True)
 check('duplicate waiting does not repeat notification',again['notification_count']==wait['notification_count'])
 time.sleep(1);after_wait=rpc.request('ping')
 if hello['sound_available']:check('waiting melody produces I2S samples',after_wait['tone_samples']>hello['tone_samples'])
 done=rpc.request('state',state='done',notify=True)
 check('done is red on screen1 with a different tone',done['backgrounds']==[0xf800,0x0841,0x0841] and done['last_tone']=='done')
 again=rpc.request('state',state='done',notify=True)
 check('duplicate completion does not repeat notification',again['notification_count']==done['notification_count'])
 time.sleep(1);after_done=rpc.request('ping')
 if hello['sound_available']:check('completion melody produces additional I2S samples',after_done['tone_samples']>after_wait['tone_samples'])
 check('invalid state rejected',rejected('state',state='unrecognized'))
 check('invalid foreground size rejected',rejected('config',config={'foreground_size':999}))
 check('path traversal rejected',rejected('activate',channel=1,dir='/data/../bad',frames=1))
 check('incomplete sequence rejected',rejected('activate',channel=1,dir=original['media'][0],frames=1))
 check('invalid commands preserve media selection',rpc.request('get')['config']['media']==original['media'])
 port.write(b'x'*2500+b'\n'+b'{bad json}\n')
 check('oversized and malformed lines recover',rpc.request('hello')['product']=='vibe-companion')
 rpc.request('config',config={'layout':'all'})
 all_green=rpc.request('state',state='working',notify=False)
 check('all-screen layout',all_green['backgrounds']==[0x07e0]*3)
 rpc.request('config',config={'layout':'traffic','muted':True,'foreground_size':120,'brightness':55})
 check('mute blocks tone queue',not rpc.request('tone',kind='waiting')['tone_queued'])
 rpc.request('state',state='working',notify=False)
 before_disconnect=rpc.request('get');time.sleep(11.2);offline=rpc.request('get')
 check('heartbeat timeout dims without stopping animation',not offline['usb_connected'] and offline['state']=='idle' and all(y>x for x,y in zip(before_disconnect['frames'],offline['frames'])))
 restored=rpc.request('state',state='done',notify=False)
 check('silent state restore does not add notification',restored['notification_count']==before_disconnect['notification_count'])
 HardReset(port,uses_usb=True)();port.close();port=None
 boot=connect();evidence['after_reset']=boot
 check('settings persist across reset',boot['config']['foreground_size']==120 and boot['config']['brightness']==55 and boot['config']['muted'])
 check('selected assets persist across reset',boot['config']['media']==original['media'])
 check('boot is silent and idle',boot['notification_count']==0 and boot['tone_samples']==0 and boot['state']=='idle')
 start=boot['frames'];ready_deadline=time.monotonic()+8
 while True:
  time.sleep(.2);running=rpc.request('ping')
  if all(y>x for x,y in zip(start,running['frames'])) or time.monotonic()>ready_deadline:break
 evidence['reset_ready']=running
 check('stored assets restart before any PLAY request',all(y>x for x,y in zip(start,running['frames'])))
 rpc.request('config',config=original)
 rpc.request('state',state='working',notify=False);time.sleep(1)
 first=rpc.request('ping');started=time.monotonic();previous=first
 for _ in range(6):
  time.sleep(2);current=rpc.request('ping')
  check('playback counters remain monotonic',all(y>=x for x,y in zip(previous['frames'],current['frames'])))
  previous=current
 elapsed=time.monotonic()-started
 fps=[(y-x)/elapsed for x,y in zip(first['frames'],current['frames'])]
 evidence['fps_per_screen']=fps;evidence['final']=current
 check('sustained three-screen composite playback',all(v>15 for v in fps))
 check('healthy free heap',current['free_heap']>16000)
 print('Composite FPS:',fps,flush=True)
finally:
 if port:
  try:
   if original:rpc.request('config',config=original)
   rpc.request('state',state='idle',notify=False)
  except Exception:pass
  port.close()
 result={'checks':checks,'evidence':evidence}
 pathlib.Path('outputs/companion-device-tests.json').write_text(json.dumps(result,indent=2),encoding='utf8')
