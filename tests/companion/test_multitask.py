import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.state import StateEngine
from companion_host.multitask import TaskSummaryController
from companion_host.events import normalize
def event(name,sid='a',turn='t1',tool='',call='c',project='p1'):
 return dict(hook_event_name=name,session_id=sid,turn_id=turn,tool_name=tool,tool_use_id=call,project_id=project,project_label=project)
class MultiTests(unittest.TestCase):
 def test_missing_call_id_approvals_have_distinct_persistent_identity(self):
  e=StateEngine();e.apply(event('UserPromptSubmit'))
  e.apply(event('PermissionRequest',tool='exec_command',call=''));first=e.views()['a']['waiters']
  e.apply(event('PermissionRequest',tool='exec_command',call=''));self.assertEqual(first,e.views()['a']['waiters'])
  e.apply(event('PostToolUse',tool='exec_command',call=''))
  restored=StateEngine();restored.restore(e.snapshot());restored.apply(event('PermissionRequest',tool='exec_command',call=''))
  self.assertNotEqual(first,restored.views()['a']['waiters'])
 def test_independent_counts_and_ack_watermark(self):
  e=StateEngine()
  for sid in ('a','b','c'):e.apply(event('UserPromptSubmit',sid))
  e.apply(event('PreToolUse','b',tool='request_user_input_async'));e.apply(event('Stop','c'))
  self.assertEqual(e.counts(),dict(working=1,waiting=1,done=1));through=e.completion_seq
  e.apply(event('Stop','a'));e.acknowledge(through)
  self.assertEqual(e.counts(),dict(working=0,waiting=1,done=1))
  e.apply(event('Stop','a'));self.assertEqual(e.counts()['done'],1)
 def test_scope_filters_counts_and_ack(self):
  e=StateEngine();e.apply(event('UserPromptSubmit','a',project='one'));e.apply(event('UserPromptSubmit','b',project='two'));e.apply(event('Stop','a'));e.apply(event('Stop','b'))
  e.acknowledge(e.completion_seq,'project','one')
  self.assertEqual(e.counts('project','one')['done'],0);self.assertEqual(e.counts('session','b')['done'],1)
  restored=StateEngine();restored.restore(e.snapshot());self.assertEqual(restored.counts(),e.counts())
 def test_project_metadata_private_path_not_retained(self):
  n=normalize(dict(hook_event_name='UserPromptSubmit',session_id='a',cwd='C:/Private/ProjectA',prompt='secret'))
  self.assertNotIn('cwd',n);self.assertNotIn('secret',str(n));self.assertEqual(n['project_label'],'ProjectA');self.assertEqual(len(n['project_id']),16)
 def test_waiting_timer_not_starved_by_other_sessions(self):
  e=StateEngine();c=TaskSummaryController();c.update(e,0);e.apply(event('UserPromptSubmit'));c.update(e,.1)
  e.apply(event('PreToolUse',tool='request_user_input_async'));c.update(e,1)
  for i in range(3):e.apply(event('UserPromptSubmit','other'+str(i)));c.update(e,1.2+i*.2)
  s,n=c.update(e,1.8);self.assertEqual(s['counts'],dict(working=3,waiting=1,done=0));self.assertEqual(n['kind'],'waiting')
  self.assertIsNone(c.update(e,3)[1])
 def test_group_partial_then_final_and_interrupt_not_success(self):
  e=StateEngine();c=TaskSummaryController();c.update(e,0)
  for sid in ('a','b','c'):e.apply(event('UserPromptSubmit',sid))
  c.update(e,.1);e.apply(event('Stop','a'));c.update(e,1);self.assertIsNone(c.update(e,1.7)[1])
  e.apply(event('Stop','b'));c.update(e,2);c.update(e,2.7)
  s,n=c.update(e,6.8);self.assertEqual(n['kind'],'partial');self.assertEqual(s['counts']['done'],2)
  e.apply(event('Stop','c'));c.update(e,7);self.assertEqual(c.update(e,7.7)[1]['kind'],'all');self.assertIsNone(c.update(e,10)[1])
  e.apply(event('UserPromptSubmit','d'));c.update(e,11);e.apply(event('Interrupt','d'));self.assertIsNone(c.update(e,12)[1])
 def test_scope_switch_and_replay_silent(self):
  e=StateEngine();e.apply(event('UserPromptSubmit'));e.apply(event('Stop'));c=TaskSummaryController();self.assertIsNone(c.update(e,0)[1])
  self.assertIsNone(c.update(e,1,'project','p1')[1]);self.assertEqual(c.update(e,2)[0]['counts']['done'],1)
 def test_child_metadata_cannot_move_parent_project(self):
  e=StateEngine();e.apply(event('UserPromptSubmit',project='parent'));e.apply(event('PreToolUse',turn='child',project='child'))
  self.assertEqual(e.counts('project','parent')['working'],1)
if __name__=='__main__':unittest.main()
