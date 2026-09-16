#!/usr/bin/env python3
"""A stand-in for the ndmspc room router's MCP endpoint.

``macros/builtin/httpRoom.C`` refuses to start outside Kubernetes (it exits unless
KUBERNETES_SERVICE_HOST is set), because a room is a Knative Service. That makes the
real router untestable on a workstation, so this mock answers ``POST /api/mcp`` with
the same JSON-RPC and handler envelopes the router produces, for the room actions, and keeps
its rooms in memory. Alongside list/open/status/close it answers `room_backup` and
`room_restore`, so exporting and restoring a room set can be exercised without a cluster.

It mirrors httpRoom.C / NMcpServer.cxx:

* the handler envelope is ``{"result": "success", "payload": {...}}`` or
  ``{"result": "failure", "error": "..."}`` - the HTTP status stays 200 either way,
  so that envelope is the real signal;
* the MCP result carries ``content[0].text`` (the handler JSON as text),
  ``structuredContent`` (the same JSON) and ``isError`` (true on failure);
* ``initialize`` also returns an ``Mcp-Session-Id`` header, like the real server;
* the ``method`` argument is validated per action (GET/POST for open, GET for
  status and list, DELETE for close), and a missing room id is rejected with the
  same message the router uses;
* a room's resource name is ``ndmspc-room-`` plus a DNS-1123 slug of the room id.

Environment:
  PORT   port to listen on (default 8090)
  HOST   address to bind (default 127.0.0.1)
  SEED   comma-separated room ids to pre-register (default "demo")
  TTL    idle TTL in seconds reported by room/list (default 3600)
  FAIL   1 makes every room action fail, to exercise the client's error path
"""

import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PREFIX = "ndmspc-room-"
PARAM = "room"
PROTOCOL_VERSION = "2025-06-18"

# The tool metadata httpRoom.C registers through RegisterMcpTool, so tools/list is
# faithful to the real router.
TOOLS = [
    {
        "name": "room_open",
        "description": "Ensure a room exists (one Knative Service per room) and return the URL that "
        "serves it. POST/GET with 'room' in the body.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "room": {"type": "string", "description": "Room id (any client-chosen string)."},
                "method": {"type": "string", "enum": ["GET", "POST"], "default": "POST"},
            },
            "additionalProperties": True,
        },
    },
    {
        "name": "room_status",
        "description": "Report whether a room is known to the router and its current revision.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "room": {"type": "string"},
                "method": {"type": "string", "enum": ["GET"], "default": "GET"},
            },
            "additionalProperties": True,
        },
    },
    {
        "name": "room_list",
        "description": "List the rooms the router is currently tracking (with their last-seen time).",
        "inputSchema": {
            "type": "object",
            "properties": {"method": {"type": "string", "enum": ["GET"], "default": "GET"}},
            "additionalProperties": True,
        },
    },
    {
        "name": "room_close",
        "description": "Delete a room's HTTPRoute and Knative Service immediately.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "room": {"type": "string"},
                "method": {"type": "string", "enum": ["DELETE"], "default": "DELETE"},
            },
            "additionalProperties": True,
        },
    },
    {
        "name": "room_backup",
        "description": "Export every tracked room and its session as one JSON document.",
        "inputSchema": {
            "type": "object",
            "properties": {"method": {"type": "string", "enum": ["GET"], "default": "GET"}},
            "additionalProperties": True,
        },
    },
    {
        "name": "room_restore",
        "description": "Ensure every room named in a document from room_backup and replay its session.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "document": {"type": "object", "description": "A document produced by room_backup."},
                "method": {"type": "string", "enum": ["POST"], "default": "POST"},
            },
            "additionalProperties": True,
        },
    },
]


