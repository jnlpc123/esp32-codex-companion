from dataclasses import dataclass, field


@dataclass
class Session:
    turn: str = ''
    active: bool = False
    done: bool = False
    async_questions: set = field(default_factory=set)
    sync_questions: set = field(default_factory=set)
    approvals: set = field(default_factory=set)
    approval_ids: dict = field(default_factory=dict)
    approval_calls: dict = field(default_factory=dict)
    tools: dict = field(default_factory=dict)
    project_id: str = ''
    project_label: str = ''
    acknowledged: bool = False
    done_seq: int = 0
    order: int = 0
    approval_ui: object = None
    ui_approval_seq: int = 0

    @property
    def waiting(self):
        approval = self.approvals if self.approval_ui is None else self.approval_ui
        return bool(self.async_questions or self.sync_questions or approval)


class StateEngine:
    def __init__(self, approval_confirmation_required=False):
        self.approval_confirmation_required = approval_confirmation_required
        self.sessions = {}
        self.completion_seq = 0
        self.order = 0
        self.request_seq = 0

    def snapshot(self):
        sessions = {sid: dict(turn=s.turn, active=s.active, done=s.done,
                         async_questions=sorted(s.async_questions),
                         sync_questions=sorted(s.sync_questions), approvals=sorted(s.approvals),
                         approval_ids=s.approval_ids,approval_calls=s.approval_calls,tools=s.tools,project_id=s.project_id,project_label=s.project_label,
                         acknowledged=s.acknowledged,done_seq=s.done_seq,order=s.order)
                for sid, s in self.sessions.items()}
        return dict(version=2,sessions=sessions,completion_seq=self.completion_seq,order=self.order,request_seq=self.request_seq)

    def restore(self, snapshot):
        if snapshot.get('version')==2:
            self.request_seq=snapshot.get('request_seq',0)
            self.completion_seq=snapshot.get('completion_seq',0);self.order=snapshot.get('order',0);snapshot=snapshot['sessions']
        self.sessions = {sid: Session(turn=s['turn'], active=s['active'], done=s['done'],
                                     async_questions=set(s['async_questions']),
                                     sync_questions=set(s['sync_questions']), approvals=set(s['approvals']),
                                     approval_ids=s.get('approval_ids',{}),project_id=s.get('project_id',''),project_label=s.get('project_label',''),
                                     approval_calls=s.get('approval_calls',{s.get('approval_ids',{}).get(t,t):t for t in s['approvals']}),tools=s.get('tools',{}),
                                     acknowledged=s.get('acknowledged',False),done_seq=s.get('done_seq',0),order=s.get('order',0))
                         for sid, s in snapshot.items()}
        for s in self.sessions.values():
            s.approval_ui = False if self.approval_confirmation_required else None
            if not s.active:s.approvals.clear();s.approval_ids.clear();s.approval_calls.clear();s.sync_questions.clear()
            self.completion_seq=max(self.completion_seq,s.done_seq)
        for s in self.sessions.values():
            if s.done and not s.done_seq:self.completion_seq+=1;s.done_seq=self.completion_seq

    def selected(self,scope='all',target=''):
        return {sid:s for sid,s in self.sessions.items() if scope=='all' or (scope=='project' and s.project_id==target) or (scope=='session' and sid==target)}

    def views(self,scope='all',target=''):
        result={}
        for sid,s in self.selected(scope,target).items():
            status='waiting' if s.waiting else 'working' if s.active else 'done' if s.done and not s.acknowledged else 'idle'
            waits=tuple(sorted([sid+'/async/'+x for x in s.async_questions]+[sid+'/input/'+x for x in s.sync_questions]+
                               ([sid+'/approval-ui/'+str(s.ui_approval_seq)] if s.approval_ui else
                                [sid+'/approval/'+x for x in s.approval_calls] if s.approval_ui is None else [])))
            result[sid]=dict(status=status,sequence=s.done_seq,waiters=waits)
        return result

    def counts(self,scope='all',target=''):
        states=[v['status'] for v in self.views(scope,target).values()]
        return {key:min(9999,states.count(key)) for key in ('working','waiting','done')}

    def acknowledge(self,through,scope='all',target=''):
        changed=False
        for s in self.selected(scope,target).values():
            if s.done and not s.acknowledged and s.done_seq<=through:s.acknowledged=True;changed=True
        return changed

    def catalog(self,scope='all',target=''):
        ordered=sorted(self.sessions.items(),key=lambda p:p[1].order,reverse=True)
        if scope=='session':ordered.sort(key=lambda p:p[0]!=target)
        sessions=[];projects={}
        for sid,s in ordered:
            label=(sid[:8]+' · '+(s.project_label or '未分组')).encode('utf8')[:32].decode('utf8','ignore')
            if len(sid)<=48 and len(sessions)<8:sessions.append(dict(id=sid,label=label))
            if s.project_id:projects[s.project_id]=dict(id=s.project_id,label=s.project_label or s.project_id)
        project_list=list(projects.values())
        if scope=='project':project_list.sort(key=lambda p:p['id']!=target)
        return dict(projects=project_list[:6],sessions=sessions)

    @property
    def state(self):
        sessions = list(self.sessions.values())
        if any(s.waiting for s in sessions):
            return 'waiting'
        if any(s.active for s in sessions):
            return 'working'
        if any(s.done and not s.acknowledged for s in sessions):
            return 'done'
        return 'idle'

    def apply(self, event):
        name = event.get('hook_event_name', '')
        sid = event.get('session_id', '')
        if not sid:
            return
        if name == 'SessionEnd':
            self.sessions.pop(sid, None)
            return
        ui_default = False if self.approval_confirmation_required else None
        s = self.sessions.setdefault(sid, Session(approval_ui=ui_default))
        turn = event.get('turn_id', '') or ''
        if name == 'UserPromptSubmit':
            self.order+=1
            self.sessions[sid] = Session(turn=turn, active=True,project_id=event.get('project_id') or s.project_id,
                                         project_label=event.get('project_label') or s.project_label,order=self.order,approval_ui=ui_default)
            return
        if name == 'SessionStart':
            return  # Resume/compaction must not clear live state.
        if s.turn and turn and s.turn != turn:
            return  # A late turn or child-agent event cannot replace the parent.
        if event.get('project_id') and not s.project_id:
            s.project_id=event['project_id'];s.project_label=event.get('project_label','')
        tool = event.get('tool_name', '')
        call = event.get('tool_use_id', '') or tool
        if name == 'UIApproval':
            visible = bool(event.get('visible'))
            if visible and not s.approval_ui:
                self.request_seq += 1
                s.ui_approval_seq = self.request_seq
            s.approval_ui = visible
        elif name == 'Interrupt':
            self.sessions[sid] = Session(turn=s.turn,project_id=s.project_id,project_label=s.project_label,order=s.order,approval_ui=ui_default)
        elif name == 'Stop':
            s.active = False
            s.approval_ui = ui_default
            s.approvals.clear();s.approval_ids.clear();s.approval_calls.clear();s.sync_questions.clear();s.tools.clear()
            if not s.waiting and not s.done:
                self.completion_seq+=1;s.done_seq=self.completion_seq;s.acknowledged=False
            s.done = not s.waiting
        elif name == 'ToolRunning':
            if call in s.approval_calls:
                s.approval_calls.pop(call);s.approvals=set(s.approval_calls.values())
                s.approval_ids={v:k for k,v in s.approval_calls.items()}
        elif name in ('PreToolUse', 'PermissionRequest'):
            if not s.done and s.turn and not s.active:
                return
            s.turn = s.turn or turn
            s.active = True
            s.done = False
            if name=='PreToolUse' and call:
                s.tools[call]=dict(tool=tool,signatures=event.get('command_signatures',[]),observed_at=event.get('observed_at',0))
            if name == 'PermissionRequest':
                if not event.get('tool_use_id'):
                    signatures=event.get('command_signatures',[])
                    candidates=[key for key,meta in s.tools.items() if meta['tool']==tool and (not signatures or meta.get('signatures')==signatures)]
                    if candidates:call=candidates[-1]
                    else:
                        if tool not in s.approvals:self.request_seq+=1
                        call=s.approval_ids.get(tool,f'{tool}/{self.request_seq}')
                        s.tools[call]=dict(tool=tool,signatures=signatures,observed_at=event.get('observed_at',0),synthetic=True)
                s.approvals.add(tool)
                s.approval_ids[tool]=call
                s.approval_calls[call]=tool
                if call in s.tools:s.tools[call]['approval_at']=event.get('observed_at',0)
            elif 'request_user_input_async' in tool:
                s.async_questions.add(call)
            elif 'request_user_input' in tool:
                s.sync_questions.add(call)
        elif name == 'PostToolUse' and s.active:
            if s.approval_calls.get(call)==tool:s.approval_calls.pop(call)
            else:
                fallback=[key for key,value in s.approval_calls.items() if value==tool and (key==tool or s.tools.get(key,{}).get('synthetic'))]
                if len(fallback)==1:s.approval_calls.pop(fallback[0]);s.tools.pop(fallback[0],None)
            s.approvals=set(s.approval_calls.values());s.approval_ids={v:k for k,v in s.approval_calls.items()}
            s.tools.pop(call,None)
            if 'request_user_input' in tool and 'request_user_input_async' not in tool:
                s.sync_questions.discard(call)
