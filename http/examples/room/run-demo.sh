#!/usr/bin/env bash
# End-to-end check of ndmspc-room-tui against the mock room router:
#   1. --list sees the seeded room;
#   2. --open creates a room and returns the URL that serves it;
#   3. --status reports the room as tracked and ready;
#   4. the new room appears in --list;
#   5. --close removes it and it is gone from --list;
#   6. a router that fails is reported and the client exits non-zero;
#   7. --open --no-wait answers while the room is still being created, and the room reaches
#      ready on its own while --status and --list show the step it is on;
#   8. a room the cluster cannot schedule is reported with the scheduler's message and the
#      no_capacity code.
#
# Steps 1-5 need no cluster: the room router cannot register outside Kubernetes, so the mock
# in this directory stands in for it.
set -uo pipefail

# `cd` writes the resolved directory to stdout when CDPATH is set and the path is
# relative (CDPATH commonly ends up containing "."), which would be captured here -
# so redirect it.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null && pwd -P)"
PROJECT_DIR="$(readlink -m "$SCRIPT_DIR/../../..")"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8090}"
FAIL_PORT="${FAIL_PORT:-$((PORT + 1))}"
CAP_PORT="${CAP_PORT:-$((PORT + 2))}"
URL="http://$HOST:$PORT"
CAP_URL="http://$HOST:$CAP_PORT"
CLIENT_BIN="${CLIENT_BIN:-$PROJECT_DIR/bin/ndmspc-room-tui}"
ROOM="${ROOM:-mgmtest}"
LOG="${LOG:-$(mktemp "${TMPDIR:-/tmp}/ndmspc-room-demo.XXXXXX.log")}"
FAIL_LOG="${FAIL_LOG:-$(mktemp "${TMPDIR:-/tmp}/ndmspc-room-demo-fail.XXXXXX.log")}"
CAP_LOG="${CAP_LOG:-$(mktemp "${TMPDIR:-/tmp}/ndmspc-room-demo-capacity.XXXXXX.log")}"

die() { echo "error: $*" >&2; exit 1; }

[ -x "$CLIENT_BIN" ] || die "client binary not found: $CLIENT_BIN (run ./scripts/make.sh install)"
command -v python3 >/dev/null 2>&1 || die "python3 is required for the JSON assertions"
# Room ids are slugified into Kubernetes names; keeping the override to that alphabet
# also keeps the expressions assert_json evaluates free of anything but literal text.
[[ "$ROOM" =~ ^[A-Za-z0-9._-]+$ ]] || die "ROOM must match [A-Za-z0-9._-]+ (got '$ROOM')"

mock_pid=""
fail_pid=""
cap_pid=""
cleanup() {
  for pid in "$mock_pid" "$fail_pid" "$cap_pid"; do
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
    [ -n "$pid" ] && wait "$pid" 2>/dev/null
  done
}
trap cleanup EXIT INT TERM

wait_for_mock() {
  local log="$1" pid="$2" ready=0
  for _ in $(seq 1 100); do
    if grep -q "listening on" "$log" 2>/dev/null; then ready=1; break; fi
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
  done
  [ "$ready" = 1 ]
}

fail=0
pass() { echo "OK: $1"; }
problem() { echo "FAIL: $1"; fail=1; }

# Assert a condition over a JSON document: assert_json <json> <description> <python expression on 'd'>
assert_json() {
  local payload="$1" description="$2" expression="$3"
  if python3 -c '
import json, sys
d = json.loads(sys.argv[1])
sys.exit(0 if eval(sys.argv[2]) else 1)
' "$payload" "$expression" 2>/dev/null; then
    pass "$description"
  else
    problem "$description"
    echo "     payload: $payload"
  fi
}

run() { "$CLIENT_BIN" --url "$URL" "$@"; }
run_cap() { "$CLIENT_BIN" --url "$CAP_URL" "$@"; }

echo "Starting the mock room router on $URL (log: $LOG) ..."
HOST="$HOST" PORT="$PORT" SEED=demo TTL=3600 "$SCRIPT_DIR/run-mock-server.sh" >"$LOG" 2>&1 &
mock_pid=$!

if ! wait_for_mock "$LOG" "$mock_pid"; then
  echo "The mock did not start. Output:"
  cat "$LOG"
  echo "RESULT: FAIL"
  exit 1
fi

echo
echo "== 1/10 --list sees the seeded room =="
list_before="$(run --list)"; rc=$?
echo "$list_before"
if [ "$rc" -ne 0 ]; then
  problem "--list exited $rc"
