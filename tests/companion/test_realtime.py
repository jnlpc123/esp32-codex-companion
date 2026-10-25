import unittest,sys,pathlib,hashlib,base64
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]))
from companion_host.state import StateEngine
from companion_host.events import normalize
from companion_host.multitask import TaskSummaryController
from companion_host.process_observer import ApprovalProcessObserver,command_matches
def ev(kind,call='',tool='Bash',command=None):
 event=dict(hook_event_name=kind,session_id='a',turn_id='t',tool_name=tool,tool_use_id=call,observed_at=100)
 if command is not None:event['tool_input']={'command':command}
 return normalize(event)
class Source:
 def __init__(self,items):self.items=items
 def sample(self,earliest):return self.items
class Tests(unittest.TestCase):
 def test_process_start_clears_only_matching_approval_before_post(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'))
  e.apply(ev('PreToolUse','one',command='Start-Sleep 60'));e.apply(ev('PermissionRequest',command='Start-Sleep 60'))
  e.apply(ev('PreToolUse','two',command='Start-Sleep 90'));e.apply(ev('PermissionRequest',command='Start-Sleep 90'))
  self.assertEqual(len(e.sessions['a'].approval_calls),2)
  o=ApprovalProcessObserver(Source([(100.2,'pwsh.exe -Command "Start-Sleep 60"')]))
  self.assertEqual(o.update(e,0),1);self.assertEqual(e.state,'waiting');self.assertEqual(set(e.sessions['a'].approval_calls),{'two'})
  e.apply(ev('PostToolUse','one'));self.assertEqual(e.state,'waiting')
  o.source.items=[(100.4,'pwsh -Command "Start-Sleep 90"')];self.assertEqual(o.update(e,1),1);self.assertEqual(e.state,'working')
 def test_no_process_or_old_process_keeps_real_approval_yellow(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'));e.apply(ev('PreToolUse','one',command='Start-Sleep 60'));e.apply(ev('PermissionRequest',command='Start-Sleep 60'))
  o=ApprovalProcessObserver(Source([(50,'pwsh -Command "Start-Sleep 60"')]))
  self.assertEqual(o.update(e,0),0);self.assertEqual(e.state,'waiting')
 def test_previous_process_cannot_clear_later_approval_for_same_call(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'));e.apply(ev('PreToolUse','one',command='work'))
  permission=ev('PermissionRequest','one',command='work');permission['observed_at']=200;e.apply(permission)
  self.assertEqual(ApprovalProcessObserver(Source([(101,'pwsh -Command work')])).update(e,0),0);self.assertEqual(e.state,'waiting')
 def test_ambiguous_identical_parallel_commands_do_not_clear_each_other(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'))
  for call in ('a','b'):e.apply(ev('PreToolUse',call,command='same'));e.apply(ev('PermissionRequest',call,command='same'))
  self.assertEqual(ApprovalProcessObserver(Source([(100.2,'pwsh -Command same')])).update(e,0),0)
  e.apply(ev('PostToolUse','a'));self.assertEqual(set(e.sessions['a'].approval_calls),{'b'})
 def test_stop_releases_denied_or_cancelled_permission_but_keeps_async_question(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'));e.apply(ev('PermissionRequest','one'));e.apply(ev('Stop'));self.assertEqual(e.state,'done')
  e.apply(ev('UserPromptSubmit'));e.apply(ev('PreToolUse','question',tool='request_user_input_async'));e.apply(ev('PermissionRequest','one'));e.apply(ev('Stop'));self.assertEqual(e.state,'waiting');self.assertFalse(e.sessions['a'].approvals)
 def test_visuals_are_immediate_but_notices_settle(self):
  e=StateEngine();c=TaskSummaryController();c.update(e,0);e.apply(ev('UserPromptSubmit'));e.apply(ev('PreToolUse','q',tool='request_user_input'))
  summary,notice=c.update(e,.01);self.assertEqual(summary['counts']['waiting'],1);self.assertIsNone(notice)
  self.assertEqual(c.update(e,.8)[1]['kind'],'waiting')
 def test_brief_permission_review_does_not_flash_yellow(self):
  e=StateEngine();c=TaskSummaryController();c.update(e,0);e.apply(ev('UserPromptSubmit'));e.apply(ev('PermissionRequest','one'))
  self.assertEqual(c.update(e,.01)[0]['state'],'working')
  e.apply(ev('ToolRunning','one'));self.assertEqual(c.update(e,.2)[0]['state'],'working')
  e.apply(ev('PermissionRequest','two'));c.update(e,1);self.assertEqual(c.update(e,1.8)[0]['state'],'waiting')
 def test_command_body_private_and_encoded_shell_matching(self):
  source='Write-Output "private sample"';event=ev('PreToolUse','c',command=source)
  self.assertNotIn('private sample',str(event));self.assertNotIn('tool_input',event)
  encoded=base64.b64encode(source.encode('utf-16-le')).decode()
  self.assertTrue(command_matches('pwsh -EncodedCommand '+encoded,event['command_signatures']))
  self.assertTrue(command_matches('pwsh -Command "'+source.replace('"','\\"')+'"',event['command_signatures']))
 def test_snapshot_preserves_pending_identity(self):
  e=StateEngine();e.apply(ev('UserPromptSubmit'));e.apply(ev('PreToolUse','c',command='work'));e.apply(ev('PermissionRequest',command='work'));other=StateEngine();other.restore(e.snapshot())
  self.assertEqual(ApprovalProcessObserver(Source([(100.2,'pwsh -Command work')])).update(other,0),1);self.assertEqual(other.state,'working')
if __name__=='__main__':unittest.main()
