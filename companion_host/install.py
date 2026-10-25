"""Prepare/install event observers; deliberately does not grant hook trust."""
import argparse
import copy
import datetime
import json
from pathlib import Path
import sys

EVENTS = ('SessionStart', 'UserPromptSubmit', 'PreToolUse', 'PermissionRequest',
          'PostToolUse', 'Stop', 'Interrupt', 'SessionEnd')
MARKER = '--vibe-companion'


def quote_ps(value):
    return "'" + str(value).replace("'", "''") + "'"


def definitions(python, script):
    command = '& ' + quote_ps(python) + ' ' + quote_ps(script) + ' ' + MARKER
    return {name: [dict(hooks=[dict(type='command', command=command, timeout=2)])] for name in EVENTS}


def merge(existing, own):
    result = copy.deepcopy(existing)
    if not isinstance(result, dict) or not isinstance(result.get('hooks', {}), dict):
        raise ValueError('Existing hooks file has an unsupported shape; leaving it untouched')
    hooks = result.setdefault('hooks', {})
    for name, groups in own.items():
        previous = hooks.get(name, [])
        if not isinstance(previous, list):
            raise ValueError('Existing event hook groups must be arrays')
        keep = []
        for group in previous:
            group = copy.deepcopy(group)
            if not isinstance(group, dict) or not isinstance(group.get('hooks', []), list):
                raise ValueError('Existing hook group has an unsupported shape')
            group['hooks'] = [h for h in group.get('hooks', []) if MARKER not in str(h.get('command', ''))]
            if group['hooks']:
                keep.append(group)
        hooks[name] = keep + groups
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--install', action='store_true', help='Merge into user hooks.json; native trust review is still required')
    p.add_argument('--destination', type=Path, default=Path.home() / '.codex' / 'hooks.json')
    args = p.parse_args()
    here = Path(__file__).resolve().parent
    own = definitions(sys.executable, here / 'hook.py')
    existing = json.loads(args.destination.read_text(encoding='utf-8-sig')) if args.destination.exists() else {}
    result = merge(existing, own)
    preview = here / 'review-hooks.json'
    preview.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf8')
    print('Reviewable hook definition:', preview)
    if args.install:
        args.destination.parent.mkdir(parents=True, exist_ok=True)
        if args.destination.exists():
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
            args.destination.with_name(args.destination.name + '.vibe-backup-' + stamp).write_bytes(args.destination.read_bytes())
        temporary = args.destination.with_name(args.destination.name + '.vibe-tmp')
        temporary.write_bytes(preview.read_bytes())
        temporary.replace(args.destination)
        print('Installed observers. Review and trust their definition in Codex; no trust settings were changed.')


if __name__ == '__main__':
    main()