else
  assert_json "$list_before" "seeded room 'demo' is listed" 'any(r["room"] == "demo" for r in d["rooms"])'
  assert_json "$list_before" "the idle TTL is reported" 'd["ttl"] == 3600'
fi

echo
echo "== 2/10 --open ensures a room and returns its URL =="
open_out="$(run --open "$ROOM")"; rc=$?
echo "$open_out"
if [ "$rc" -ne 0 ]; then
  problem "--open exited $rc"
else
  assert_json "$open_out" "opening '$ROOM' returns the room id" "d['room'] == '$ROOM'"
  assert_json "$open_out" "the returned URL carries the room parameter" "d['url'] == '?room=$ROOM'"
  assert_json "$open_out" "the resource name is prefixed" "d['name'] == 'ndmspc-room-$ROOM'"
fi

echo
echo "== 3/10 --status reports the room as tracked and ready =="
status_out="$(run --status "$ROOM")"; rc=$?
echo "$status_out"
if [ "$rc" -ne 0 ]; then
  problem "--status exited $rc"
else
  assert_json "$status_out" "the room is tracked by the router" "d['tracked'] is True"
  assert_json "$status_out" "the room's Service exists and is ready" "d['exists'] is True and d['ready'] is True"
fi

echo
echo "== 4/10 the opened room appears in --list =="
list_after="$(run --list)"; rc=$?
if [ "$rc" -ne 0 ]; then
  problem "--list exited $rc"
else
  assert_json "$list_after" "the opened room is listed" "any(r['room'] == '$ROOM' for r in d['rooms'])"
fi

echo
echo "== 5/10 --close removes the room =="
close_out="$(run --close "$ROOM")"; rc=$?
echo "$close_out"
if [ "$rc" -ne 0 ]; then
  problem "--close exited $rc"
else
  assert_json "$close_out" "close echoes the room" "d['room'] == '$ROOM'"
fi
list_closed="$(run --list)"; rc=$?
if [ "$rc" -ne 0 ]; then
  problem "--list exited $rc"
else
  assert_json "$list_closed" "the closed room is gone" "not any(r['room'] == '$ROOM' for r in d['rooms'])"
fi

echo
echo "== 6/10 --backup writes the rooms and their sessions to a file =="
# A fresh path: --backup refuses to overwrite, so the file must not exist yet (mktemp would
# create it), hence a temporary directory with the file inside it.
BACKUP_FILE="${BACKUP_FILE:-$(mktemp -d "${TMPDIR:-/tmp}/ndmspc-rooms.XXXXXX")/rooms.json}"
run --open "$ROOM" >/dev/null 2>&1     # make sure the room exists, with a session
backup_out="$(run --backup "$BACKUP_FILE" 2>&1)"; rc=$?
echo "$backup_out" | tail -1
if [ "$rc" -ne 0 ]; then
  problem "--backup exited $rc"
elif python3 -c '
import json, sys
document = json.load(open(sys.argv[1]))
rooms = {room["room"]: room for room in document["rooms"]}
room = rooms.get(sys.argv[2])
assert room is not None, "room missing from the document"
assert document["version"] == 1, "unexpected document version"
assert room.get("snapshot", {}).get("actions"), "no session in the document"
' "$BACKUP_FILE" "$ROOM" 2>/dev/null; then
  pass "the file holds '$ROOM' and its session"
else
  problem "the file is missing the room or its session"
  head -c 300 "$BACKUP_FILE"; echo
fi

echo
echo "== 7/10 --restore brings a deleted room back, with its session =="
run --close "$ROOM" >/dev/null 2>&1     # delete it: the file becomes the only copy
if python3 -c '
import json, sys
rooms = [room["room"] for room in json.loads(sys.argv[1])["rooms"]]
sys.exit(1 if sys.argv[2] in rooms else 0)
' "$(run --list)" "$ROOM" 2>/dev/null; then
  restore_out="$(run --restore "$BACKUP_FILE" 2>&1)"; rc=$?
  echo "$restore_out" | tail -1
  if [ "$rc" -ne 0 ]; then
    problem "--restore exited $rc"
  elif ! python3 -c '
