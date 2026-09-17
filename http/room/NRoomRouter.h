#ifndef Ndmspc_NRoomRouter_H
#define Ndmspc_NRoomRouter_H

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NHttpRequest.h"

namespace Ndmspc {

/**
 * @brief The room router's configuration, from the environment.
 *
 *   NDMSPC_ROOM_NAMESPACE      namespace for the room objects        (default: default)
 *   NDMSPC_ROOM_PREFIX         room resource name prefix             (default: ndmspc-room-)
 *   NDMSPC_ROOM_PARAM          query parameter that identifies a room(default: room)
 *   NDMSPC_ROOM_SKELETON       skeleton ConfigMap name               (default: ndmspc-room-skeleton)
 *   NDMSPC_ROOM_URL_BASE       external base URL for the room links  (default: empty -> "?param=id")
 *   NDMSPC_ROOM_IDLE_TTL       idle time before a room is deleted    (default: 1h)
 *   NDMSPC_ROOM_READY_TIMEOUT  how long to wait for a room to be Ready (default: 45s)
 *   NDMSPC_ROOM_MAX_PREPARING  rooms being created at the same time  (default: 4, 0 = no limit)
 *   NDMSPC_ROOM_WAIT           default for room/open's wait flag     (default: true)
 *   NDMSPC_ROOM_TOKEN_FILE     ServiceAccount token                  (default: in-cluster path)
 *   NDMSPC_ROOM_CA_FILE        cluster CA bundle                     (default: in-cluster path)
 *
 * The namespace and the API server come from KUBERNETES_SERVICE_HOST/PORT, which is also what
 * tells the router it is running in a cluster at all.
 */
struct NRoomConfig {
  std::string ns{"default"};                    ///< Namespace the room objects live in
  std::string prefix{"ndmspc-room-"};           ///< Room resource name prefix
  std::string param{"room"};                    ///< Query parameter that identifies a room
  std::string skeleton{"ndmspc-room-skeleton"}; ///< Skeleton ConfigMap name
  std::string urlBase;                          ///< External base URL for the room links
  long        idleTtlSec{3600};                 ///< Idle time before an idle room is deleted, in seconds
  long        readyTimeoutSec{45};              ///< How long to wait for a room to become Ready, in seconds
  int         maxPreparing{4};                  ///< Rooms prepared at the same time (0 = no limit)
  bool        waitDefault{true};                ///< Default for room/open's wait flag
  std::string apiServer;                        ///< In-cluster API server ("" = not in a cluster)
  std::string tokenFile;                        ///< ServiceAccount token
  std::string caFile;                           ///< Cluster CA bundle

  /// @brief Reads every knob above from the environment (with the defaults in this struct).
  static NRoomConfig FromEnv();

  /// @brief Parses "1/0", "true/false", "yes/no", "on/off"; anything else keeps the fallback.
  static bool ParseBool(const std::string & text, bool fallback);

  /// @brief Parses "<n>[smhd]" into seconds (e.g. "1h" -> 3600).
  static long ParseDuration(const std::string & text, long fallback);
};

/**
 * @brief One room as the router tracks it.
 *
 * A room is registered as soon as its creation starts (so room/list and room/status can report it
 * while it is being prepared), not only once it is ready.
 */
struct NRoomState {
  std::string name;     ///< Kubernetes resource name (prefix + slug of the room id)
  std::string value;    ///< The room id, as chosen by the client
  std::string revision; ///< Latest ready Knative revision
  long        lastSeen{0}; ///< Epoch seconds of the last request that touched the room
  std::string snapshot;    ///< Last captured session, replayed when the room wakes

  bool        preparing{false}; ///< An ensure is running for this room right now
  std::string phase;            ///< Where it is: service, ready, route, restore, failed, cancelled
  std::string error;            ///< Why the last creation failed ("" when it did not)
  std::string code;             ///< Stable reason behind `error`: no_capacity, "" (see below)
  long        startedAt{0};     ///< Epoch seconds the creation started (clients show elapsed)
  long        finishedAt{0};    ///< Epoch seconds it ended (0 while it runs)
  std::string session;          ///< "restored" | "live" | "" - what the session replay did
  int         generation{0};    ///< Bumped by every request, so a superseded worker stops
  bool        cancel{false};    ///< Set by room/close, so a worker stops at its next step
};

/**
 * @brief What the router needs from the cluster.
 *
 * The router talks to the Kubernetes API over this one seam, which is what lets a test stand in for
 * a cluster: everything above it (path building, the 404-then-POST apply, the status parsing, the
 * waiting and the failure reasons) is the router's own logic and is exercised against a fake.
 */
class IRoomCluster {
  public:
  virtual ~IRoomCluster() = default;

