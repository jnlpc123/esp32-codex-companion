"""Non-policy Codex hook: retain event identity only; never return a decision."""
import json
import sys
import os
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


def main():
    try:
        from companion_host.events import EventStore
        data=Path(os.environ.get('VIBE_COMPANION_DATA',str(Path(__file__).resolve().parent/'runtime')))
        # stdin can contain private tool arguments. Do not persist or log it.
        payload = sys.stdin.buffer.read(1048576)
        event = json.loads(payload)
        if isinstance(event, dict):
            EventStore(data / 'events.sqlite').append(event)
            status=data/'status.json'
            if not status.exists() or time.time()-status.stat().st_mtime>=5:
                from companion_host.bridge import start_hidden
                start_hidden()
    except Exception:
        pass  # A disconnected accessory must never block or alter Codex.
    print('{}')


if __name__ == '__main__':
    main()
