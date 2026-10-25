import pathlib,sys,tempfile,sqlite3,json,unittest
from contextlib import closing
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.state import StateEngine
from companion_host.history_observer import NativeHistoryObserver
def event(kind,call='',tool='Bash'):
 return dict(hook_event_name=kind,session_id='a',turn_id='t',tool_name=tool,tool_use_id=call)
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.path=pathlib.Path(self.tmp.name)/'history.sqlite'
  with closing(sqlite3.connect(self.path,isolation_level=None)) as c:
   c.executescript('CREATE TABLE thread_turns(thread_id TEXT,turn_id TEXT,status TEXT);CREATE TABLE thread_items(thread_id TEXT,turn_id TEXT,item_id TEXT,item_json TEXT);')
   c.execute('INSERT INTO thread_turns VALUES(?,?,?)',('a','t','inProgress'))
  self.e=StateEngine();self.e.apply(event('UserPromptSubmit'));self.e.apply(event('PreToolUse','c'));self.e.apply(event('PermissionRequest','c'));self.o=NativeHistoryObserver(self.path)
 def tearDown(self):self.tmp.cleanup()
 def test_completed_command_clears_missing_post_without_ending_turn(self):
  with closing(sqlite3.connect(self.path,isolation_level=None)) as c:c.execute('INSERT INTO thread_items VALUES(?,?,?,?)',('a','t','c',json.dumps(dict(status='completed',command='must never be selected'))))
  self.assertEqual(self.o.update(self.e,0),1);self.assertEqual(self.e.state,'working');self.assertFalse(self.e.sessions['a'].done)
 def test_other_turn_or_call_cannot_clear_pending(self):
  with closing(sqlite3.connect(self.path,isolation_level=None)) as c:c.execute('INSERT INTO thread_items VALUES(?,?,?,?)',('a','old','c','{"status":"completed"}'))
  self.assertEqual(self.o.update(self.e,0),0);self.assertEqual(self.e.state,'waiting')
 def test_missing_database_leaves_real_waiting(self):
  self.assertEqual(NativeHistoryObserver(self.path.with_name('missing')).update(self.e,0),0);self.assertEqual(self.e.state,'waiting')
 def test_completed_turn_preserves_async_question(self):
  self.e.apply(event('PreToolUse','q','request_user_input_async'))
  with closing(sqlite3.connect(self.path,isolation_level=None)) as c:c.execute("UPDATE thread_turns SET status='completed'")
  self.o.update(self.e,0);self.assertEqual(self.e.state,'waiting');self.assertFalse(self.e.sessions['a'].approvals)
if __name__=='__main__':unittest.main()
