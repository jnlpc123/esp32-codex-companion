"""Per-task settling, independent counts and non-repeating grouped notices."""
import hashlib


class TaskSummaryController:
    def __init__(self):
        self.scope=None
        self.stable={}
        self.candidates={}
        self.seen_waiters=set()
        self.seen_done=set()
        self.partial_due=None
        self.partial_items=set()

    @staticmethod
    def notice(kind,items):
        token=hashlib.sha256((kind+repr(sorted(items))).encode()).hexdigest()[:16]
        return dict(kind=kind,id=token)

    def update(self,engine,now,scope='all',target=''):
        raw=engine.views(scope,target);quiet=self.scope!=(scope,target)
        if quiet:
            self.scope=(scope,target);self.stable=dict(raw);self.candidates={};self.partial_due=None;self.partial_items=set()
        else:
            for sid in list(self.stable):
                if sid not in raw:self.stable.pop(sid,None);self.candidates.pop(sid,None)
            for sid in list(self.candidates):
                if sid not in raw:self.candidates.pop(sid,None)
            for sid,item in raw.items():
                if self.candidates.get(sid,(None,0))[0]!=item:self.candidates[sid]=(dict(item),now)
                delay={'waiting':.7,'done':.6}.get(item['status'],0)
                if now-self.candidates[sid][1]>=delay:self.stable[sid]=dict(item)
        counts={k:min(9999,sum(v['status']==k for v in self.stable.values())) for k in ('working','waiting','done')}
        waiters={w for v in self.stable.values() if v['status']=='waiting' for w in v['waiters']}
        done={(sid,v['sequence']) for sid,v in self.stable.items() if v['status']=='done'}
        state=next((k for k in ('waiting','working','done') if counts[k]),'idle')
        # PermissionRequest precedes both automatic review and a real dialog.
        # Filter short approval-only phases; explicit questions remain immediate.
        visual={sid:dict(v) for sid,v in raw.items()}
        if not quiet:
            for sid,v in visual.items():
                approval_only=v['status']=='waiting' and v['waiters'] and all('/approval/' in w for w in v['waiters'])
                candidate=self.candidates.get(sid,(None,now))
                if approval_only and now-candidate[1]<.7:v['status']='working'
        visible={k:min(9999,sum(v['status']==k for v in visual.values())) for k in ('working','waiting','done')}
        visible_state=next((k for k in ('waiting','working','done') if visible[k]),'idle')
        summary=dict(state=visible_state,counts=visible,completion_seq=max((v['sequence'] for v in visual.values() if v['status']=='done'),default=0),scope=dict(mode=scope,target=target))
        new_wait=waiters-self.seen_waiters;new_done=done-self.seen_done
        self.seen_waiters=waiters;self.seen_done=done
        if quiet:return summary,None
        if not counts['done']:self.partial_due=None;self.partial_items=set()
        all_done=counts['done']>0 and not counts['working'] and not counts['waiting']
        if new_done:
            self.partial_items.update(new_done)
            if self.partial_due is None:self.partial_due=now+5
        if new_wait:return summary,self.notice('waiting',new_wait)
        if all_done:
            self.partial_due=None;self.partial_items=set()
            return summary,self.notice('all',done) if new_done else None
        if self.partial_due is not None and now>=self.partial_due and not counts['waiting']:
            notice=self.notice('partial',self.partial_items) if counts['working'] and counts['done'] else None
            self.partial_due=None;self.partial_items=set()
            return summary,notice
        return summary,None