import json, sys
rooms = [room["room"] for room in json.loads(sys.argv[1])["rooms"]]
sys.exit(0 if sys.argv[2] in rooms else 1)
' "$(run --list)" "$ROOM" 2>/dev/null; then
    problem "the room did not come back"
  else
    pass "the room is back after the restore"
    run --backup "$BACKUP_FILE" --force >/dev/null 2>&1
    if python3 -c '
import json, sys
rooms = {room["room"]: room for room in json.load(open(sys.argv[1]))["rooms"]}
sys.exit(0 if rooms.get(sys.argv[2], {}).get("snapshot", {}).get("actions") else 1)
' "$BACKUP_FILE" "$ROOM" 2>/dev/null; then
      pass "and it carries its session again"
    else
      problem "the restored room lost its session"
    fi
  fi
else
  problem "the room was still listed after --close"
fi

echo
echo "== 8/10 a failing router is reported, not ignored =="
HOST="$HOST" PORT="$FAIL_PORT" FAIL=1 SEED=demo "$SCRIPT_DIR/run-mock-server.sh" >"$FAIL_LOG" 2>&1 &
fail_pid=$!
if ! wait_for_mock "$FAIL_LOG" "$fail_pid"; then
  problem "the failing mock did not start"
  cat "$FAIL_LOG"
else
  err_out="$("$CLIENT_BIN" --url "http://$HOST:$FAIL_PORT" --list 2>&1)"; rc=$?
  echo "$err_out"
  if [ "$rc" -eq 0 ]; then
    problem "a forced router failure still exited 0"
  elif ! grep -q "forced failure" <<<"$err_out"; then
    problem "the router's error message was not surfaced"
  else
    pass "the failure is surfaced and the exit code is non-zero"
  fi
fi

echo
echo "== 9/10 --open --no-wait returns while the room is still being created =="
# The router creates a room in the background, so a client does not have to hold the router - or
# itself - for the whole wait. The mock models that with a short PREPARE window.
ASYNC_ROOM="${ROOM}async"
# The room id goes straight after --open: CLI11 takes the next token as its value, so
# "--open --no-wait <id>" would read "--no-wait" as the id and reject the room.
async_out="$(run --open "$ASYNC_ROOM" --no-wait)"; rc=$?
echo "$async_out"
if [ "$rc" -ne 0 ]; then
  problem "--open --no-wait exited $rc"
else
  assert_json "$async_out" "--open --no-wait answers while the room is being prepared" \
    "d['state'] == 'preparing'"
  assert_json "$async_out" "and reports the step its creation is on" \
    "d['phase'] in ('service', 'ready', 'route', 'restore')"
fi
assert_json "$(run --status "$ASYNC_ROOM")" "the room is listed as being prepared" \
  "d['state'] == 'preparing' and d['preparing'] is True"

async_status=""
for _ in $(seq 1 20); do
  async_status="$(run --status "$ASYNC_ROOM")"
  if python3 -c '
import json, sys
sys.exit(0 if json.loads(sys.argv[1]).get("state") == "ready" else 1)
' "$async_status" 2>/dev/null; then break; fi
  sleep 0.5
done
assert_json "$async_status" "and becomes ready without anyone waiting on room/open" \
  "d['state'] == 'ready' and d['ready'] is True"
run --close "$ASYNC_ROOM" >/dev/null 2>&1

echo
echo "== 10/10 a room the cluster cannot schedule says so =="
# A router whose cluster has no room for another pod: the reason is the scheduler's own, and the
# room carries the stable no_capacity code next to it.
HOST="$HOST" PORT="$CAP_PORT" NO_ROOM_CAPACITY=1 SEED=demo "$SCRIPT_DIR/run-mock-server.sh" >"$CAP_LOG" 2>&1 &
cap_pid=$!
if ! wait_for_mock "$CAP_LOG" "$cap_pid"; then
  problem "the no-capacity mock did not start"
  cat "$CAP_LOG"
else
  cap_out="$(run_cap --open fullroom 2>&1)"; rc=$?
  echo "$cap_out"
  if [ "$rc" -eq 0 ]; then
    problem "a room that cannot be scheduled still exited 0"
  elif ! grep -q "Insufficient cpu" <<<"$cap_out"; then
    problem "the scheduler's message was not reported"
  else
    pass "--open reports why the room cannot be created"
  fi
  assert_json "$(run_cap --list)" "and the listed room carries the no_capacity code" \
    "[r for r in d['rooms'] if r['room'] == 'fullroom' and r['state'] == 'failed' and r['code'] == 'no_capacity']"
fi

echo
if [ "$fail" -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
exit "$fail"
