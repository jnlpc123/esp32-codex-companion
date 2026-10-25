"""Read only lifecycle metadata for known pending calls; never select item text."""
from pathlib import Path
import sqlite3
from contextlib import closing

class NativeHistoryObserver:
    def __init__(self,path=None):
        self.path=Path(path) if path else Path.home()/'.codex/thread_history_1.sqlite'
        self.next_poll=0;self.matches=0
    def update(self,engine,now):
        if now<self.next_poll:return 0
        self.next_poll=now+.25
        pending=[(sid,s.turn,tuple(s.approval_calls)) for sid,s in engine.sessions.items() if s.approval_calls and s.turn]
        if not pending or not self.path.exists():return 0
        count=0
        try:
            with closing(sqlite3.connect(self.path.resolve().as_uri()+'?mode=ro',uri=True,timeout=.025)) as db:
                for sid,turn,calls in pending:
                    row=db.execute('SELECT status FROM thread_turns WHERE thread_id=? AND turn_id=?',(sid,turn)).fetchone()
                    if row and row[0] in ('completed','interrupted','failed'):
                        engine.apply(dict(hook_event_name='Stop' if row[0]=='completed' else 'Interrupt',session_id=sid,turn_id=turn));count+=1;continue
                    for call in calls:
                        row=db.execute("SELECT json_extract(item_json,'$.status') FROM thread_items WHERE thread_id=? AND turn_id=? AND item_id=?",(sid,turn,call)).fetchone()
                        if row and row[0] in ('completed','failed','declined','interrupted'):
                            engine.apply(dict(hook_event_name='ToolRunning',session_id=sid,turn_id=turn,tool_use_id=call));count+=1
        except (sqlite3.Error,OSError,ValueError):pass
        self.matches+=count;return count
