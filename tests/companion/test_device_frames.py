import sys,pathlib,serial,time,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.transport import UsbProtocol
s=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=1);s.dtr=s.rts=False;s.port='COM10';s.open()
try:
 p=UsbProtocol(s);a=p.request('hello');time.sleep(2);b=p.request('ping');delta=[y-x for x,y in zip(a['frames'],b['frames'])];print('Frame deltas:',delta);assert all(v>0 for v in delta),'Every configured screen must render frames'
finally:s.close()
