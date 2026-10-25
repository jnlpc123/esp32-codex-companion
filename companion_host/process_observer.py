"""Read-only Windows process-start evidence. Command text never leaves memory.

Only fresh shells descended from Codex are inspected. Hooks retain SHA256 and
lengths; an exact command match proves approval has passed, without changing it.
"""
import base64
import ctypes as C
from ctypes import wintypes as W
import hashlib
import os
import re

def command_matches(text,signatures):
    variants=[text]
    match=re.search(r'(?i)-(?:encodedcommand|enc)\s+["\']?([A-Za-z0-9+/=]+)',text)
    if match:
        try:variants.append(base64.b64decode(match[1]).decode('utf-16-le'))
        except (ValueError,UnicodeError):pass
    for text in variants:
        data=re.sub(r'\s+',' ',text).strip().encode()
        for sig in signatures:
            size=sig['size'];expected=sig['sha256']
            for start in range(max(0,len(data)-size+1)):
                if hashlib.sha256(data[start:start+size]).hexdigest()==expected:return True
    return False

class WindowsProcesses:
    def __init__(self):
        self.enabled=os.name=='nt'
        if not self.enabled:return
        self.k=C.WinDLL('kernel32',use_last_error=True);self.nt=C.WinDLL('ntdll')
        class Entry(C.Structure):
            _fields_=[('size',W.DWORD),('usage',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),('module',W.DWORD),('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),('flags',W.DWORD),('name',W.WCHAR*260)]
        self.Entry=Entry
        self.k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];self.k.CreateToolhelp32Snapshot.restype=W.HANDLE
        self.k.Process32FirstW.argtypes=[W.HANDLE,C.POINTER(Entry)];self.k.Process32NextW.argtypes=[W.HANDLE,C.POINTER(Entry)]
        self.k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];self.k.OpenProcess.restype=W.HANDLE
        self.k.CloseHandle.argtypes=[W.HANDLE]
        self.k.GetProcessTimes.argtypes=[W.HANDLE,C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME)]
        self.nt.NtQueryInformationProcess.argtypes=[W.HANDLE,W.ULONG,C.c_void_p,W.ULONG,C.POINTER(W.ULONG)];self.nt.NtQueryInformationProcess.restype=W.LONG
    def sample(self,earliest):
        if not self.enabled:return []
        h=self.k.CreateToolhelp32Snapshot(2,0)
        if not h or h==C.c_void_p(-1).value:return []
        processes={};e=self.Entry();e.size=C.sizeof(e)
        try:
            ok=self.k.Process32FirstW(h,C.byref(e))
            while ok:
                processes[e.pid]=(e.parent,e.name.lower());ok=self.k.Process32NextW(h,C.byref(e))
        finally:self.k.CloseHandle(h)
        result=[]
        for pid,(parent,name) in processes.items():
            if name not in ('pwsh.exe','powershell.exe','cmd.exe','bash.exe','wsl.exe'):continue
            ancestor=parent;owned=False
            for _ in range(5):
                item=processes.get(ancestor)
                if not item:break
                if item[1]=='codex.exe':owned=True;break
                ancestor=item[0]
            if not owned:continue
            ph=self.k.OpenProcess(0x1000,False,pid)
            if not ph:continue
            try:
                times=[W.FILETIME() for _ in range(4)]
                if not self.k.GetProcessTimes(ph,*[C.byref(x) for x in times]):continue
                created=((times[0].dwHighDateTime<<32)|times[0].dwLowDateTime)/10000000-11644473600
                if created<earliest-.1:continue
                needed=W.ULONG();self.nt.NtQueryInformationProcess(ph,60,None,0,C.byref(needed))
                if needed.value<16 or needed.value>131072:continue
                buffer=C.create_string_buffer(needed.value)
                if self.nt.NtQueryInformationProcess(ph,60,buffer,len(buffer),C.byref(needed))<0:continue
                class Unicode(C.Structure):_fields_=[('length',W.USHORT),('maximum',W.USHORT),('pointer',C.c_void_p)]
                u=Unicode.from_buffer(buffer);start=C.addressof(buffer)
                if not u.pointer or u.pointer<start or u.pointer+u.length>start+len(buffer):continue
                result.append((created,C.wstring_at(u.pointer,u.length//2)))
            finally:self.k.CloseHandle(ph)
        return result

class ApprovalProcessObserver:
    def __init__(self,source=None):self.source=source or WindowsProcesses();self.next_poll=0;self.matches=0
    def update(self,engine,now):
        if now<self.next_poll:return 0
        self.next_poll=now+.15
        pending=[]
        for sid,s in engine.sessions.items():
            for call,tool in s.approval_calls.items():
                meta=s.tools.get(call,{})
                if meta.get('signatures') and meta.get('observed_at'):pending.append((sid,s.turn,call,meta))
        if not pending:return 0
        try:processes=self.source.sample(min(p[3].get('approval_at') or p[3]['observed_at'] for p in pending))
        except (OSError,ValueError):return 0
        count=0
        confirmed=set()
        for created,text in processes:
            matches=[(sid,turn,call) for sid,turn,call,meta in pending if created>=(meta.get('approval_at') or meta['observed_at'])-.1 and command_matches(text,meta['signatures'])]
            if len(matches)==1:confirmed.add(matches[0])
        for sid,turn,call in confirmed:
            engine.apply(dict(hook_event_name='ToolRunning',session_id=sid,turn_id=turn,tool_use_id=call));count+=1
        self.matches+=count;return count
