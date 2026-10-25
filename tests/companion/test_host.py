import sys,pathlib,unittest,tempfile,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.state import StateEngine
from companion_host.multitask import TaskSummaryController
from companion_host.events import EventStore,normalize
from companion_host.transport import UsbProtocol
from companion_host.install import merge,definitions

def ev(name,s='a',turn='t1',tool='',call='c1',source=''):
 return dict(hook_event_name=name,session_id=s,turn_id=turn,tool_name=tool,tool_use_id=call,source=source)

class StateTests(unittest.TestCase):
 def test_submit_work_and_stop_complete(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));self.assertEqual(x.state,'working');x.apply(ev('Stop'));self.assertEqual(x.state,'done')
 def test_other_work_prevents_false_completion(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('UserPromptSubmit','b'));x.apply(ev('Stop'));self.assertEqual(x.state,'working');x.apply(ev('Stop','b'));self.assertEqual(x.state,'done')
 def test_async_question_holds_yellow_through_tool_return_and_stop(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('PreToolUse',tool='functions.request_user_input_async'));x.apply(ev('PostToolUse',tool='functions.request_user_input_async'));x.apply(ev('PostToolUse',tool='Bash'));x.apply(ev('Stop'));self.assertEqual(x.state,'waiting');x.apply(ev('UserPromptSubmit',turn='t2'));self.assertEqual(x.state,'working')
 def test_sync_question_clears_only_on_matching_return(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('PreToolUse',tool='request_user_input'));x.apply(ev('PostToolUse',tool='Bash',call='other'));self.assertEqual(x.state,'waiting');x.apply(ev('PostToolUse',tool='request_user_input'));self.assertEqual(x.state,'working')
 def test_permission_matches_tool_and_resumes(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('PermissionRequest',tool='Bash'));x.apply(ev('PostToolUse',tool='apply_patch'));self.assertEqual(x.state,'waiting');x.apply(ev('PostToolUse',tool='Bash'));self.assertEqual(x.state,'working')
 def test_waiting_has_priority_over_other_work(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('PreToolUse',tool='request_user_input_async'));x.apply(ev('UserPromptSubmit','b'));self.assertEqual(x.state,'waiting')
 def test_stale_stop_cannot_end_new_turn(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('UserPromptSubmit',turn='t2'));x.apply(ev('Stop'));self.assertEqual(x.state,'working')
 def test_subagent_other_turn_cannot_replace_parent(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('PreToolUse',turn='child',tool='Bash'));x.apply(ev('Stop',turn='child'));self.assertEqual(x.state,'working')
 def test_compaction_preserves_pending_state(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('SessionStart',source='compact'));self.assertEqual(x.state,'working')
 def test_interrupt_does_not_signal_completion(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('Interrupt'));self.assertEqual(x.state,'idle')
 def test_duplicate_stops_and_late_posts_do_not_reopen(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('Stop'));x.apply(ev('Stop'));x.apply(ev('PostToolUse',tool='Bash'));self.assertEqual(x.state,'done')
 def test_new_tool_after_stop_resumes_same_turn(self):
  x=StateEngine();x.apply(ev('UserPromptSubmit'));x.apply(ev('Stop'));x.apply(ev('PreToolUse',tool='Bash',call='new-call'));self.assertEqual(x.state,'working')
 def test_fast_auto_approval_does_not_flash_yellow(self):
  e=StateEngine();x=TaskSummaryController();e.apply(ev('UserPromptSubmit'));x.update(e,0)
  e.apply(ev('PermissionRequest',tool='Bash'));self.assertEqual(x.update(e,1)[0]['state'],'working')
  e.apply(ev('ToolRunning'));self.assertEqual(x.update(e,1.2)[0]['state'],'working')
  e.apply(ev('PermissionRequest',tool='Bash',call='c2'));self.assertEqual(x.update(e,2)[0]['state'],'working');self.assertEqual(x.update(e,2.8)[0]['state'],'waiting')
 def test_completion_debounced_for_auto_continuations(self):
  e=StateEngine();x=TaskSummaryController();e.apply(ev('UserPromptSubmit'));x.update(e,0)
  e.apply(ev('Stop'));self.assertIsNone(x.update(e,1)[1]);e.apply(ev('UserPromptSubmit',turn='t2'));self.assertEqual(x.update(e,1.1)[0]['state'],'working')
  e.apply(ev('Stop',turn='t2'));self.assertIsNone(x.update(e,2)[1]);self.assertEqual(x.update(e,2.7)[1]['kind'],'all')

