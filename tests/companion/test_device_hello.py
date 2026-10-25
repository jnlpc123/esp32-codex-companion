import sys,pathlib,serial,json
root=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(root))
from companion_host.transport import UsbProtocol
s=serial.Serial(port=None,baudrate=115200,timeout=.15,write_timeout=1);s.dtr=s.rts=False;s.port='COM10';s.open()
try:
 hello=UsbProtocol(s).request('hello')
 assert hello['product']=='vibe-companion'
 print(json.dumps(hello,ensure_ascii=False))
finally:s.close()
