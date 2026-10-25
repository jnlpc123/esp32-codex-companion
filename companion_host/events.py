import json
import sqlite3
import time
import hashlib
import re
from contextlib import contextmanager
from pathlib import Path

FIELDS = ('hook_event_name', 'session_id', 'turn_id', 'tool_name', 'tool_use_id', 'source')


def normalize(event):
    result={key: str(event.get(key) or '')[:256] for key in FIELDS}
    cwd=str(event.get('cwd') or '').replace('\\','/').rstrip('/')
    project=str(event.get('project_id') or '')[:48]
    label=str(event.get('project_label') or '')
    if cwd:
        project=hashlib.sha256(cwd.casefold().encode()).hexdigest()[:16]
        label=cwd.rsplit('/',1)[-1]
    result['project_id']=project
    result['project_label']=''.join(c for c in label if c.isprintable()).encode('utf8')[:32].decode('utf8','ignore')
    observed=event.get('observed_at');now=time.time()
    result['observed_at']=observed if isinstance(observed,(int,float)) and 0<observed<=now+60 else now
    signatures=event.get('command_signatures',[])
    command=(event.get('tool_input') or {}).get('command') if isinstance(event.get('tool_input'),dict) else None
    if isinstance(command,str):
        text=re.sub(r'\s+',' ',command).strip()
        variants=sorted({text,text.replace('"','\\"'),text.replace('"','""')})
        signatures=[dict(sha256=hashlib.sha256(v.encode()).hexdigest(),size=len(v.encode())) for v in variants if v]
    result['command_signatures']=[s for s in signatures[:3] if isinstance(s,dict) and isinstance(s.get('size'),int) and 0<s['size']<=32768 and re.fullmatch('[0-9a-f]{64}',str(s.get('sha256','')))]
    return result


class EventStore:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, created REAL NOT NULL, event TEXT NOT NULL)')
            db.execute('CREATE TABLE IF NOT EXISTS snapshot (id INTEGER PRIMARY KEY CHECK(id=1), cursor INTEGER, state TEXT)')

    @contextmanager
    def connect(self):
        db = sqlite3.connect(str(self.path), timeout=.3)
        try:
            with db:
                yield db
        finally:
            db.close()

    def append(self, event):
        with self.connect() as db:
            db.execute('INSERT INTO events(created,event) VALUES(?,?)', (time.time(), json.dumps(normalize(event))))

    def after(self, cursor):
        with self.connect() as db:
            rows = db.execute('SELECT id,event FROM events WHERE id>? ORDER BY id LIMIT 1024', (cursor,)).fetchall()
        return [(row[0], json.loads(row[1])) for row in rows]

    def restore(self):
        with self.connect() as db:
            row = db.execute('SELECT cursor,state FROM snapshot WHERE id=1').fetchone()
        return (row[0], json.loads(row[1])) if row else (0, {})

    def checkpoint(self, cursor, state):
        with self.connect() as db:
            db.execute('INSERT OR REPLACE INTO snapshot(id,cursor,state) VALUES(1,?,?)', (cursor, json.dumps(state)))
            # Retain the cursor row so SQLite cannot reuse a consumed event ID.
            db.execute('DELETE FROM events WHERE id<?', (cursor,))