def slug(room_id):
    """Approximate NdmspcRoomSlug: a DNS-1123 label, with an FNV-1a fallback."""
    text = re.sub(r"-{2,}", "-", re.sub(r"[^a-z0-9-]+", "-", room_id.lower())).strip("-")[:63].strip("-")
    if text:
        return text
    digest = 0xCBF29CE484222325
    for byte in room_id.encode():
        digest ^= byte
        digest = (digest * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "r%016x" % digest


class Rooms:
    """The in-memory room registry the real router keeps (and rebuilds from k8s)."""

    def __init__(self):
        self.rooms = {}
        # Sessions, kept apart from the room entries just as the real router keeps them out of
        # room/list. A real deployment gets these from the room reporting its own session; the
        # mock stands in for that, which is enough to exercise backup/restore.
        self.snapshots = {}

    def name(self, room_id):
        return PREFIX + slug(room_id)

    def session(self, room_id):
        return {
            "v": 1,
            "room": room_id,
            "file": "NBinnings01Gaus.root",
            "actions": [{"name": "ngnt/open", "in": {"file": "NBinnings01Gaus.root"}}],
        }

    def open(self, room_id):
        name = self.name(room_id)
        existing = self.rooms.get(name)
        entry = {
            "name": name,
            "room": room_id,
            "revision": (existing or {}).get("revision") or name + "-00001",
            "lastSeen": int(time.time()),
            "ready": True,
            # A freshly opened room has a pod starting; a seeded one rests at zero.
            "replicas": (existing or {}).get("replicas", 1),
            "active": True,
        }
        self.rooms[name] = entry
        self.snapshots.setdefault(name, self.session(room_id))
        return entry

    def seed(self, room_id):
        name = self.name(room_id)
        self.rooms[name] = {
            "name": name,
            "room": room_id,
            "revision": name + "-00001",
            "lastSeen": int(time.time()),
            "ready": True,
            "replicas": 0,
            "active": False,
        }

    def status(self, room_id):
        name = self.name(room_id)
        entry = self.rooms.get(name)
        payload = {"room": room_id, "name": name, "param": PARAM, "tracked": entry is not None}
        if entry is not None:
            payload["revision"] = entry["revision"]
            payload["lastSeen"] = entry["lastSeen"]
        payload["exists"] = entry is not None
        if entry is not None:
            payload["ready"] = entry["ready"]
        return payload

    def close(self, room_id):
        name = self.name(room_id)
        self.rooms.pop(name, None)
        self.snapshots.pop(name, None)
        return {"room": room_id, "name": name}

    def backup(self, ttl):
        """The document the router exports: the rooms plus their sessions, no ROOT data."""
        entries = []
        for name in sorted(self.rooms):
            room = self.rooms[name]
            entry = {
                "room": room["room"],
                "name": room["name"],
                "revision": room["revision"],
                "lastSeen": room["lastSeen"],
            }
            if name in self.snapshots:
                entry["snapshot"] = self.snapshots[name]
            entries.append(entry)
        return {
            "version": 1,
            "createdAt": int(time.time()),
            "router": {"namespace": "default", "prefix": PREFIX, "param": PARAM, "ttl": ttl},
            "rooms": entries,
        }

    def restore(self, document):
        """Additive, like the router: ensure each room, replay its session, delete nothing."""
        restored, failed = [], []
        for entry in document.get("rooms", []):
            room_id = entry.get("room", "")
            if not room_id:
                continue
            room = self.open(room_id)
            if "snapshot" in entry:
                self.snapshots[room["name"]] = entry["snapshot"]
            restored.append({"room": room_id, "name": room["name"], "revision": room["revision"],
                             "session": "restored"})
        return {"restored": restored, "failed": failed}

    def entries(self):
        return [self.rooms[key] for key in sorted(self.rooms)]


def call_tool(server, tool, arguments):
    """Reproduce one room handler from httpRoom.C."""
    method = arguments.get("method", "POST")
    room_id = arguments.get("room", "")

    def failure(message):
        return {"result": "failure", "error": message}

    if server.fail:
        return failure("mock: forced failure (FAIL=1)")

    if tool == "room_list":
        if "GET" not in method:
            return failure("Unsupported HTTP method for room/list")
        return {"result": "success", "payload": {"rooms": server.rooms.entries(), "ttl": server.ttl}}

    # Backup and restore carry no room id: they act on the whole set.
    if tool == "room_backup":
        if "GET" not in method and "POST" not in method:
            return failure("Unsupported HTTP method for room/backup")
        return {"result": "success", "payload": server.rooms.backup(server.ttl)}

    if tool == "room_restore":
        if "POST" not in method:
            return failure("Unsupported HTTP method for room/restore")
        document = arguments.get("document")
        if not isinstance(document, dict) or not isinstance(document.get("rooms"), list):
            return failure("Missing 'rooms' array in the restore document")
        if document.get("version", 1) != 1:
            return failure("Unsupported restore document version %s" % document.get("version"))
        router = document.get("router")
        if isinstance(router, dict):
            param = router.get("param", PARAM)
            prefix = router.get("prefix", PREFIX)
            if param != PARAM or prefix != PREFIX:
                return failure(
                    "The document came from a router configured differently (param '%s', prefix '%s')"
                    % (param, prefix)
                )
        return {"result": "success", "payload": server.rooms.restore(document)}

    if not room_id:
        return failure('Missing room id (send it in the body as {"room": "<id>"})')

    if tool == "room_open":
        if "GET" not in method and "POST" not in method:
            return failure("Unsupported HTTP method for room/open")
        entry = server.rooms.open(room_id)
        return {
            "result": "success",
            "payload": {
                "room": room_id,
                "name": entry["name"],
                "revision": entry["revision"],
                "param": PARAM,
                "url": "?%s=%s" % (PARAM, room_id),
                "ttl": server.ttl,
            },
        }

    if tool == "room_status":
        if "GET" not in method:
            return failure("Unsupported HTTP method for room/status")
        return {"result": "success", "payload": server.rooms.status(room_id)}

    if tool == "room_close":
        if "DELETE" not in method:
            return failure("Unsupported HTTP method for room/close")
        return {"result": "success", "payload": server.rooms.close(room_id)}

    return failure("Unsupported action: " + tool)


def tool_result(handler):
    """Wrap a handler envelope the way NMcpServer::CallTool does."""
    is_error = isinstance(handler, dict) and handler.get("result") == "failure"
    return {
        "content": [{"type": "text", "text": json.dumps(handler)}],
        "structuredContent": handler,
        "isError": is_error,
    }


def rpc_result(message_id, result):
    return {"jsonrpc": "2.0", "id": message_id, "result": result}


def rpc_error(message_id, code, message):
    return {"jsonrpc": "2.0", "id": message_id, "error": {"code": code, "message": message}}


def handle_message(server, message):
    """Mirror NMcpServer::Handle."""
    if not isinstance(message, dict):
        return rpc_error(None, -32600, "Invalid Request")

    has_id = message.get("id") is not None
    message_id = message.get("id")
    method = message.get("method")
    if not isinstance(method, str):
        return rpc_error(message_id, -32600, "Invalid Request") if has_id else None

    params = message.get("params") if isinstance(message.get("params"), dict) else {}

    # Notifications carry no id and never produce a response.
    if not has_id:
        return None

    if method == "initialize":
        return rpc_result(
            message_id,
            {
                "protocolVersion": params.get("protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "ndmspc-room-mock", "version": "1.0.0"},
            },
        )

    if method == "ping":
        return rpc_result(message_id, {})

    if method == "tools/list":
        return rpc_result(message_id, {"tools": TOOLS})

    if method == "tools/call":
        name = params.get("name")
        if not isinstance(name, str):
            return rpc_error(message_id, -32602, "Invalid params: 'name' is required")
        if name not in {tool["name"] for tool in TOOLS}:
            return rpc_error(message_id, -32602, "Unknown tool: " + name)
        arguments = params.get("arguments") if isinstance(params.get("arguments"), dict) else {}
        return rpc_result(message_id, tool_result(call_tool(server, name, arguments)))

    return rpc_error(message_id, -32601, "Method not found: " + method)


class Handler(BaseHTTPRequestHandler):
    server_version = "ndmspc-room-mock/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("mock: " + (fmt % args) + "\n")

    def send_json(self, status, payload, extra_headers=None):
        body = b"" if payload is None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (extra_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_POST(self):
        if self.path.split("?")[0].rstrip("/") != "/api/mcp":
            self.send_json(404, {"error": "not found (the endpoint is /api/mcp)"})
            return

        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length).decode("utf-8", "replace")

        try:
            message = json.loads(raw)
        except ValueError:
            self.send_json(200, rpc_error(None, -32700, "Parse error"))
            return

        response = handle_message(self.server, message)

        # initialize carries a session id, as the real endpoint does.
        headers = {}
        if isinstance(message, dict) and message.get("method") == "initialize":
            headers["Mcp-Session-Id"] = "mock-%d" % int(time.time() * 1000)

        self.send_json(200, response, headers)

    def do_GET(self):
        self.send_json(405, {"error": "use POST /api/mcp"})


def main():
    host = os.environ.get("HOST", "127.0.0.1")
    port = int(os.environ.get("PORT", "8090"))
    ttl = int(os.environ.get("TTL", "3600"))
    fail = os.environ.get("FAIL", "").strip().lower() not in ("", "0", "false", "no", "off")
    seed = [item.strip() for item in os.environ.get("SEED", "demo").split(",") if item.strip()]

    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.rooms = Rooms()
    server.ttl = ttl
    server.fail = fail
    for room_id in seed:
        server.rooms.seed(room_id)

    print(
        "mock room router listening on http://%s:%d/api/mcp (seed=%s ttl=%d fail=%s)"
        % (host, port, ",".join(seed) or "-", ttl, fail),
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