  /// @brief One Kubernetes API call.
  /// @param method GET/POST/PATCH/DELETE.
  /// @param path API path, e.g. "/apis/serving.knative.dev/v1/namespaces/default/services".
  /// @param body Request body ("" for none).
  /// @param contentType Content-Type of the body.
  virtual NHttpResponse Request(const std::string & method, const std::string & path, const std::string & body = "",
                                const std::string & contentType = "application/json") = 0;
};

/// @brief The real cluster: the in-cluster API server, with the router's ServiceAccount token.
class NRoomClusterClient : public IRoomCluster {
  public:
  /**
   * @brief Constructor.
   * @param config Configuration (API server, namespace and TLS material) for the requests.
   */
  explicit NRoomClusterClient(NRoomConfig config) : fConfig(std::move(config)) {}

  NHttpResponse Request(const std::string & method, const std::string & path, const std::string & body = "",
                        const std::string & contentType = "application/json") override;

  private:
  NRoomConfig fConfig; ///< Configuration (API server, namespace and TLS material) for the requests
};

/**
 * @class NRoomRouter
 * @brief The room router: the always-on entry Service of an NDMSPC on Knative.
 *
 * It creates one Knative Service per room on demand and writes one HTTPRoute per room matching
 * `?<param>=<id>`, pointing straight at that room's revision Service, so steady-state traffic goes
 * gateway -> room and never touches the router again. It is the framework capability behind
 * `ndmspc-server --rooms true` (NDMSPC_ROOMS=1); a server without rooms never touches
 * any of this.
 *
 * ### Actions
 * Registered in the same handler map the macros use (`gNdmspcHttpHandlers`, plus their MCP metadata
 * in `gNdmspcMcpTools`), so they are served both as `/api/<action>` and as MCP tools:
 *
 *   room/open     GET/POST  ensure a room: by default it waits for the room and returns its URL;
 *                           wait=false returns at once with state=preparing and the work continues
 *                           in the background - poll room/status or room/list
 *   room/status   GET       whether a room is known, its revision, and - while it is being created -
 *                           the phase that creation has reached
 *   room/list     GET       the rooms being tracked (including those still preparing, and those
 *                           whose creation failed)
 *   room/close    DELETE    delete a room's HTTPRoute and Knative Service (a creation still running
 *                           is cancelled)
 *   room/state    GET/POST  internal: a room reports its session here and fetches it back when it
 *                           wakes (hidden from the MCP tool list)
 *   room/backup   GET       export every tracked room and its session as one JSON document
 *   room/restore  POST      ensure every room in such a document and replay its session (additive)
 *
 * ### Creating a room, in the background
 * Creating a room means creating a Knative Service, waiting for its first revision, pinning the
 * HTTPRoute and replaying the session - tens of seconds, and minutes when the room has to roll.
 * ROOT's THttpServer serves one request at a time, so doing that on the request thread would freeze
 * the router for its whole duration. `room/open` therefore takes a `wait` flag: true (the default)
 * keeps the blocking answer, false registers the room and returns at once with `state=preparing`,
 * leaving the work to a background thread. A preparing room is listed by room/list and described by
 * room/status with the `phase` it has reached; NDMSPC_ROOM_MAX_PREPARING bounds how many run at
 * once. The worker checks between steps whether the room was closed or superseded, so a slow create
 * cannot outlive the room it belongs to.
 *
 * ### Websockets
 * The router serves no session of its own, so a websocket to this server must name a room it is
 * tracking: `/ws/root.websocket?room=<id>`. Without the parameter, or with a room the router does
 * not know, the upgrade is refused - a client that reaches the router has gone to the wrong
 * endpoint (a connection for a room the router knows is routed to that room's pod by the gateway),
 * and answering it would hand the client an empty session. The refusal reaches the client as a
 * failed handshake; the reason is logged. A server started with `--ws false` (NDMSPC_WS=0) has no
 * websocket endpoint at all and none of this applies; either way room clients are unaffected, since
 * a connection naming a room is routed to that room's pod.
 *
 * ### Why a creation failed
 * A room that could not be created is reported as `state=failed` with the reason in `error`, plus a
 * stable `code` whenever the router can name it:
 *
 *   no_capacity   the cluster cannot place the room's pod. The message is the scheduler's own
 *                 ("0/1 nodes are available: 1 Insufficient cpu"), read from the pod itself - the
 *                 Knative Service only ever says "waiting for a Revision to become ready". It is
 *                 reported as soon as the verdict repeats, not after the whole ready timeout.
 *   name_conflict the room's name is already taken by an object the router did not create (it
 *                 carries no `ndmspc.io/room` label), so that object is left untouched instead of
 *                 being overwritten or deleted. Pick another room id, or free the name.
 *   (empty)       anything else: a failed apply, a roll that never got there, a timeout.
 *
 * Reading pods needs get/list on pods (core) in this namespace. Without that permission nothing
 * breaks - the router falls back to the timeout message - but the reason stays generic.
 *
 * ### What the router will and will not touch
 * A room's resources are the router's own, and both of them carry the `ndmspc.io/room` label: the
 * per-room Knative Service and HTTPRoute it creates, and that label is its ownership test. If the
 * name a room would need is already taken by anything else - above all the router's own entry
 * Service, but equally a Service an operator created - the creation fails with `name_conflict` and
 * the object is left exactly as it was; `room/close` and the idle sweep likewise only ever delete
 * objects carrying the label. Keep the router's own resources out of the room prefix namespace: the
 * devops role names the router `ndmspc-router` while rooms are `ndmspc-room-<id>`, so the two
 * cannot meet in the first place.
 *
 * Kubernetes only: `ndmspc-server --rooms true` refuses at startup, and registering it fails, when
 * KUBERNETES_SERVICE_HOST is unset, since the router needs the in-cluster API to create the per-room
 * Services and HTTPRoutes.
 */
class NRoomRouter {
  public:
  /// @brief Whether a creation replays the room's stored session once the room is up.
  ///
  /// room/restore answers None: it stores the document's snapshot *after* the room is up and replays
  /// it itself, so a replay here would bring back the older session and make the room look in use.
  enum class Replay { Stored, None };

