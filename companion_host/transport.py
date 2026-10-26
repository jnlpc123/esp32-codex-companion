import json
import time
import http.client


class HttpProtocol:
    """Direct local HTTP transport: no environment proxy, redirects or credentials."""
    def __init__(self, host, expected_device='', timeout=1.0, connection_factory=None):
        if not host or any(c in host for c in '/@?#\\'):
            raise ValueError('Wi-Fi host must be a hostname or address, optionally with port')
        self.host, self.expected_device, self.timeout = host, expected_device, timeout
        self.factory = connection_factory or http.client.HTTPConnection
        self.next_id = 1

    def close(self):
        pass  # Connections are scoped to one request.

    def request(self, op, **fields):
        rid = self.next_id
        packet = json.dumps(dict(v=1, id=rid, op=op, **fields), separators=(',', ':')).encode()
        if len(packet) > 2048:
            raise ValueError('Wi-Fi command exceeds 2048 bytes')
        self.next_id = self.next_id % 2147483646 + 1
        conn = self.factory(self.host, timeout=self.timeout)
        try:
            conn.request('POST', '/api/companion', packet, {'Content-Type': 'application/json'})
            response = conn.getresponse()
            raw = response.read(4097)
            if response.status != 200 or len(raw) > 4096:
                raise ValueError('Wi-Fi status endpoint rejected or oversized response')
            reply = json.loads(raw)
            if not isinstance(reply, dict) or reply.get('id') != rid or reply.get('ok') is not True or reply.get('product') != 'vibe-companion':
                raise ValueError('Unexpected Wi-Fi companion response')
            if self.expected_device and reply.get('device_id') != self.expected_device:
                raise ValueError('Wi-Fi device identity does not match USB board')
            return reply
        except http.client.HTTPException as exc:
            raise OSError('Wi-Fi HTTP connection failed') from exc
        finally:
            conn.close()


class FailoverLink:
    """USB primary, Wi-Fi fallback; switching restores silently, never sends idle."""
    def __init__(self, connect_usb, connect_wifi):
        self.connect_usb, self.connect_wifi = connect_usb, connect_wifi
        self.peer, self.kind, self.sent, self.reply = None, 'none', None, {}
        self.next_probe = self.next_wifi = self.next_ping = 0
        self.error = ''
        self.sent_summary=None

    @staticmethod
    def fields(state,notify,summary=None,notice=None):
        fields=dict(state=state,notify=notify)
        if summary is not None:
            fields.update(counts=dict(summary['counts']),completion_seq=summary['completion_seq'],scope=dict(summary['scope']))
        if notice:fields.update(notice_kind=notice['kind'],notice_id=notice['id'])
        return fields

    def _adopt(self, peer, kind, state, now,summary=None):
        try:
            reply = peer.request('sync' if kind == 'wifi' else 'state', **self.fields(state,False,summary))
        except (OSError, ValueError, TimeoutError):
            peer.close()
            raise
        if self.peer:
            self.peer.close()
        self.peer, self.kind, self.sent, self.reply = peer, kind, state, reply
        self.next_ping, self.error = now + 2, ''
        self.sent_summary=summary

    def tick(self, state, now,summary=None,notice=None):
        adopted=False
        if self.kind != 'usb' and now >= self.next_probe:
            self.next_probe = now + 2
            try:
                peer = self.connect_usb()
                if peer:
                    self._adopt(peer, 'usb', state, now,summary);adopted=True
            except (OSError, ValueError, TimeoutError) as exc:
                self.error = str(exc)
        if self.peer:
            try:
                if not adopted and (state != self.sent or summary!=self.sent_summary or notice):
                    notify=bool(notice) if summary is not None else state in ('waiting','done')
                    self.reply = self.peer.request('sync' if self.kind == 'wifi' else 'state', **self.fields(state,notify,summary,notice))
                    self.sent = state
                    self.sent_summary=summary
                    self.next_ping = now + 2
                elif now >= self.next_ping:
                    self.reply = (self.peer.request('sync', **self.fields(state,False,summary)) if self.kind == 'wifi' else self.peer.request('ping'))
                    self.next_ping = now + 2
            except (OSError, ValueError, TimeoutError) as exc:
                self.error = str(exc)
                self.peer.close()
                self.peer, self.kind, self.sent = None, 'none', None
        if not self.peer and now >= self.next_wifi:
            self.next_wifi = now + 2
            try:
                peer = self.connect_wifi()
                if peer:
                    self._adopt(peer, 'wifi', state, now,summary)
            except (OSError, ValueError, TimeoutError) as exc:
                self.error = str(exc)

    def close(self):
        if self.peer:
            self.peer.close()
        self.peer, self.kind = None, 'none'


class UsbProtocol:
    def __init__(self, port, timeout=1.5):
        self.port, self.timeout, self.next_id = port, timeout, 1

    def close(self):
        self.port.close()

    def request(self, op, **fields):
        request_id = self.next_id
        packet = json.dumps(dict(v=1, id=request_id, op=op, **fields), separators=(',', ':')).encode() + b'\n'
        if len(packet) > 2048:
            raise ValueError('USB command exceeds 2048 bytes')
        self.next_id = self.next_id % 2147483646 + 1
        self.port.write(packet)
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self.port.readline()
            if not line:
                time.sleep(.005)
                continue
            if not line.startswith(b'VIBE ') or len(line) > 4096:
                continue
            try:
                reply = json.loads(line[5:])
            except (ValueError, UnicodeError):
                continue
            if not isinstance(reply, dict) or reply.get('id') != request_id:
                continue
            if not reply.get('ok'):
                detail=reply.get('error', 'Device rejected command')
                if isinstance(reply.get('errno'),int):detail+=f" (errno {reply['errno']})"
                raise ValueError(detail)
            if isinstance(reply.get('boot_id'),int):self.boot_id=reply['boot_id']
            return reply
        raise TimeoutError('Companion USB acknowledgement timed out')
