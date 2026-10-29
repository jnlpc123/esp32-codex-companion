"""Bounded management requests. Only the existing bridge loop calls the port."""
import copy
import queue
import threading
import time

OPS=frozenset(('get','hello','get_catalog','sysinfo','storage_info','storage_list','file_read',
               'config','activate','ack','tone','manual_state','clear_media','cleanup_status','prune_cache','import_begin',
               'import_commit','import_abort','file_begin','file_chunk','file_end','file_status','storage_job_status',
               'wifi_configure','wifi_status','wifi_ignore','factory_reset'))

class ManagementQueue:
    def __init__(self):
        self.requests=queue.Queue(maxsize=8);self.lock=threading.Lock()
        self.identity='';self.transport='none';self.capabilities=[];self.boot_id=None
        self.active_until=0
    def set_device(self,identity,transport,capabilities,boot_id=None):
        with self.lock:
            changed=identity!=self.identity or transport!=self.transport or boot_id!=self.boot_id
            self.identity,self.transport,self.capabilities=identity,transport,list(capabilities)
            self.boot_id=boot_id
            if changed:
                while True:
                    try:item=self.requests.get_nowait()
                    except queue.Empty:break
                    item['error']=ValueError('USB device changed or disconnected');item['done'].set()
    def snapshot(self):
        with self.lock:return dict(device_id=self.identity,transport=self.transport,capabilities=self.capabilities[:],boot_id=self.boot_id)
    def submit(self,device_id,op,fields,timeout=5):
        limit=12 if timeout==5 and op.startswith(('import_','file_')) else timeout
        deadline=time.monotonic()+limit
        result=self._submit_one(device_id,op,fields,min(5,limit))
        while result.get('pending') and result.get('storage_job'):
            remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError('USB storage I/O timed out; inspect before retrying')
            time.sleep(.02)
            result=self._submit_one(device_id,'storage_job_status',{'job_token':result['storage_job']},min(5,remaining))
        return result
    def _submit_one(self,device_id,op,fields,timeout):
        if op not in OPS:raise ValueError('Unsupported management operation')
        event=threading.Event()
        item=dict(device=device_id,op=op,fields=copy.deepcopy(fields),deadline=time.monotonic()+timeout,done=event,cancelled=False)
        with self.lock:
            if self.transport!='usb' or not device_id or self.identity!=device_id:raise ValueError('USB device unavailable or changed')
            try:self.requests.put_nowait(item)
            except queue.Full:raise ValueError('Management queue busy; retry') from None
        if not event.wait(timeout):
            with self.lock:item['cancelled']=True
            raise TimeoutError('USB management timed out; inspect device before retrying')
        if 'error' in item:raise item['error']
        return item['reply']
    def process_one(self,peer,device_id,transport):
        try:item=self.requests.get_nowait()
        except queue.Empty:return
        with self.lock:
            if item['cancelled'] or time.monotonic()>item['deadline']:
                item['error']=TimeoutError('Expired management request');item['done'].set();return
            if not peer or transport!='usb' or device_id!=item['device']:
                item['error']=ValueError('USB device changed');item['done'].set();return
        try:item['reply']=peer.request(item['op'],**item['fields'])
        except (OSError,ValueError,TimeoutError) as exc:item['error']=exc
        finally:
            self.active_until=time.monotonic()+.15
            item['done'].set()
        return item.get('reply')
    @property
    def pending(self):return not self.requests.empty() or time.monotonic()<self.active_until
