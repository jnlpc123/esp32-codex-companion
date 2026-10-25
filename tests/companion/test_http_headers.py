import http.client,json,sys
host=sys.argv[1] if len(sys.argv)>1 else 'esp32.local'
def request(path,cookie_size):
    c=http.client.HTTPConnection(host,timeout=5)
    c.request('GET',path,headers={'Cookie':'browser_test='+('x'*cookie_size),'User-Agent':'Mozilla/5.0 Chrome/Browser compatibility test','Accept':'text/html,application/xhtml+xml,application/json,*/*','Accept-Language':'zh-CN,zh;q=0.9,en;q=0.8'})
    r=c.getresponse();status=r.status;body=r.read();c.close()
    return status,body
checks=[]
for path in ('/','/companion.css','/companion.js','/api/companion'):
    status,body=request(path,4800)
    assert status==200,(path,status,body[:100])
    checks.append(path);print('PASS: browser headers accepted',path,flush=True)
status,body=request('/',9000)
assert status==431,(status,body[:100])
print('PASS: excessive headers remain bounded',flush=True)
assert request('/',0)[0]==200
print('PASS: normal request recovers after rejection',flush=True)