  /// @brief Whether a running creation may carry on, and why not when it may not.
  enum class Abort { None, Cancelled, Superseded, Gone };

  /// @brief The process's router: environment configuration and the real cluster.
  NRoomRouter();

  /// @brief A router over an injected cluster, for tests.
  NRoomRouter(NRoomConfig config, std::shared_ptr<IRoomCluster> cluster);

  /// @brief Stops the background work still running (a creation, a session capture) and waits for it.
  ~NRoomRouter();

  /// @brief The router of this process (one per process: it owns the room registry).
  static NRoomRouter & Instance();

  /**
   * @brief Whether rooms can be served here: the in-cluster Kubernetes API must be configured.
   *
   * Rooms are Knative Services and HTTPRoutes created through the API server named by
   * KUBERNETES_SERVICE_HOST/PORT, so a process without that environment cannot serve them. Call this
   * before doing any work for rooms, so `--rooms` outside a cluster is refused at startup rather
   * than after a server, a macro load or a request has already happened. Register() applies it too.
   *
   * @param reason Set to an actionable message saying why not, when rooms cannot work here.
   * @return kFALSE when this process cannot serve rooms.
   */
  static bool KubernetesAvailable(std::string * reason = nullptr);

  /**
   * @brief Registers the room actions and the websocket policy.
   *
   * @param server The server to apply the websocket policy to; when null, gNHttpServer is used.
   * @return kFALSE when rooms cannot work here (no Kubernetes environment), having logged why.
   */
  bool Register(NHttpServer * server = nullptr);

  // ---------------------------------------------------------------- the actions
  // The handlers are thin adapters over these, so each action's whole behaviour (method check,
  // request parsing, payload) can be exercised without a server.
  /// @brief room/open: ensure a room, waiting for it unless `wait` is false.
  void HandleOpen(const std::string & method, json & in, json & out);
  /// @brief room/status: whether a room is known and where its creation has reached.
  void HandleStatus(const std::string & method, json & in, json & out);
  /// @brief room/list: the rooms being tracked.
  void HandleList(const std::string & method, json & out);
  /// @brief room/close: delete a room, cancelling a creation still running for it.
  void HandleClose(const std::string & method, json & in, json & out);
  /// @brief room/state: a room reports its session, or fetches it back.
  void HandleState(const std::string & method, json & in, json & out);
  /// @brief room/backup: export every tracked room and its session.
  void HandleBackup(const std::string & method, json & out);
  /// @brief room/restore: ensure every room in a document and replay its session.
  void HandleRestore(const std::string & method, json & in, json & out);