class StoreTests(unittest.TestCase):
 def test_checkpoint_preserves_old_waiting_session(self):
  with tempfile.TemporaryDirectory(dir=pathlib.Path(__file__).resolve().parent) as d:
   store=EventStore(pathlib.Path(d)/'events.sqlite');engine=StateEngine()
   for event in [ev('UserPromptSubmit'),ev('PreToolUse',tool='request_user_input_async'),ev('UserPromptSubmit','b')]+[ev('PreToolUse','b',tool='Bash')]*1200:
    store.append(event);engine.apply(event)
   cursor=0
   while True:
    rows=store.after(cursor)
    if not rows:break
    cursor=rows[-1][0]
   store.checkpoint(cursor,engine.snapshot())
   restored_cursor,snapshot=store.restore();restored=StateEngine();restored.restore(snapshot)
   for _,event in store.after(restored_cursor):restored.apply(event)
   self.assertEqual(restored.state,'waiting')
   store.append(ev('SessionEnd'));self.assertGreater(store.after(restored_cursor)[0][0],restored_cursor)
 def test_normalized_events_drop_private_content(self):
  e=ev('UserPromptSubmit');e.update(prompt='secret',transcript_path='private',tool_input={'token':'secret'});n=normalize(e);self.assertNotIn('secret',json.dumps(n));self.assertNotIn('transcript_path',n)
 def test_sqlite_queue_order_and_cursor(self):
  with tempfile.TemporaryDirectory(dir=pathlib.Path(__file__).resolve().parent) as d:
   s=EventStore(pathlib.Path(d)/'events.sqlite');s.append(ev('UserPromptSubmit'));s.append(ev('Stop'));rows=s.after(0);self.assertEqual([e['hook_event_name'] for _,e in rows],['UserPromptSubmit','Stop']);self.assertEqual(s.after(rows[-1][0]),[])

class FakePort:
 def __init__(self,replies):self.replies=list(replies);self.writes=[]
 def write(self,b):self.writes.append(b);return len(b)
 def readline(self):return self.replies.pop(0) if self.replies else b''

class InstallerTests(unittest.TestCase):
 def test_merge_is_idempotent_and_preserves_other_commands(self):
  old={'description':'keep','hooks':{'Stop':[{'hooks':[{'type':'command','command':'echo existing'}]}]}}
  own=definitions('C:/Python/python.exe','C:/My Folder/hook.py')
  new=merge(old,own);self.assertEqual(merge(new,own),new)
  self.assertEqual(new['hooks']['Stop'][0]['hooks'][0]['command'],'echo existing')
  self.assertEqual(old['hooks']['Stop'][0]['hooks'][0]['command'],'echo existing')
 def test_powershell_paths_escape_quotes(self):
  own=definitions("C:/User's Folder/python.exe",'C:/My Folder/hook.py')
  self.assertIn("'C:/User''s Folder/python.exe'",own['Stop'][0]['hooks'][0]['command'])

class ProtocolTests(unittest.TestCase):
 def test_filters_boot_noise_and_matches_request_id(self):
  p=FakePort([b'boot log\n',b'VIBE {bad\n',b'VIBE {"id":0,"ok":true}\n',b'VIBE {"id":1,"ok":true,"product":"vibe-companion"}\n']);u=UsbProtocol(p);self.assertEqual(u.request('hello')['product'],'vibe-companion');self.assertEqual(json.loads(p.writes[0])['v'],1)
 def test_state_request_can_suppress_reconnect_tone(self):
  p=FakePort([b'VIBE {"id":1,"ok":true}\n']);UsbProtocol(p).request('state',state='done',notify=False);self.assertFalse(json.loads(p.writes[0])['notify'])
 def test_rejects_oversized_packet(self):
  with self.assertRaises(ValueError):UsbProtocol(FakePort([])).request('config',data='x'*4096)

if __name__=='__main__':unittest.main()
