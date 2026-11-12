#!/usr/bin/env bash
# HAL's AI brain, on this computer (see docs/SETUP.md, "The AI brain").
#   tools/brain.sh start              start it in the background (log: brain/.run/brain.log)
#   tools/brain.sh stop
#   tools/brain.sh status
#   tools/brain.sh ask zero "What do you think of the old model?"    (or: ask desk "...")
#   tools/brain.sh pair [user@host]   share a key with HAL Zero and tell him where the brain is
#                                     (default pi@cardputerzero.local)
set -euo pipefail
cd "$(dirname "$0")/.."
PY=tools/.venv/bin/python
RUN=brain/.run
[ -x "$PY" ] || { echo "Set up the voice tool first: docs/SETUP.md, step 3."; exit 1; }
port() { "$PY" -c 'from brain.config import port_from, read_env; print(port_from(read_env()))'; }
running() { [ -f "$RUN/pid" ] && kill -0 "$(cat "$RUN/pid")" 2>/dev/null; }
healthy() { curl -fs -m 1 "http://127.0.0.1:$1/health" >/dev/null 2>&1; }
case "${1:-}" in
  start)
    if running; then echo "already running (pid $(cat "$RUN/pid"))"; exit 0; fi
    mkdir -p "$RUN"
    p=$(port)
    nohup "$PY" -m brain.server >> "$RUN/brain.log" 2>&1 &
    echo $! > "$RUN/pid"
    for _ in $(seq 100); do
      if healthy "$p"; then grep "listening" "$RUN/brain.log" | tail -n 1; exit 0; fi
      running || break
      sleep 0.2
    done
    echo "HAL's brain didn't start. The end of brain/.run/brain.log:"; tail -n 5 "$RUN/brain.log"; rm -f "$RUN/pid"; exit 1
    ;;
  stop)
    if ! running; then echo "not running"; rm -f "$RUN/pid"; exit 0; fi
    kill "$(cat "$RUN/pid")"
    for _ in $(seq 50); do running || break; sleep 0.1; done
    rm -f "$RUN/pid"; echo "stopped"
    ;;
  status)
    if running && healthy "$(port)"; then echo "running (pid $(cat "$RUN/pid")), port $(port)"; else echo "not running"; exit 1; fi
    ;;
  ask)  shift; exec "$PY" -m brain.ask "$@" ;;
  pair) shift; exec "$PY" -m brain.pair "$@" ;;
  *) sed -n 2,8p "$0"; exit 1 ;;
esac