  // ---------------------------------------------------------------- the router's own vocabulary
  /// @brief Whether this websocket upgrade may be served here (see the class note).
  bool WsConnect(const std::string & query) const;
  /// @brief Whether the router is tracking a room.
  bool Tracked(const std::string & value) const;
  /// @brief Whether a room's creation is still running.
  bool Preparing(const std::string & value) const;
  /// @brief The number of rooms currently being prepared.
  int  PreparingCount() const;
  /// @brief Adopts the rooms that already exist in the cluster, and expires idle ones.
  void Sweep();
  /// @brief The rooms being tracked, as a snapshot (safe to read without holding anything).
  std::map<std::string, NRoomState> Rooms() const;
  /// @brief The background threads still running (creations and session captures).
  int Workers() const;

  // ---------------------------------------------------------------- rules, exposed for tests
  /// @brief Percent-decodes a query component ('+' becomes a space).
  static std::string UrlDecode(const std::string & in);
  /// @brief Splits a query string ("k=v&k2=v2") into its decoded parameters.
  static std::map<std::string, std::string> ParseQuery(const std::string & query);
  /// @brief Maps an arbitrary room id onto a DNS-1123 label, falling back to a stable hash.
  static std::string Slug(const std::string & value, size_t maxLen);
  /// @brief The Kubernetes name of a room (prefix + slug of its id).
  static std::string RoomName(const NRoomConfig & cfg, const std::string & value);
  /// @brief Whether a cluster object is one of the router's rooms (carries the room label).
  static bool HasRoomLabel(const json & object);
  /// @brief The URL a client is handed for a room: the external base plus the room parameter.
  static std::string ClientUrl(const NRoomConfig & cfg, const std::string & value);
  /// @brief The room parameter of a query string, or "" when it is absent or empty.
  static std::string RoomParameter(const std::string & query, const std::string & param);
  /// @brief The per-room Knative Service object (skeleton spec + the room's own environment).
  static json ServiceObject(const NRoomConfig & cfg, const std::string & name, const std::string & value,
                            const std::string & stateUrl, const json & skeleton);
  /// @brief The per-room HTTPRoute object: the `?<param>=<id>` match, the `/ws` alias, the headers.
  static json RouteObject(const NRoomConfig & cfg, const std::string & name, const std::string & value,
                          const std::string & revision, const json & skeleton);
  /// @brief Whether a request wants to wait for the room: body "wait", query "wait", or the default.
  static bool WaitFlag(const NRoomConfig & cfg, const json & in);
  /// @brief The error a pod the scheduler cannot place produces ("0/1 nodes are available: ...").
  static std::string UnschedulableError(const std::string & reason);

  /// @brief The `code` reported for a room whose pod the cluster cannot schedule.
  static constexpr const char * kNoCapacity = "no_capacity";

  /// @brief The `code` reported when a room's name is taken by something that is not a room.
  static constexpr const char * kNameConflict = "name_conflict";

  private:
  /**
   * @brief One Kubernetes API call (through the injected cluster).
   * @param method GET/POST/PATCH/DELETE.
   * @param path API path, e.g. "/apis/serving.knative.dev/v1/namespaces/default/services".
   * @param body Request body ("" for none).
   * @param contentType Content-Type of the body.
   * @return The API server's response (status -1 when the transport failed, with the reason in body).
   */
  NHttpResponse Request(const std::string & method, const std::string & path, const std::string & body = "",
                        const std::string & contentType = "application/json");

  // API paths.
  /// @brief The Knative Services collection path of this router's namespace.
  std::string SvcCollection() const;
  /**
   * @brief The path of one room's Knative Service.
   * @param name Kubernetes name of the room.
   * @return The API path.
   */
  std::string SvcPath(const std::string & name) const;
  /// @brief The HTTPRoutes collection path of this router's namespace.
  std::string RouteCollection() const;
  /**
   * @brief The path of one room's HTTPRoute.
   * @param name Kubernetes name of the room.
   * @return The API path.
   */
  std::string RoutePath(const std::string & name) const;
  /**
   * @brief The path of one Knative revision.
   * @param name Revision name.
   * @return The API path.
   */
  std::string RevisionPath(const std::string & name) const;
  /// @brief The path of the skeleton ConfigMap.
  std::string SkeletonPath() const;
  /**
   * @brief The in-cluster address of a room's revision.
   * @param revision Knative revision name ("" yields "").
   * @return The revision Service URL, or "" when no revision is known yet.
   */
  std::string RoomBaseUrl(const std::string & revision) const;
  /**
   * @brief This router's own address as a room sees it, for the room to report its session back.
   * @return The router Service URL, or "" when K_SERVICE is unset.
   */
  std::string RouterBaseUrl() const;

