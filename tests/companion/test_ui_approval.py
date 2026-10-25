import unittest, sys, sqlite3, tempfile
from contextlib import closing
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from companion_host.state import StateEngine
from companion_host.multitask import TaskSummaryController
from companion_host.ui_approval import UIApprovalObserver, title_hash

def event(name,sid='a',**fields):
    return dict(hook_event_name=name,session_id=sid,turn_id='t',**fields)

class ConfirmedApprovalTests(unittest.TestCase):
    def engine(self):
        engine=StateEngine(approval_confirmation_required=True)
        engine.apply(event('UserPromptSubmit'))
        return engine

    def test_slow_automatic_review_never_lights_yellow(self):
        engine=self.engine();controller=TaskSummaryController();controller.update(engine,0)
        engine.apply(event('PermissionRequest',tool_name='Bash',tool_use_id='c'))
        for now in (1,10,100):
            summary,notice=controller.update(engine,now)
            self.assertEqual(summary['state'],'working');self.assertIsNone(notice)

    def test_actual_ui_without_permission_hook_is_waiting_immediately(self):
        engine=self.engine();controller=TaskSummaryController();controller.update(engine,0)
        engine.apply(event('UIApproval',visible=True))
        self.assertEqual(controller.update(engine,.01)[0]['state'],'waiting')
        self.assertEqual(controller.update(engine,.8)[1]['kind'],'waiting')

    def test_closed_ui_clears_yellow_even_when_raw_hook_is_pending(self):
        engine=self.engine()
        engine.apply(event('PermissionRequest',tool_name='MCP',tool_use_id='c'))
        engine.apply(event('UIApproval',visible=True));self.assertEqual(engine.state,'waiting')
        engine.apply(event('UIApproval',visible=False));self.assertEqual(engine.state,'working')
        self.assertTrue(engine.sessions['a'].approval_calls)

    def test_explicit_question_survives_closed_ui(self):
        engine=self.engine()
        engine.apply(event('PreToolUse',tool_name='request_user_input_async',tool_use_id='q'))
        engine.apply(event('UIApproval',visible=False));self.assertEqual(engine.state,'waiting')

    def test_parallel_scope_and_completion(self):
        engine=self.engine();engine.apply(event('UserPromptSubmit','b'))
        engine.apply(event('UIApproval','b',visible=True))
        self.assertEqual(engine.counts(),dict(working=1,waiting=1,done=0))
        self.assertEqual(engine.views('session','a')['a']['status'],'working')
        engine.apply(event('Stop','b'));self.assertEqual(engine.views()['b']['status'],'done')

    def test_restart_does_not_restore_stale_controls(self):
        engine=self.engine();engine.apply(event('UIApproval',visible=True))
        restored=StateEngine(approval_confirmation_required=True);restored.restore(engine.snapshot())
        self.assertEqual(restored.state,'working')

class FakeProbe:
    def __init__(self,sample):self.value=sample
    def sample(self,now):return self.value
    def close(self):pass

class UIObserverTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.path=Path(self.temp.name)/'titles.sqlite'
        with closing(sqlite3.connect(self.path,isolation_level=None)) as db:
            db.execute('CREATE TABLE threads(id TEXT,title TEXT,private_chat TEXT)')
            db.executemany('INSERT INTO threads VALUES(?,?,?)',[('a','任务 A','secret'),('b','任务 B','secret')])
        self.engine=StateEngine(approval_confirmation_required=True)
        for sid in ('a','b'):self.engine.apply(event('UserPromptSubmit',sid))
    def tearDown(self):self.temp.cleanup()
    def observer(self,hashes,now=1):
        source=FakeProbe((now,dict(available=True,sidebar_available=True,approval_title_hashes=hashes)))
        return UIApprovalObserver(source,self.path)
    def test_only_confirmed_task_waits_and_title_normalization_matches(self):
        observer=self.observer([title_hash('  任务   B ')])
        observer.update(self.engine,1)
        self.assertEqual(self.engine.views()['a']['status'],'working')
        self.assertEqual(self.engine.views()['b']['status'],'waiting')
        self.assertEqual(observer.status['confirmed_approvals'],1)
        self.assertNotIn('secret',str(observer.__dict__))
    def test_control_disappearance_restores_working_without_new_process(self):
        observer=self.observer([title_hash('任务 A')]);observer.update(self.engine,1)
        observer.source.value=(1.5,dict(available=True,sidebar_available=True,approval_title_hashes=[]))
        observer.update(self.engine,1.5);self.assertEqual(self.engine.state,'working')
    def test_stale_or_unavailable_probe_never_falls_back_to_false_hook_yellow(self):
        observer=self.observer([title_hash('任务 A')]);observer.update(self.engine,1)
        observer.source.value=None
        self.engine.apply(event('PermissionRequest',tool_name='MCP',tool_use_id='c'))
        observer.update(self.engine,4)
        self.assertEqual(self.engine.state,'working');self.assertFalse(observer.status['ui_probe_available'])
    def test_unknown_title_is_reported_not_assigned_to_wrong_task(self):
        observer=self.observer([title_hash('unknown')]);observer.update(self.engine,1)
        self.assertEqual(self.engine.state,'working');self.assertEqual(observer.status['ui_unmatched_approvals'],1)

    def viewed(self,observer,title,now):
        observer.source.value=(now,dict(available=True,sidebar_available=True,
            approval_title_hashes=[],foreground_title_hashes=[title_hash(title)]))
        return observer.update(self.engine,now)

    def test_viewing_completed_chat_acknowledges_only_that_chat(self):
        for sid in ('a','b'):self.engine.apply(event('Stop',sid))
        observer=self.observer([])
        self.viewed(observer,'任务 A',1);self.assertEqual(self.engine.counts()['done'],2)
        self.viewed(observer,'任务 A',2.1)
        self.assertTrue(self.engine.sessions['a'].acknowledged)
        self.assertFalse(self.engine.sessions['b'].acknowledged)
        self.assertEqual(observer.acknowledgements,1)

    def test_background_chat_and_brief_view_do_not_acknowledge(self):
        self.engine.apply(event('Stop'))
        observer=self.observer([]);self.viewed(observer,'任务 A',1)
        observer.source.value=(1.2,dict(available=True,sidebar_available=True,approval_title_hashes=[]))
        observer.update(self.engine,1.2)
        self.viewed(observer,'任务 A',2);self.viewed(observer,'任务 A',2.5)
        self.assertFalse(self.engine.sessions['a'].acknowledged)

    def test_open_running_chat_does_not_consume_later_completion(self):
        observer=self.observer([]);self.viewed(observer,'任务 A',1);self.viewed(observer,'任务 A',3)
        self.engine.apply(event('Stop'));self.viewed(observer,'任务 A',3.1)
        self.assertFalse(self.engine.sessions['a'].acknowledged)
        self.viewed(observer,'任务 A',4.2);self.assertTrue(self.engine.sessions['a'].acknowledged)

    def test_new_completion_gets_a_new_view_hold(self):
        self.engine.apply(event('Stop'));observer=self.observer([])
        self.viewed(observer,'任务 A',1);self.viewed(observer,'任务 A',2.1)
        self.engine.apply(event('UserPromptSubmit'));self.engine.apply(event('Stop'))
        self.viewed(observer,'任务 A',2.2);self.assertFalse(self.engine.sessions['a'].acknowledged)
        self.viewed(observer,'任务 A',3.3);self.assertTrue(self.engine.sessions['a'].acknowledged)

    def test_duplicate_display_names_never_ack_wrong_completed_chat(self):
        with closing(sqlite3.connect(self.path,isolation_level=None)) as db:
            db.execute("UPDATE threads SET title='相同标题'")
        for sid in ('a','b'):self.engine.apply(event('Stop',sid))
        observer=self.observer([]);self.viewed(observer,'相同标题',1);self.viewed(observer,'相同标题',3)
        self.assertEqual(self.engine.counts()['done'],2)

    def test_renamed_chat_uses_display_name_not_original_title(self):
        with closing(sqlite3.connect(self.path,isolation_level=None)) as db:
            db.execute('ALTER TABLE threads ADD COLUMN name TEXT')
            db.execute("UPDATE threads SET name='显示名称' WHERE id='a'")
        self.engine.apply(event('Stop'));observer=self.observer([])
        self.viewed(observer,'显示名称',1);self.viewed(observer,'显示名称',2.1)
        self.assertTrue(self.engine.sessions['a'].acknowledged)

    def test_stale_view_cannot_acknowledge_completion(self):
        self.engine.apply(event('Stop'));observer=self.observer([])
        self.viewed(observer,'任务 A',1);observer.source.value=None
        observer.update(self.engine,3.1);self.assertFalse(self.engine.sessions['a'].acknowledged)

    def test_unknown_chat_with_same_name_prevents_wrong_ack(self):
        with closing(sqlite3.connect(self.path,isolation_level=None)) as db:
            db.execute('INSERT INTO threads VALUES(?,?,?)',('not-tracked','任务 A','secret'))
        self.engine.apply(event('Stop'));observer=self.observer([])
        self.viewed(observer,'任务 A',1);self.viewed(observer,'任务 A',3)
        self.assertFalse(self.engine.sessions['a'].acknowledged)

    def test_view_ack_is_preserved_by_snapshot(self):
        self.engine.apply(event('Stop'));observer=self.observer([])
        self.viewed(observer,'任务 A',1);self.viewed(observer,'任务 A',2.1)
        restored=StateEngine(True);restored.restore(self.engine.snapshot())
        self.assertTrue(restored.sessions['a'].acknowledged)

if __name__=='__main__':unittest.main()
