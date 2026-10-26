"""HTTP-compatible import transactions transported in bounded USB packets."""
import base64
import re
import threading
import zlib

class MediaTransfer:
    def __init__(self,submit):self.submit=submit;self.lock=threading.Lock();self.transaction=None;self.generation=None
    def observe(self,device,boot_id):
        generation=(device,boot_id)
        if generation==self.generation:return
        with self.lock:
            if generation!=self.generation:self.transaction=None;self.generation=generation
    def _current(self,device,directory):
        t=self.transaction
        if not t or t['device']!=device or t['directory']!=directory:raise ValueError('Upload transaction/device changed')
        return t
    def action(self,device,operation,directory):
        if not re.fullmatch(r'v[0-9a-f]{8}',directory or ''):raise ValueError('Invalid upload directory')
        with self.lock:
            if operation=='begin':
                if self.transaction:raise ValueError('Another import is in progress; finish or cancel it')
                result=self.submit(device,'import_begin',dict(directory=directory))
                self.transaction=dict(device=device,directory=directory,token=result['token']);return dict(ok=True)
            if operation not in ('commit','abort'):raise ValueError('Unknown import operation')
            t=self._current(device,directory)
            try:result=self.submit(device,'import_'+operation,dict(token=t['token']))
            except ValueError:
                if operation=='abort':self.transaction=None
                raise
            except TimeoutError:
                # Lost commit ACK: activation is the durable source of truth.
                if operation!='commit':self.transaction=None;raise
                status=self.submit(device,'get',{})
                if '/data/'+directory not in status.get('config',{}).get('media',[]):raise
                result=dict(ok=True)
            except OSError:
                if operation=='abort':self.transaction=None
                raise
            self.transaction=None;return result
    def upload(self,device,path,data):
        if not re.fullmatch(r'v[0-9a-f]{8}/(?:[0-9]{4}\.(?:jpg|a5)|_preview\.png|_timing\.txt)',path or ''):raise ValueError('Invalid upload filename')
        if not data or len(data)>262144:raise ValueError('File size exceeds 256 KiB')
        directory,name=path.split('/')
        with self.lock:
            t=self._current(device,directory)
            started=self.submit(device,'file_begin',dict(token=t['token'],name=name,length=len(data),crc32=zlib.crc32(data)))
            token=started['file_token'];offset=0;retries=0
            while offset<len(data):
                chunk=data[offset:offset+768];end=offset+len(chunk)
                try:result=self.submit(device,'file_chunk',dict(file_token=token,offset=offset,data=base64.b64encode(chunk).decode('ascii'),crc32=zlib.crc32(chunk)))
                except TimeoutError:
                    result=self.submit(device,'file_status',dict(file_token=token))
                    if result.get('offset')==offset:
                        retries+=1
                        if retries>=3:raise TimeoutError('USB upload made no progress') from None
                        continue
                if result.get('offset')!=end:raise ValueError('Unexpected USB upload offset; do not activate')
                offset=end;retries=0
            try:self.submit(device,'file_end',dict(file_token=token))
            except TimeoutError:self.submit(device,'file_end',dict(file_token=token))
            return dict(ok=True)
    def read(self,device,path):
        if not re.fullmatch(r'/data/[A-Za-z0-9_-]{1,31}/(?:[0-9]{4}\.(?:jpg|a5)|_preview\.png|_timing\.txt)',path or ''):raise ValueError('Invalid media read path')
        result=bytearray();length=None
        while True:
            reply=self.submit(device,'file_read',dict(path=path,offset=len(result),size=768))
            if reply.get('offset')!=len(result):raise ValueError('Unexpected media read offset')
            n=reply.get('length');chunk=base64.b64decode(reply.get('data',''),validate=True)
            if not isinstance(n,int) or n<0 or n>262144 or (length is not None and n!=length):raise ValueError('Invalid or changed media length')
            length=n
            if len(chunk)>768 or len(result)+len(chunk)>length:raise ValueError('Oversized media reply')
            result.extend(chunk)
            if reply.get('eof'):
                if len(result)!=length:raise ValueError('Incomplete media read')
                return bytes(result)
            if not chunk:raise ValueError('Media read made no progress')