  // Cluster operations.
  /**
   * @brief Reads and parses the skeleton ConfigMap ("room-skeleton.json" key).
   * @param out Filled with the skeleton JSON.
   * @param error Filled when it cannot be read or parsed.
   * @return True on success.
   */
  bool Skeleton(json & out, std::string & error);
  /**
   * @brief Creates the object, or merge-patches its spec when it already exists.
   *
   * An object that already holds the name but carries no room label is never touched: the room fails
   * with kNameConflict instead, so a name that happens to be taken cannot be overwritten.
   *
   * @param collection Collection path to POST to.
   * @param item Item path to GET and PATCH.
   * @param object The object to create or patch.
   * @param error Filled when the operation fails.
   * @param code Set to kNameConflict when the name is taken by something that is not a room.
   * @return True on success.
   */
  bool Apply(const std::string & collection, const std::string & item, const json & object, std::string & error,
             std::string & code);
  /**
   * @brief Stores a room's session snapshot on its own Knative Service (as an annotation).
   * @param name Kubernetes name of the room.
   * @param snapshot The encoded snapshot.
   * @param error Filled when the annotation cannot be written.
   * @return True on success.
   */
  bool Annotate(const std::string & name, const std::string & snapshot, std::string & error);
  /**
   * @brief Returns a room's stored session snapshot.
   *
   * Reads the registry first, then falls back to the Service annotation, so a snapshot outlives a
   * router restart.
   *
   * @param name Kubernetes name of the room.
   * @param id The room id (recorded when the snapshot is read back from the cluster).
   * @return The encoded snapshot, or "" when the room has none.
   */
  std::string Snapshot(const std::string & name, const std::string & id);
  /**
   * @brief Remembers a room's session: in the registry, and on its Service so it outlives this process.
   * @param name Kubernetes name of the room.
   * @param id The room id.
   * @param text The encoded snapshot.
   */
  void StoreSnapshot(const std::string & name, const std::string & id, const std::string & text);
  /**
   * @brief Deletes a room's Knative Service and HTTPRoute - only objects that carry the room label.
   * @param name Kubernetes name of the room.
   */
  void Delete(const std::string & name);
  /**
   * @brief The scheduler's own words for a pod it cannot place.
   *
   * Needs read access to pods; when the cluster refuses, it reports no reason and the caller keeps
   * its previous behaviour - a missing permission must not fail a room.
   *
   * @param name Room name (used when no revision exists yet).
   * @param revision Knative revision name ("" falls back to the room's Service).
   * @param reason Filled with the scheduler's message.
   * @return True when a pod of this room is reported unschedulable.
   */
  bool PodUnschedulable(const std::string & name, const std::string & revision, std::string & reason);
  /**
   * @brief Waits for a room's newest revision to be the ready one.
   * @param name Kubernetes name of the room.
   * @param revision Filled with the ready revision name.
   * @param error Filled on failure.
   * @param code Set to kNoCapacity when the pod cannot be scheduled.
   * @return True when the room is ready.
   */
  bool WaitReady(const std::string & name, std::string & revision, std::string & error, std::string & code);

  // Registry.
  /**
   * @brief Marks a room as seen now, resetting its idle TTL.
   * @param name Kubernetes name of the room.
   */
  void Touch(const std::string & name);
  /// @brief Adopts the rooms that already exist in the cluster into the registry (once per process).
  void Adopt();
  /**
   * @brief Cancels a creation still running for a room and deletes it.
   * @param value Room id.
   */
  void CloseRoom(const std::string & value);
  /**
   * @brief The room id of a request: body "room", or the room parameter of its query.
   * @param in Request body.
   * @return The room id, or "" when it is absent.
   */
  std::string RequestId(json & in) const;
  /**
   * @brief Captures a room's session and stores it; runs off the request path.
   * @param name Kubernetes name of the room.
   * @param value Room id.
   * @param revision Knative revision name.
   */
  void CaptureNow(const std::string & name, const std::string & value, const std::string & revision);
  /**
   * @brief Captures a room's session on a background thread.
   * @param name Kubernetes name of the room.
   * @param value Room id.
   * @param revision Knative revision name.
   */
  void Capture(const std::string & name, const std::string & value, const std::string & revision);
  /**
   * @brief Replays a room's stored session into it once it is ready.
   * @param value Room id.
   * @param payload Filled with what the replay did (session, restored or restoreError).
   */
  void ReplaySession(const std::string & value, json & payload);

