# NDMSPC

A ROOT framework extension for N-dimensional storage, analysis, and visualization of high-dimensional data — designed for HEP, applicable to any domain.

## Documentation

The main documentation is hosted at:

<https://ndmspc.gitlab.io/docs/>

Please use the docs site for installation, usage guides, architecture, and examples.

## Run locally

```bash
ndmspc-server start ngnt             # http://localhost:8080
```

or straight from the container:

```bash
podman run --rm -p 8080:8080 registry.gitlab.com/ndmspc/ndmspc/base:next
```

Without `-m`, the server loads the built-in macros (`httpNgntBase.C`,
`httpNgnt.C`, `httpRoom.C`). Point a browser at <http://localhost:8080/> for the
web UI, or use the API at <http://localhost:8080/api/>.

## Run on a local kind cluster

The companion repo [ndmspc/devops](https://gitlab.com/ndmspc/devops) brings up the
whole stack — kind cluster + local registry, Knative Serving, the Istio Gateway API
ingress and the ndmspc Service — from this checkout's container image:

```bash
git clone https://gitlab.com/ndmspc/devops.git
cd devops
ansible-playbook playbooks/local.yml --tags install \
    -e with_cluster=kind -e with_knative=true -e with_ndmspc=true
```

The run prints the external URL, e.g. `http://ndmspc.127.0.0.1.sslip.io:8009/`.

### Deploying an image built from this checkout

```bash
# 1. build and publish it to the kind local registry (localhost:5001)
podman build -f ci/Dockerfile.base -t localhost:5001/ndmspc/base:dev .
podman push --tls-verify=false localhost:5001/ndmspc/base:dev

# 2. point the deployment at it (rolls a new Knative revision)
ansible-playbook playbooks/local.yml --tags apply \
    -e with_cluster=kind -e with_knative=true -e with_ndmspc=true \
    -e ndmspc_image=localhost:5001/ndmspc/base:dev
```

### Only the Knative Service

If Knative Serving and a reachable ingress are already there, a plain Service is
enough — the image's entrypoint runs `ndmspc-server start ngnt` for you:

```yaml
# ndmspc-ngnt.yaml
apiVersion: serving.knative.dev/v1
kind: Service
metadata:
  name: ndmspc-ngnt
  namespace: default
spec:
  template:
    spec:
      containerConcurrency: 1
      containers:
        - image: registry.gitlab.com/ndmspc/ndmspc/base:next
          ports:
            - containerPort: 8080
```

```bash
kubectl apply -f ndmspc-ngnt.yaml
kubectl get ksvc ndmspc-ngnt -o jsonpath='{.status.url}{"\n"}'
curl "$(kubectl get ksvc ndmspc-ngnt -o jsonpath='{.status.url}')/api/"
```

`containerConcurrency: 1` keeps one server instance to one concurrent request;
raise it when an instance should serve several users (see *Rooms*).

## Rooms

`httpRoom.C` turns the Service into a **room router**: one Knative Service per
room, created on demand, so the users of a room share one instance. Clients carry
their room in a query parameter (`?room=<id>`) and the gateway routes those
requests — HTTP and WebSocket alike — straight to that room.

```bash
BASE=http://ndmspc.127.0.0.1.sslip.io:8009     # the deployment's external URL

# add (or re-use) a room; returns the URL to send its clients to
curl -s -X POST   -H 'Content-Type: application/json' -d '{"room":"abcd123"}' "$BASE/api/room/open"
# list the rooms the router is tracking, with their live state
curl -s "$BASE/api/room/list"
# state of one room
curl -s -X GET    -H 'Content-Type: application/json' -d '{"room":"abcd123"}' "$BASE/api/room/status"
# delete a room
curl -s -X DELETE -H 'Content-Type: application/json' -d '{"room":"abcd123"}' "$BASE/api/room/close"
# export every room and its session to a file
curl -s "$BASE/api/room/backup" > rooms.json
# restore them from that file, creating any room that is missing
curl -s -X POST -H 'Content-Type: application/json' --data-binary @rooms.json "$BASE/api/room/restore"
```

The room id goes in the JSON body, not the query string — a `?room=` call is
routed to that room (once it exists) instead of the router. Rooms are created with
`min-scale 0`, so an idle room keeps its Service but runs no pods (`"replicas": 0`
in `room/list`); the next `?room=` request starts it again. A room left untouched
for `NDMSPC_ROOM_IDLE_TTL` (default `1h`) is deleted automatically.

The same actions are exposed as MCP tools (`room_open`, `room_list`,
`room_status`, `room_close`, `room_backup`, `room_restore`) through `ndmspc-mcp` or
`POST /api/mcp`.

`ndmspc-room-tui` drives them from a terminal — the same binary also runs a single action
and prints JSON when given `--list`, `--open`, `--status`, `--close`, `--backup <file>` or
`--restore <file>`:

```bash
ndmspc-room-tui --url "$BASE"          # interactive room UI
ndmspc-room-tui --url "$BASE" --list   # scripted
```

Opening a room (`room/open`) also replays the session it had before it scaled to zero — the
same file, navigator and drill-down — so an idle room comes back as it was left rather than
empty. See [the session restore notes](http/README.md) for the details and the one case it
does not cover.

The router needs read/write access to Knative `services` and Gateway API
`httproutes` (and read on `revisions`) — its ServiceAccount, the room-skeleton
ConfigMap and the `ndmspc_room_router_*` settings are managed by the `ndmspc` role
in [ndmspc/devops](https://gitlab.com/ndmspc/devops).
