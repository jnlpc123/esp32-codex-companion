"""Single-owner USB/Wi-Fi bridge. No account/API credentials are needed."""
import argparse
import json
import logging
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent), str(HERE / 'vendor')]
from companion_host.events import EventStore
from companion_host.state import StateEngine
from companion_host.multitask import TaskSummaryController
from companion_host.process_observer import ApprovalProcessObserver
from companion_host.history_observer import NativeHistoryObserver
from companion_host.ui_approval import UIApprovalObserver
from companion_host.transport import UsbProtocol, HttpProtocol, FailoverLink

DATA = Path(os.environ.get('VIBE_COMPANION_DATA', str(HERE / 'runtime')))


def start_hidden():
    DATA.mkdir(parents=True, exist_ok=True)
    if (DATA / 'paused').exists():
        return
    heartbeat = DATA / 'status.json'
    if heartbeat.exists() and time.time() - heartbeat.stat().st_mtime < 5:
        return
    executable = Path(sys.executable)
    if os.name == 'nt' and executable.with_name('pythonw.exe').exists():
        executable = executable.with_name('pythonw.exe')
    kwargs = dict(stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if os.name == 'nt':
        kwargs['creationflags'] = subprocess.CREATE_NO_WINDOW
    else:
        kwargs['start_new_session'] = True
    subprocess.Popen([str(executable), str(HERE / 'bridge.py'), '--run'], **kwargs)


def singleton():
    DATA.mkdir(parents=True, exist_ok=True)
    lock = open(DATA / 'bridge.lock', 'a+b')
    if lock.tell() == 0:
        lock.write(b'0')
        lock.flush()
    lock.seek(0)
    try:
        if os.name == 'nt':
            import msvcrt
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        lock.close()
        return None
    return lock


def discover_ports(serial_tools, preferred):
    ports = [p.device for p in serial_tools.comports() if p.vid == 0x303a and p.pid == 0x1001]
    if preferred in ports:
        ports.remove(preferred)
        ports.insert(0, preferred)
    return ports


def run():
    lock = singleton()
    if lock is None:
        return
    from logging.handlers import RotatingFileHandler
    handler = RotatingFileHandler(DATA / 'bridge.log', maxBytes=262144, backupCount=2, encoding='utf8')
    logging.basicConfig(level=logging.INFO, handlers=[handler], format='%(asctime)s %(message)s')
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        logging.exception('pyserial unavailable; install the bundled dependency')
        lock.close()
        return
    config_file = HERE / 'config.json'
    config = json.loads(config_file.read_text(encoding='utf-8-sig')) if config_file.exists() else {}
    use_ui = bool(config.get('ui_approval_detection', False))
    store, engine = EventStore(DATA / 'events.sqlite'), StateEngine(approval_confirmation_required=use_ui)
    controller=TaskSummaryController()
    observer=ApprovalProcessObserver()
    history_observer=NativeHistoryObserver()
    ui_observer=UIApprovalObserver() if use_ui else None
    cursor, saved = store.restore()
    engine.restore(saved)
    while True:
        rows = store.after(cursor)
        if not rows:
            break
        for cursor, event in rows:
            engine.apply(event)
    next_status = 0
    last_prune = cursor
    (DATA / 'stop').unlink(missing_ok=True)
    learned_file = DATA / 'last-device.json'
    try:
        learned = json.loads(learned_file.read_text(encoding='utf8'))
    except (OSError, ValueError):
        learned = {}

    def remember(reply):
        if reply.get('device_id') and reply.get('wifi_ip') and reply['wifi_ip'] != '0.0.0.0':
            value = dict(device_id=reply['device_id'], wifi_ip=reply['wifi_ip'])
            if value != learned:
                temporary = learned_file.with_suffix('.tmp')
                temporary.write_text(json.dumps(value), encoding='utf8')
                os.replace(temporary, learned_file)
                learned.clear(); learned.update(value)

    def connect_usb():
        for name in discover_ports(list_ports, config.get('port', 'COM10')):
            candidate = None
            try:
                candidate = serial.Serial(port=None, baudrate=115200, timeout=.12, write_timeout=1)
                candidate.dtr = candidate.rts = False
                candidate.port = name
                candidate.open()
                protocol = UsbProtocol(candidate)
                reply = protocol.request('hello')
                if reply.get('product') != 'vibe-companion':
                    raise ValueError('Port is not a companion device')
                remember(reply)
                return protocol
            except (OSError, ValueError, TimeoutError):
                if candidate:
                    candidate.close()
        return None

    def connect_wifi():
        candidates = dict.fromkeys(filter(None, (config.get('wifi_host'), learned.get('wifi_ip'), 'esp32.local')))
        expected = config.get('device_id') or learned.get('device_id', '')
        last_error = None
        for host in candidates:
            try:
                protocol = HttpProtocol(host, expected_device=expected)
                reply = protocol.request('hello')
                if not reply.get('device_id') or 'wifi_connected' not in reply:
                    raise ValueError('Device needs Wi-Fi companion firmware')
                remember(reply)
                return protocol
            except (OSError, ValueError, TimeoutError) as exc:
                last_error = exc
        if last_error:
            raise last_error
        return None

    link = FailoverLink(connect_usb, connect_wifi)
    last_ack=None;last_catalog=None;last_delivery_ms=0
    try:
        while not (DATA / 'stop').exists() and not (DATA / 'paused').exists():
            now = time.monotonic()
            batch_at=0
            for cursor, event in store.after(cursor):
                engine.apply(event)
                batch_at=max(batch_at,event.get('observed_at',0))
            process_changed=observer.update(engine,now)
            history_changed=history_observer.update(engine,now)
            before_ack=ui_observer.acknowledgements if ui_observer else 0
            ui_changed=ui_observer.update(engine,now) if ui_observer else 0
            if ui_observer and ui_observer.acknowledgements!=before_ack:
                store.checkpoint(cursor,engine.snapshot())
            if process_changed or history_changed or ui_changed:batch_at=time.time()
            if cursor - last_prune >= 128:
                store.checkpoint(cursor, engine.snapshot())
                last_prune = cursor
            feedback=link.reply
            scope=feedback.get('config',{}).get('scope',{})
            mode,target=scope.get('mode','all'),scope.get('target','')
            ack=feedback.get('ack',{})
            ack_key=(feedback.get('boot_id'),ack.get('id',0))
            if ack.get('id') and ack_key!=last_ack:
                selection=ack.get('scope',{})
                if engine.acknowledge(ack.get('through',0),selection.get('mode','all'),selection.get('target','')):
                    store.checkpoint(cursor,engine.snapshot())
                last_ack=ack_key
            summary,notice=controller.update(engine,now,mode,target)
            state=summary['state']
            previous = link.kind
            before_summary=link.sent_summary
            link.tick(state, now,summary,notice)
            if batch_at and link.peer and link.sent_summary==summary and summary!=before_summary:last_delivery_ms=max(0,(time.time()-batch_at)*1000)
            if link.kind != previous:
                logging.info('Companion transport: %s -> %s', previous, link.kind)
            hello = link.reply
            if link.peer:
                remember(hello)
                catalog=engine.catalog(mode,target)
                catalog_key=(link.peer,json.dumps(catalog,sort_keys=True,ensure_ascii=False))
                if catalog_key!=last_catalog:
                    try:
                        link.reply=link.peer.request('catalog',**catalog)
                        last_catalog=catalog_key
                    except (OSError,ValueError,TimeoutError) as exc:
                        link.error=str(exc);link.close()
            if now >= next_status:
                status = dict(state=state, connected=link.peer is not None, transport=link.kind,
                              port=link.peer.port.port if link.kind == 'usb' else None, last_error=link.error,
                              sound_available=hello.get('sound_available', False), pid=os.getpid())
                status.update(counts=summary['counts'],scope=summary['scope'])
                status.update(ui_observer.status if ui_observer else dict(approval_source='hooks'))
                status.update(permission_process_matches=observer.matches,history_corrections=history_observer.matches,pending_approvals=sum(len(s.approval_calls) for s in engine.sessions.values()),last_delivery_ms=round(last_delivery_ms,1),loop_ms=round((time.monotonic()-now)*1000,1))
                status['wifi_ip'] = hello.get('wifi_ip') or learned.get('wifi_ip', '')
                temporary = DATA / 'status.tmp'
                temporary.write_text(json.dumps(status), encoding='utf8')
                os.replace(temporary, DATA / 'status.json')
                next_status = now + 1
            time.sleep(.08)
    finally:
        if ui_observer:ui_observer.close()
        if link.peer:
            try:
                empty=dict(counts=dict(working=0,waiting=0,done=0),completion_seq=0,scope=dict(mode='all',target=''))
                link.peer.request('sync' if link.kind == 'wifi' else 'state',**link.fields('idle',False,empty))
            except Exception:
                pass
        link.close()
        (DATA / 'status.json').unlink(missing_ok=True)
        lock.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    group = p.add_mutually_exclusive_group(required=True)
    for name in ('run', 'start', 'stop', 'pause', 'resume', 'status'):
        group.add_argument('--' + name, action='store_true')
    args = p.parse_args()
    DATA.mkdir(parents=True, exist_ok=True)
    if args.run:
        run()
    elif args.stop or args.pause:
        (DATA / ('paused' if args.pause else 'stop')).touch()
    elif args.start or args.resume:
        if args.resume:
            (DATA / 'paused').unlink(missing_ok=True)
        start_hidden()
    elif args.status:
        status = DATA / 'status.json'
        print(status.read_text(encoding='utf8') if status.exists() else '{"running":false}')


if __name__ == '__main__':
    main()