  // Creation lifecycle.
  /**
   * @brief Whether a running creation may carry on, and why not when it may not.
   * @param name Kubernetes name of the room.
   * @param generation The generation the worker was started with.
   * @return Abort::None when it may carry on.
   */
  Abort CheckRunning(const std::string & name, int generation) const;
  /**
   * @brief Publishes the step a creation is in, for room/list and room/status.
   * @param name Kubernetes name of the room.
   * @param phase The phase it has reached.
   */
  void SetPhase(const std::string & name, const char * phase);
  /**
   * @brief Records how a creation ended and takes the room out of the preparing state.
   * @param name Kubernetes name of the room.
   * @param revision The ready revision ("" on failure).
   * @param session What the session replay did ("" when there was none).
   * @param error Why it failed ("" on success).
   * @param code Stable reason behind error ("" when it has none).
   */
  void EnsureFinish(const std::string & name, const std::string & revision, const std::string & session,
                    const std::string & error, const std::string & code);
  /**
   * @brief Handles a creation that has to stop, leaving nothing behind that it should not.
   * @param name Kubernetes name of the room.
   * @param generation The generation the worker was started with.
   * @return Always false, so callers can `return Stopped(...)`.
   */
  bool Stopped(const std::string & name, int generation);
  /**
   * @brief Creates (or rolls) one room, publishing its progress as it goes.
   *
   * Holds no lock across the Kubernetes waits, and stops as soon as the room is closed or a newer
   * request for it supersedes this one.
   *
   * @param value Room id.
   * @param generation The generation the worker was started with.
   * @param replay Whether the room's stored session is replayed once it is up.
   * @param payload Filled with the room's state and, once it is ready, its URL.
   * @param error Filled when the creation fails.
   * @param code Stable reason behind error ("" when it has none).
   * @return True when the room is ready.
   */
  bool EnsureWorker(const std::string & value, int generation, Replay replay, json & payload, std::string & error,
                    std::string & code);
  /**
   * @brief Starts creating (or rolling) a room, and reports what the client can act on.
   * @param value Room id chosen by the client.
   * @param wait True keeps the blocking answer; false registers the room as preparing and returns at once.
   * @param replay Whether the room's stored session is replayed once it is up.
   * @param payload Receives the room's state and, once it is ready, its URL.
   * @param error Actionable reason when the call itself could not be started.
   * @param code Stable reason behind error ("" when it has none).
   * @return True when the call itself was started (the room may still be preparing).
   */
  bool EnsureStart(const std::string & value, bool wait, Replay replay, json & payload, std::string & error,
                   std::string & code);

  /// @brief A background thread, and how to tell whether it has finished.
  struct Worker {
    std::string                        room;   ///< The room it works on, for reaping
    std::thread                        thread; ///< The thread itself
    std::shared_ptr<std::atomic<bool>> done;   ///< Set as its last act, so it can be joined safely
  };

  /**
   * @brief Starts a worker and hands the router ownership of it.
   * @param room The room it works on, for reaping.
   * @param thread The thread itself.
   * @param done Set as the thread's last act, so it can be joined safely.
   */
  void SpawnWorker(const std::string & room, std::thread thread, std::shared_ptr<std::atomic<bool>> done);
  /**
   * @brief Joins the workers that have finished - and every one of them when `all` is set.
   * @param all Join every worker, not only the finished ones (default: false).
   */
  void ReapWorkers(bool all = false);
  /// @brief Asks every worker to stop and waits for them, when the router goes away.
  void StopWorkers();

  NRoomConfig                       fConfig;         ///< Configuration (from the environment, or injected by a test)
  std::shared_ptr<IRoomCluster>     fCluster;        ///< The cluster the router talks to (the real one, or a test's)
  std::map<std::string, NRoomState> fRooms;          ///< The registry: room name -> its state
  mutable std::mutex                fMutex;          ///< Guards fRooms
  bool                              fAdopted{false}; ///< Whether Adopt() has already run in this process
  std::mutex                        fAdoptMutex;     ///< Guards the one-time adoption of existing rooms
  std::vector<Worker>               fWorkers;        ///< The background threads still running
  mutable std::mutex                fWorkerMutex;    ///< Guards fWorkers
};

} // namespace Ndmspc

#endif
