"""User-authorized read-only Codex UI metadata, isolated from the bridge loop.

Only the helper's boolean/hash JSON and known session title metadata are read.
No UI actions, chat text, commands, approval decisions or credentials are read.
"""
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import sqlite3
import subprocess
import threading
import time


def title_hash(title):
    return hashlib.sha256(re.sub(r'\s+', ' ', title.strip()).encode('utf8')).hexdigest()


class ProbeProcess:
    def __init__(self, executable=None):
        self.executable = Path(executable or Path(__file__).with_name('CodexApprovalProbe.exe'))
        self.process = None
        self.samples = queue.Queue(maxsize=2)
        self.started_at = 0
        self.last_received = 0

    def _read(self, process):
        try:
            for line in process.stdout:
                if len(line) > 65536:
                    continue
                try:
                    value = json.loads(line.lstrip('\ufeff'))
                except ValueError:
                    continue
                if not isinstance(value, dict):
                    continue
                sample = (time.monotonic(), value)
                try:
                    self.samples.put_nowait(sample)
                except queue.Full:
                    try:
                        self.samples.get_nowait()
                    except queue.Empty:
                        pass
                    try:
                        self.samples.put_nowait(sample)
                    except queue.Full:
                        pass
        except (OSError, ValueError):
            pass
        finally:
            process.stdout.close()

    def sample(self, now):
        latest = None
        while True:
            try:
                latest = self.samples.get_nowait()
            except queue.Empty:
                break
        if latest:
            self.last_received = latest[0]
        if self.process is not None and (self.process.poll() is not None or
                now - max(self.started_at, self.last_received) > 6):
            self.close()
        if self.process is None and os.name == 'nt' and self.executable.exists():
            try:
                self.process = subprocess.Popen([str(self.executable)], stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, encoding='utf8',
                    creationflags=subprocess.CREATE_NO_WINDOW)
                self.started_at = now
                threading.Thread(target=self._read, args=(self.process,), daemon=True).start()
            except OSError:
                pass
        return latest

    def close(self):
        process, self.process = self.process, None
        if process is not None and process.poll() is None:
            process.terminate()  # Only the helper process created by this object.


class UIApprovalObserver:
    def __init__(self, source=None, titles_path=None):
        self.source = source or ProbeProcess()
        self.titles_path = Path(titles_path or Path.home()/'.codex/state_5.sqlite')
        self.latest = {}
        self.received_at = None
        self.titles = {}
        self.next_titles = 0
        self.known_ids = ()
        self.status = {}
        self.acknowledgements = 0
        self.view_holds = {}
        self.unique_titles = set()

    def _titles(self, ids, now):
        if ids == self.known_ids and now < self.next_titles:
            return
        self.known_ids, self.next_titles = ids, now + 2
        if not ids:
            self.titles = {}
            self.unique_titles = set()
            return
        try:
            with closing(sqlite3.connect(self.titles_path.resolve().as_uri()+'?mode=ro',
                                        uri=True, timeout=.025)) as db:
                # Fixed columns and known IDs only. Never select message/item text.
                columns={row[1] for row in db.execute('PRAGMA table_info(threads)')}
                display="COALESCE(NULLIF(name,''),title)" if 'name' in columns else 'title'
                rows = db.execute('SELECT id,'+display+' FROM threads WHERE id IN ('+
                                  ','.join('?' for _ in ids)+')', ids).fetchall()
                unique={title_hash(title) for _,title in rows if title and
                        db.execute('SELECT COUNT(*) FROM threads WHERE '+display+'=?',(title,)).fetchone()[0]==1}
            self.titles = {sid:title_hash(title) for sid,title in rows if title}
            # Also reject normalized-name collisions among known sessions.
            self.unique_titles={value for value in unique if list(self.titles.values()).count(value)==1}
        except (sqlite3.Error, OSError, ValueError):
            self.titles = {sid:value for sid,value in self.titles.items() if sid in ids}
            self.unique_titles = set()

    def update(self, engine, now):
        sample = self.source.sample(now)
        if sample:
            self.received_at, self.latest = sample
        age = now-self.received_at if self.received_at is not None else None
        healthy = (age is not None and age < 2 and bool(self.latest.get('available')) and
                   bool(self.latest.get('sidebar_available') or self.latest.get('visible_approval_groups')))
        self._titles(tuple(sorted(engine.sessions)), now)
        hashes = set(self.latest.get('approval_title_hashes', [])) if healthy else set()
        matched_hashes = set()
        changed = 0
        foreground=set(self.latest.get('foreground_title_hashes',[])) if healthy else set()
        viewed=[sid for sid,value in self.titles.items() if value in foreground and value in self.unique_titles]
        viewed_sid=viewed[0] if len(viewed)==1 and len(foreground)==1 else ''
        holds={}
        for sid, session in engine.sessions.items():
            visible = self.titles.get(sid) in hashes
            if visible:
                matched_hashes.add(self.titles[sid])
            if session.approval_ui != visible:
                engine.apply(dict(hook_event_name='UIApproval',session_id=sid,
                                  turn_id=session.turn,visible=visible))
                changed += 1
            if sid==viewed_sid and session.done and not session.active and not session.waiting and not session.acknowledged:
                token=(sid,session.done_seq)
                since=self.view_holds.get(token,now)
                if now-since>=1 and engine.acknowledge(session.done_seq,'session',sid):
                    self.acknowledgements+=1;changed+=1
                else:
                    holds[token]=since
        self.view_holds=holds
        self.status = dict(approval_source='uia', ui_probe_available=healthy,
            ui_sidebar_available=bool(self.latest.get('sidebar_available')),
            ui_sidebar_rows=self.latest.get('sidebar_rows',0),
            ui_query_ms=self.latest.get('query_ms',0),
            ui_stale_ms=round(age*1000) if age is not None else None,
            confirmed_approvals=sum(bool(s.approval_ui) for s in engine.sessions.values()),
            ui_foreground_session=viewed_sid,ui_view_acknowledgements=self.acknowledgements,
            ui_unmatched_approvals=len(hashes-matched_hashes))
        return changed

    def close(self):
        self.source.close()
