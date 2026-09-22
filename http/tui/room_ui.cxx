// The interactive room screen: a live list of the router's rooms with a detail pane,
// an action queue that keeps every HTTP call off the render thread, and dialogs for
// the two mutating actions (open, close).
#include "room_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ftxui_all.hpp"

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NRoomAccess.h"
#include "ndmspc/http/NRoomClient.h"

namespace Ndmspc {

namespace {

using namespace ftxui;

constexpr int kTickMs     = 100; ///< Worker poll interval, ms
constexpr int kChromeRows = 10;  ///< Terminal rows taken by everything but the room table

/// @brief Which overlay is open, if any.
enum class Dialog { None, OpenRoom, ConfirmClose, Help };

/// @brief One queued room action.
struct Action {
  enum class Kind { Refresh, Open, Status, Close };
  Kind        kind{Kind::Refresh};
  std::string room;
};

/// @brief State shared between the worker thread and the render thread.
struct State {
  std::mutex             mutex;
  std::vector<NRoomInfo> rooms;
  int                    ttl{0};
  /// Whether the router answered this session as an admin (NDMSPC_ROOM_ADMINS), which is why the
  /// list may hold rooms that are not its own.
  bool                   admin{false};
  std::string            status;     ///< Last successful action or information
  std::string            error;      ///< Last failure; cleared by the next success
  std::string            detailRoom; ///< The room the cached status payload belongs to
  json                   detail;     ///< Payload of the last room_status call
  bool                   connected{false};
  bool                   busy{false};
  std::string            busyLabel;
  bool                   paused{false};
  std::string            lastUpdated;
  /// Rooms this session asked to create, and whether a failure has been announced for them: a
  /// creation can fail long after the call answered, and the row alone is easy to miss.
  std::map<std::string, bool> asked;
};

/// @brief A lock-free copy of State, so rendering never holds the mutex.
struct Snapshot {
  std::vector<NRoomInfo> rooms;
  int                    ttl{0};
  bool                   admin{false};
  std::string            status;
  std::string            error;
  std::string            detailRoom;
  json                   detail;
  bool                   connected{false};
  bool                   busy{false};
  std::string            busyLabel;
  bool                   paused{false};
  std::string            lastUpdated;
};

/// @brief Copy the shared state under its lock.
Snapshot TakeSnapshot(State & state)
{
  std::lock_guard<std::mutex> lock(state.mutex);
  Snapshot                    copy;
  copy.rooms         = state.rooms;
  copy.ttl           = state.ttl;
  copy.admin         = state.admin;
  copy.status        = state.status;
  copy.error         = state.error;
  copy.detailRoom    = state.detailRoom;
  copy.detail        = state.detail;
  copy.connected     = state.connected;
  copy.busy          = state.busy;
  copy.busyLabel     = state.busyLabel;
  copy.paused        = state.paused;
  copy.lastUpdated   = state.lastUpdated;
  return copy;
}

/// @brief Read a string member without throwing.
std::string StringMember(const json & object, const char * key)
{
  if (!object.is_object() || !object.contains(key)) return {};
  return NUtils::GetJsonString(object[key]);
}

/// @brief Read a bool member without throwing.
bool BoolMember(const json & object, const char * key)
{
  if (!object.is_object() || !object.contains(key)) return false;
  return NUtils::GetJsonBool(object[key]);
}

/// @brief Local wall-clock time as HH:MM:SS.
std::string ClockNow()
{
  const std::time_t now = std::time(nullptr);
  std::tm           tm{};
  localtime_r(&now, &tm);
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
  return buffer;
}

/// @brief Render an epoch timestamp as a short age.
std::string FormatAge(long epochSeconds)
{
  if (epochSeconds <= 0) return "never";
  const long age = static_cast<long>(std::time(nullptr)) - epochSeconds;
  if (age < 0) return "just now";
  if (age < 60) return std::to_string(age) + "s ago";
  if (age < 3600) return std::to_string(age / 60) + "m ago";
  if (age < 86400) return std::to_string(age / 3600) + "h ago";
  return std::to_string(age / 86400) + "d ago";
}

/// @brief Busy-indicator frame.
std::string SpinnerFrame(int frame)
{
  static const char * frames[] = {"|", "/", "-", "\\"};
  return frames[frame % 4];
}

/// @brief Drop a trailing "/api/mcp" and any trailing slashes, to build client URLs.
std::string BaseUrl(const std::string & url)
{
  std::string       base   = url;
  const std::string suffix = "/api/mcp";
  if (base.size() >= suffix.size() &&
      base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
    base.resize(base.size() - suffix.size());
  }
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

/// @brief The room's API endpoint: what an API client points at to talk to the room.
std::string ServingUrl(const std::string & url, const std::string & roomId)
{
  return BaseUrl(url) + "/api?room=" + roomId;
}

/// @brief The page to open the room in a browser: the router's own UI carrying the same
/// ?room=<id> parameter, which the viewer uses to join the room.
std::string PageUrl(const std::string & url, const std::string & roomId)
{
  return BaseUrl(url) + "?room=" + roomId;
}

/// @brief The WebSocket URL a client uses to reach a room served by the router.
///
/// The handshake carries the same ?room=<id> parameter as an HTTP request, which is what
/// keeps the room awake - and, on a cold start, wakes it before the connection is served.
std::string ServingWsUrl(const std::string & url, const std::string & roomId)
{
  std::string base = BaseUrl(url);
  if (base.compare(0, 6, "https:") == 0) {
    base = "wss:" + base.substr(6);
  }
  else if (base.compare(0, 5, "http:") == 0) {
    base = "ws:" + base.substr(5);
  }
  return base + "/ws/root.websocket?room=" + roomId;
}

/// @brief Add the access token that opens a room to a URL built for it (no-op without one).
std::string WithToken(const std::string & url, const std::string & token)
{
  if (token.empty()) return url;
  return url + "&" + NRoomAccess::kParam + "=" + token;
}

/// @brief Whether a failure looks like rejected authentication.
bool IsAuthenticationError(const std::string & error)
{
  return error.find("authentication") != std::string::npos || error.find("HTTP 401") != std::string::npos ||
         error.find("HTTP 403") != std::string::npos;
}

/// @brief Supplies the OIDC bearer token, re-obtaining it once it grows stale.
class TokenProvider {
  public:
  explicit TokenProvider(const NRoomUiOptions & options) : fOptions(options) {}

  /// @return The current token, or "" when OIDC is not configured.
  std::string Get()
  {
    if (!fOptions.oidcEnabled) return {};

    const auto now   = std::chrono::steady_clock::now();
    const bool stale = fToken.empty() ||
                       std::chrono::duration_cast<std::chrono::seconds>(now - fObtainedAt).count() >=
                           fOptions.oidcTokenRefreshSeconds;
    if (!stale) return fToken;

    try {
      NOidcTokenClient client(fOptions.oidc);
      fToken      = client.ObtainAccessToken();
      fObtainedAt = now;
      NLogInfo("Obtained an OIDC access token (%zu bytes)", fToken.size());
    }
    catch (const std::exception & e) {
      NLogError("Could not obtain an OIDC access token: %s", e.what());
      fToken      = {};
      fObtainedAt = now; // do not retry on every single call
    }
    return fToken;
  }

  /// @brief Force a re-obtain on the next Get(), e.g. once a call is rejected.
  void Invalidate()
  {
    fToken      = {};
    fObtainedAt = {};
  }

  private:
  NRoomUiOptions                        fOptions;
  std::string                           fToken;
  std::chrono::steady_clock::time_point fObtainedAt{};
};

/// @brief Owns the room client and performs every call away from the render thread.
class Worker {
  public:
  Worker(const NRoomUiOptions & options, State & state, std::function<void()> wake)
      : fOptions(options), fState(state), fWake(std::move(wake)), fTokens(options)
  {
  }

  void Start() { fThread = std::thread([this] { Loop(); }); }

  void Stop()
  {
    fQuit = true;
    if (fThread.joinable()) fThread.join();
  }

  void Push(Action action)
  {
    {
      std::lock_guard<std::mutex> lock(fQueueMutex);
      fQueue.push_back(std::move(action));
    }
    fWake();
  }

  private:
  void         Loop();
  void         RefreshList();
  void         Run(const Action & action);
  NRoomClient & Client();
  void         SetError(const std::string & message);
  void         SetBusy(bool busy, const std::string & label = {});

  NRoomUiOptions                        fOptions;
  State &                               fState;
  std::function<void()>                 fWake;
  TokenProvider                         fTokens;
  std::mutex                            fQueueMutex;
  std::deque<Action>                    fQueue;
  std::atomic<bool>                     fQuit{false};
  std::thread                           fThread;
  std::unique_ptr<NRoomClient>          fClient;
  std::string                           fTokenInUse;
  std::chrono::steady_clock::time_point fLastRefresh{};
};

void Worker::SetError(const std::string & message)
{
  std::lock_guard<std::mutex> lock(fState.mutex);
  fState.error     = message;
  fState.connected = false;
}

void Worker::SetBusy(bool busy, const std::string & label)
{
  std::lock_guard<std::mutex> lock(fState.mutex);
  fState.busy = busy;
  if (!label.empty()) fState.busyLabel = label;
}

NRoomClient & Worker::Client()
{
  const std::string token = fTokens.Get();
  if (fClient == nullptr || token != fTokenInUse) {
    fTokenInUse = token;
    fClient     = std::make_unique<NRoomClient>(
        fOptions.mcpEndpoint, token,
        std::make_shared<NRoomHttpClientImpl>(fOptions.clientCert, fOptions.clientKey, fOptions.keyPasswordFile,
                                              fOptions.caFile, fOptions.caPath, fOptions.insecure));
  }
  return *fClient;
}

void Worker::RefreshList()
{
  const NRoomListResult list = Client().List();

  std::lock_guard<std::mutex> lock(fState.mutex);
  if (!list.ok) {
    // Keep the last known list on screen: a briefly unreachable router should not wipe
    // what the user was looking at.
    fState.error     = list.error;
    fState.connected = false;
  }
  else {
    fState.rooms     = list.rooms;
    fState.ttl       = list.ttl;
    fState.admin     = list.admin;
    fState.connected = true;
    fState.error.clear();

    // A room this session asked for that could not be created: say why, once. The row shows the
    // state, but the reason belongs where the action itself was reported.
    for (const auto & room : fState.rooms) {
      const auto asked = fState.asked.find(room.room);
      if (asked == fState.asked.end() || asked->second) continue;
      if (room.state == "ready") {
        fState.asked.erase(asked); // nothing to announce for a room that came up
        continue;
      }
      if (room.state != "failed") continue;
      fState.status = "cannot create " + room.room + ": " +
                      (room.error.empty() ? std::string("no reason reported") : room.error);
      asked->second = true;
    }
  }
  fState.lastUpdated = ClockNow();
  fLastRefresh       = std::chrono::steady_clock::now();
}

void Worker::Run(const Action & action)
{
  if (action.kind == Action::Kind::Refresh) {
    RefreshList();
    return;
  }

  if (action.kind == Action::Kind::Open) {
    std::lock_guard<std::mutex> lock(fState.mutex);
    fState.asked.emplace(action.room, false);
  }

  SetBusy(true, action.kind == Action::Kind::Open ? "creating " + action.room
                          : action.kind == Action::Kind::Close ? "deleting " + action.room
                                                               : "reading " + action.room);

  NRoomResult result;
  switch (action.kind) {
  // Not waiting: the router prepares the room in the background and reports it as "preparing",
  // so the screen keeps working and several rooms can be created one after another. The wait the
  // window used to explain is now the row's state.
  case Action::Kind::Open: result = Client().Open(action.room, /*wait=*/false); break;
  case Action::Kind::Status: result = Client().Status(action.room); break;
  case Action::Kind::Close: result = Client().Close(action.room); break;
  case Action::Kind::Refresh: return;
  }

  if (!result.ok) {
    if (IsAuthenticationError(result.error)) fTokens.Invalidate();
    SetError(result.error);
  }
  else {
    std::lock_guard<std::mutex> lock(fState.mutex);
    fState.error.clear();
    switch (action.kind) {
    case Action::Kind::Open:
      // The router creates the room in the background now, so this call answers at once and the
      // room arrives in the list as "preparing"; say which of the two it is.
      fState.status = StringMember(result.payload, "state") == "preparing"
                          ? "preparing " + action.room
                          : "created " + action.room + "  " + StringMember(result.payload, "url");
      break;
    case Action::Kind::Status: {
      fState.detailRoom = action.room;
      fState.detail     = result.payload;
      const std::string state = StringMember(result.payload, "state");
      fState.status = "status for " + action.room + " - " +
                      (state.empty() ? (BoolMember(result.payload, "ready") ? "ready" : "not ready") : state);
      break;
    }
    case Action::Kind::Close:
      if (fState.detailRoom == action.room) {
        fState.detailRoom.clear();
        fState.detail = json();
      }
      fState.status = "deleted " + action.room;
      break;
    case Action::Kind::Refresh: break;
    }
  }

  SetBusy(false);
  RefreshList();
}

void Worker::Loop()
{
  Ndmspc::NLogger::SetThreadName("RoomWorker");

  while (!fQuit) {
    Action action;
    bool   hasAction = false;
    {
      std::lock_guard<std::mutex> lock(fQueueMutex);
      if (!fQueue.empty()) {
        action = std::move(fQueue.front());
        fQueue.pop_front();
        hasAction = true;
      }
    }

    if (hasAction) {
      Run(action);
      fWake();
      continue; // drain the queue before considering a periodic refresh
    }

    bool idle   = false;
    bool paused = false;
    {
      std::lock_guard<std::mutex> lock(fState.mutex);
      idle   = !fState.busy;
      paused = fState.paused;
    }

    // An action in flight keeps fState.busy set for its whole duration, and this loop is inside
    // that action while it runs - so it cannot wake the screen then. RunRoomUi keeps its own
    // ticker for that; this wake only covers what changed here, between actions.
    if (fOptions.refreshSeconds > 0 && idle && !paused &&
        std::chrono::steady_clock::now() - fLastRefresh >= std::chrono::seconds(fOptions.refreshSeconds)) {
      RefreshList();
      fWake();
      continue;
    }

    if (!idle) fWake();
    std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
  }
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------

/// @brief A "name  value" line for the detail pane.
Element Field(const std::string & name, const std::string & value)
{
  return hbox({text(name) | dim | size(WIDTH, EQUAL, 12), text(value) | flex});
}

/// @brief Title, connection state and refresh state.
Element RenderHeader(const Snapshot & state, const NRoomUiOptions & options, int frame)
{
  const Element connection = state.connected ? text("connected") | color(Color::Green)
                                             : text("disconnected") | color(Color::Red);

  Element activity = text("");
  if (state.busy) {
    activity = hbox({text(SpinnerFrame(frame) + " ") | color(Color::Yellow),
                     text(state.busyLabel) | color(Color::Yellow)});
  }

  Element freshness = text(state.lastUpdated.empty() ? "loading..." : "updated " + state.lastUpdated) | dim;
  if (state.paused) freshness = text("paused") | color(Color::Yellow);

  const std::string ttl = state.ttl > 0 ? "   idle TTL " + std::to_string(state.ttl) + "s" : "";
  // The router said it answered as an admin: say so, since the list then holds other people's rooms.
  const std::string admin = state.admin ? "   [admin]" : "";

  return vbox({
      hbox({text("ndmspc-room-tui") | bold, filler(), connection}),
      hbox({text("router: " + BaseUrl(options.serverUrl)) | dim, filler(), activity}),
      hbox({text(std::to_string(state.rooms.size()) + " room(s)") | dim, text(ttl) | dim,
            text(admin) | dim, filler(), freshness}),
  });
}

/// @brief The room table.
Element RenderRoomTable(const Snapshot & state, size_t selected, int rows, int frame)
{
  std::vector<Element> lines;
  lines.push_back(hbox({
      text("ROOM") | bold | size(WIDTH, EQUAL, 22),
      text("OWNER") | bold | size(WIDTH, EQUAL, 20),
      text("STATE") | bold | size(WIDTH, EQUAL, 11),
      text("PODS") | bold | size(WIDTH, EQUAL, 6),
      text("SEEN") | bold,
  }));
  lines.push_back(separatorLight());

  if (state.rooms.empty()) {
    lines.push_back(text(""));
    lines.push_back(text("No rooms yet - press c to create one.") | dim | center);
    return vbox(std::move(lines));
  }

  // Plain rows rather than a scrolling component, so every cell can be styled: the
  // window is moved by hand to keep the selection visible.
  const int    visible = std::max(1, std::max(3, rows));
  const size_t first   = selected >= static_cast<size_t>(visible) ? selected - visible + 1 : 0;
  const size_t last    = std::min(state.rooms.size(), first + static_cast<size_t>(visible));

  if (first > 0) lines.push_back(text("  ^ more") | dim);

  for (size_t i = first; i < last; ++i) {
    const NRoomInfo & room = state.rooms[i];

    Element roomState = text("pending") | color(Color::Yellow);
    Element pods      = text("0") | dim;
    if (room.preparing) {
      // The router is still creating it, and reports which step it has reached.
      roomState = hbox({text(SpinnerFrame(frame) + " "), text("preparing")}) | color(Color::Yellow);
      pods      = text("-") | dim;
    }
    else if (!room.error.empty() || room.state == "failed") {
      // The scheduler refusing the pod is worth naming: "failed" hides the one cause a user can
      // actually do something about.
      roomState = text(room.code == "no_capacity" ? "no capacity" : "failed") | color(Color::Red);
    }
    else if (room.ready) {
      roomState = room.active ? text("active") | color(Color::Green) : text("idle") | dim;
      pods      = room.active ? text(std::to_string(room.replicas)) : text("0") | dim;
    }

    // For a room that is still being created, "seen" is how long its creation has been running.
    const long seenAt = (room.preparing && room.startedAt > 0) ? room.startedAt : room.lastSeen;

    Element row = hbox({
        text(room.room) | size(WIDTH, EQUAL, 22),
        // A room created before ownership existed, or by a caller that identified itself to nobody,
        // has no owner - and that is worth showing as such rather than as an empty cell.
        text(room.owner.empty() ? "-" : room.owner) | size(WIDTH, EQUAL, 20),
        roomState | size(WIDTH, EQUAL, 11),
        pods | size(WIDTH, EQUAL, 6),
        text(FormatAge(seenAt)),
    });
    if (i == selected) row = row | inverted;
    lines.push_back(row);
  }

  if (last < state.rooms.size()) lines.push_back(text("  v more") | dim);

  return vbox(std::move(lines));
}

/// @brief Details of the selected room, plus its cached status payload.
Element RenderDetail(const Snapshot & state, const NRoomUiOptions & options, size_t selected,
                     const std::string & accessLevel)
{
  if (state.rooms.empty()) return vbox({text("Select a room to see its details.") | dim}) | flex;

  const NRoomInfo & room = state.rooms[std::min(selected, state.rooms.size() - 1)];

  std::vector<Element> lines;
  lines.push_back(text(room.room) | bold);
  lines.push_back(separatorLight());
  const std::string stateText = room.state.empty() ? (room.ready ? "ready" : "not ready") : room.state;
  const std::string pods =
      room.preparing ? "-" : (std::to_string(room.replicas) + (room.active ? " (active)" : " (idle)"));

  lines.push_back(Field("resource", room.name));
  lines.push_back(Field("owner", room.owner.empty() ? "-" : room.owner));
  lines.push_back(Field("revision", room.revision.empty() ? "-" : room.revision));
  lines.push_back(Field("state", stateText));
  if (room.preparing) lines.push_back(Field("phase", room.phase.empty() ? "service" : room.phase));
  lines.push_back(Field("pods", pods));
  lines.push_back(Field("last seen", FormatAge(room.lastSeen)));
  if (!room.error.empty()) lines.push_back(Field("error", room.error));
  if (room.code == "no_capacity") {
    lines.push_back(text("free capacity on the cluster, or lower the room's requests") | dim);
  }

  lines.push_back(separatorLight());
  if (room.preparing) lines.push_back(text("still being created - the URLs work once it is ready") | dim);

  // The links carry the token that opens the room, at the level being looked at: one room hands out
  // a read-write link and a read-only one, and which one is copied is the choice made here.
  const std::string level = room.HasAccess() ? accessLevel : std::string();
  const std::string token = room.TokenFor(level);
  if (room.HasAccess()) {
    lines.push_back(Field("access", level == NRoomAccess::kReadOnly ? "read-only (t: read-write)"
                                                                   : "read-write (t: read-only)"));
  }
  const std::string suffix = token.empty() ? std::string() : " (" + level + ")";
  lines.push_back(text("page URL" + suffix) | dim);
  lines.push_back(text(WithToken(PageUrl(options.serverUrl, room.room), token)) | color(Color::Cyan) | flex);
  lines.push_back(text("api URL" + suffix) | dim);
  lines.push_back(text(WithToken(ServingUrl(options.serverUrl, room.room), token)) | color(Color::Cyan) | flex);
  lines.push_back(text("websocket URL" + suffix) | dim);
  lines.push_back(text(WithToken(ServingWsUrl(options.serverUrl, room.room), token)) | color(Color::Cyan) | flex);

  if (state.detailRoom == room.room && state.detail.is_object()) {
    lines.push_back(separatorLight());
    lines.push_back(text("status") | dim);
    for (auto item = state.detail.begin(); item != state.detail.end(); ++item) {
      lines.push_back(
          Field(item.key(), item.value().is_string() ? item.value().get<std::string>() : item.value().dump()));
    }
  }

  return vbox(std::move(lines)) | flex;
}

/// @brief The message line: the last error, or the last success.
Element RenderStatusLine(const Snapshot & state)
{
  if (!state.error.empty()) return text(state.error) | color(Color::Red);
  return text(state.status) | dim;
}

/// @brief The key hints.
Element RenderKeyBar()
{
  return hbox({
      text(" q ") | inverted, text(" quit  "),
      text(" c ") | inverted, text(" create  "),
      text(" d ") | inverted, text(" delete  "),
      text(" enter ") | inverted, text(" status  "),
      text(" t ") | inverted, text(" rw/ro  "),
      text(" r ") | inverted, text(" refresh  "),
      text(" p ") | inverted, text(" pause  "),
      text(" ? ") | inverted, text(" help"),
  });
}

/// @brief A centred dialog box.
Element RenderDialogBox(const std::string & title, const std::vector<Element> & body)
{
  std::vector<Element> lines;
  lines.push_back(text(title) | bold);
  lines.push_back(separatorLight());
  for (const auto & line : body) lines.push_back(line);
  return vbox(std::move(lines)) | border | size(WIDTH, EQUAL, 64) | center;
}

/// @brief What the worker is busy with, phrased for a message ("creating alpha").
std::string BusyPhrase(const Snapshot & state)
{
  return state.busyLabel.empty() ? "another action" : state.busyLabel;
}

/// @brief The open-room dialog, showing the room id typed so far.
///
/// One action runs at a time, so while the worker is busy the dialog names what is still
/// running and says that Enter is unavailable, instead of quietly swallowing the key.
Element RenderOpenDialog(const std::string & input, bool busy, const std::string & busyPhrase)
{
  std::vector<Element> body = {
      text("Room id (any string; a room you create is named after you):"),
      hbox({text("> ") | bold, text(input), text(busy ? " working..." : "_") | dim}),
      text(""),
  };
  if (busy) {
    body.push_back(text("Still running: " + busyPhrase) | color(Color::Yellow));
    body.push_back(text("Enter is unavailable - Esc: cancel") | dim);
  }
  else {
    body.push_back(text("Enter: create    Esc: cancel") | dim);
  }
  return RenderDialogBox("Create a room", body);
}

/// @brief The close-room confirmation dialog.
///
/// Same rule as the create dialog: while another action is in flight the confirmation says so
/// and refuses `y`, rather than queueing a deletion the user cannot see.
Element RenderCloseDialog(const std::string & room, bool busy, bool preparing, const std::string & busyPhrase)
{
  std::vector<Element> body = {
      text("Delete the room '" + room + "'?"),
  };
  // A room can be deleted while it is still being created - the router cancels that creation and
  // deletes what it had made so far, so say it rather than let it look like an ordinary delete.
  if (preparing) body.push_back(text("Its creation is still running and will be cancelled.") | color(Color::Yellow));
  body.push_back(text(""));

  if (busy) {
    body.push_back(text("Still running: " + busyPhrase) | color(Color::Yellow));
    body.push_back(text("y is unavailable - n / Esc: cancel") | dim);
  }
  else {
    body.push_back(text("y: delete    n / Esc: cancel") | dim);
  }
  return RenderDialogBox("Delete a room", body);
}

/// @brief The key help overlay.
Element RenderHelpDialog(const NRoomUiOptions & options)
{
  const std::string refresh =
      options.refreshSeconds > 0 ? std::to_string(options.refreshSeconds) + "s" : "manual only";
  return RenderDialogBox("Keys",
                         {
                             Field("up / down", "move the selection (j / k, PgUp / PgDn, Home / End)"),
                             Field("enter", "refresh the selected room's status"),
                             Field("c / o / a", "create a room"),
                             Field("d / Del", "delete a room"),
                             Field("t", "hand out the read-write or the read-only link"),
                             Field("r", "refresh the room list now"),
                             Field("p", "pause / resume the automatic refresh"),
                             Field("?", "toggle this help"),
                             Field("q / Esc", "quit"),
                             text(""),
                             Field("router", options.serverUrl),
                             Field("mcp", options.mcpEndpoint),
                             Field("refresh", refresh),
                             text(""),
                             text("An idle room keeps its Service but runs no pods, so 'idle' with 0 pods is "
                                  "normal.") |
                                 dim,
                             text("A room left untouched for the idle TTL is deleted by the router.") | dim,
                             text("A room is created in the background: it appears as 'preparing', with the") | dim,
                             text("step the router is on, until it is ready to use.") | dim,
                         });
}

} // namespace

int RunRoomUi(const NRoomUiOptions & options)
{
  // The screen owns the terminal; a console log line would corrupt it.
  Ndmspc::NLogger::SetConsoleOutput(false);
  Ndmspc::NLogger::SetProcessName("ndmspc-room-tui");

  State state;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    // "busy" means an action is in flight and gates both the periodic refresh and the
    // dialogs' submit, so it must not be set here: the worker's first iteration loads
    // the room list immediately on its own.
    state.status = "connecting to the router...";
  }

  auto              screen = ScreenInteractive::Fullscreen();
  std::atomic<bool> running{true};

  Worker worker(options, state, [&] {
    if (running) screen.PostEvent(Event::Custom);
  });

  size_t      selected = 0;
  Dialog      dialog   = Dialog::None;
  std::string input;
  /// Which link the detail pane shows: a room hands out a read-write and a read-only one.
  std::string accessLevel = NRoomAccess::kReadWrite;
  int         frame = 0;
  worker.Start();

  // The worker runs an action on its own loop and is *inside* it until it answers, so it cannot
  // wake the screen for the duration. Tick the screen while an action is in flight - and while any
  // room is still being created, whose row has to keep moving - rather than leaving the display
  // frozen until the next refresh; PostEvent is safe from another thread, which is how the worker
  // wakes it too.
  std::thread ticker([&] {
    while (running) {
      bool animate = false;
      {
        std::lock_guard<std::mutex> lock(state.mutex);
        animate = state.busy;
        for (const auto & room : state.rooms) {
          animate = animate || room.preparing;
        }
      }
      if (animate) screen.PostEvent(Event::Custom);
      std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }
  });

  auto renderer = Renderer([&] {
    ++frame;
    const Snapshot snapshot = TakeSnapshot(state);

    if (!snapshot.rooms.empty() && selected >= snapshot.rooms.size()) selected = snapshot.rooms.size() - 1;

    const int height = Terminal::Size().dimy;
    const int rows   = std::max(3, height - kChromeRows);

    auto main = vbox({
        RenderHeader(snapshot, options, frame),
        separator(),
        hbox({RenderRoomTable(snapshot, selected, rows, frame) | size(WIDTH, EQUAL, 48), separator(),
              RenderDetail(snapshot, options, selected, accessLevel)}) |
            flex,
        separator(),
        RenderStatusLine(snapshot),
        RenderKeyBar(),
    }) | border;

    const auto selectedRoom = [&]() -> std::string {
      if (snapshot.rooms.empty()) return {};
      return snapshot.rooms[std::min(selected, snapshot.rooms.size() - 1)].room;
    };
    const auto selectedPreparing = [&]() -> bool {
      if (snapshot.rooms.empty()) return false;
      return snapshot.rooms[std::min(selected, snapshot.rooms.size() - 1)].preparing;
    };

    switch (dialog) {
    case Dialog::OpenRoom: return dbox({main, RenderOpenDialog(input, snapshot.busy, BusyPhrase(snapshot))});
    case Dialog::ConfirmClose:
      return dbox({main, RenderCloseDialog(selectedRoom(), snapshot.busy, selectedPreparing(), BusyPhrase(snapshot))});
    case Dialog::Help: return dbox({main, RenderHelpDialog(options)});
    case Dialog::None: break;
    }
    return main;
  });

  auto component = CatchEvent(renderer, [&](const Event & event) {
    const Snapshot snapshot = TakeSnapshot(state);
    const auto     room     = [&]() -> std::string {
      if (snapshot.rooms.empty()) return {};
      return snapshot.rooms[std::min(selected, snapshot.rooms.size() - 1)].room;
    };
    // One action runs at a time, so a key that would start one is refused while another is
    // in flight - saying what is still running, rather than swallowing the key.
    const auto refuseWhileBusy = [&]() {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.status = "Still running: " + BusyPhrase(snapshot) + " - wait until it finishes";
    };

    // A dialog owns the keyboard while it is open.
    if (dialog == Dialog::OpenRoom) {
      if (event == Event::Escape) {
        dialog = Dialog::None;
        input.clear();
        return true;
      }
      if (event == Event::Backspace) {
        if (!input.empty()) input.pop_back();
        return true;
      }
      if (event == Event::Return) {
        if (!input.empty() && !snapshot.busy) {
          worker.Push({Action::Kind::Open, input});
          dialog = Dialog::None;
          input.clear();
        }
        return true;
      }
      if (event.is_character()) {
        input += event.character();
        return true;
      }
      return true; // swallow everything while typing
    }

    if (dialog == Dialog::ConfirmClose) {
      if (event == Event::Escape || event == Event::Character('n')) {
        dialog = Dialog::None;
        return true;
      }
      if (event == Event::Character('y') && !room().empty() && !snapshot.busy) {
        worker.Push({Action::Kind::Close, room()});
        dialog = Dialog::None;
        return true;
      }
      return true;
    }

    if (dialog == Dialog::Help) {
      if (event == Event::Escape || event == Event::Character('?') || event == Event::Character('q')) {
        dialog = Dialog::None;
      }
      return true;
    }

    if (event == Event::Character('q') || event == Event::Escape) {
      running = false;
      screen.Exit();
      return true;
    }
    if (event == Event::Character('?')) {
      dialog = Dialog::Help;
      return true;
    }
    if (event == Event::Character('p')) {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.paused = !state.paused;
      state.status = state.paused ? "automatic refresh paused" : "automatic refresh resumed";
      return true;
    }
    if (event == Event::Character('t')) {
      // Which link the pane shows: the level is a property of the token being handed out, so this
      // only changes what is displayed - it does not touch the room.
      const bool readOnly = accessLevel == NRoomAccess::kReadOnly;
      accessLevel         = readOnly ? NRoomAccess::kReadWrite : NRoomAccess::kReadOnly;
      std::lock_guard<std::mutex> lock(state.mutex);
      state.status = readOnly ? "showing the read-write link" : "showing the read-only link";
      return true;
    }
    if (event == Event::Character('r')) {
      if (snapshot.busy) {
        refuseWhileBusy();
        return true;
      }
      worker.Push({Action::Kind::Refresh, ""});
      return true;
    }
    if (event == Event::Character('c') || event == Event::Character('o') || event == Event::Character('a')) {
      dialog = Dialog::OpenRoom;
      input.clear();
      return true;
    }
    if ((event == Event::Character('d') || event == Event::Delete) && !snapshot.rooms.empty()) {
      dialog = Dialog::ConfirmClose;
      return true;
    }
    if (event == Event::Return && !snapshot.rooms.empty()) {
      if (snapshot.busy) {
        refuseWhileBusy();
        return true;
      }
      worker.Push({Action::Kind::Status, room()});
      return true;
    }

    if (snapshot.rooms.empty()) return false;

    const size_t last = snapshot.rooms.size() - 1;
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (selected > 0) --selected;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      if (selected < last) ++selected;
      return true;
    }
    if (event == Event::Home) {
      selected = 0;
      return true;
    }
    if (event == Event::End) {
      selected = last;
      return true;
    }
    const auto page = static_cast<size_t>(std::max(1, Terminal::Size().dimy - kChromeRows));
    if (event == Event::PageUp) {
      selected = selected > page ? selected - page : 0;
      return true;
    }
    if (event == Event::PageDown) {
      selected = std::min(last, selected + page);
      return true;
    }
    return false;
  });

  screen.Loop(component);

  running = false;
  if (ticker.joinable()) ticker.join();
  worker.Stop();
  return 0;
}

} // namespace Ndmspc
