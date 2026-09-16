// The interactive room screen: a live list of the router's rooms with a detail pane,
// an action queue that keeps every HTTP call off the render thread, and dialogs for
// the two mutating actions (open, close).
#include "room_mgm_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ftxui_all.hpp"

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
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
  std::string            status;     ///< Last successful action or information
  std::string            error;      ///< Last failure; cleared by the next success
  std::string            detailRoom; ///< The room the cached status payload belongs to
  json                   detail;     ///< Payload of the last room_status call
  bool                   connected{false};
  bool                   busy{false};
  std::string            busyLabel;
  bool                   paused{false};
  std::string            lastUpdated;
};

/// @brief A lock-free copy of State, so rendering never holds the mutex.
struct Snapshot {
  std::vector<NRoomInfo> rooms;
  int                    ttl{0};
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
  copy.rooms       = state.rooms;
  copy.ttl         = state.ttl;
  copy.status      = state.status;
  copy.error       = state.error;
  copy.detailRoom  = state.detailRoom;
  copy.detail      = state.detail;
  copy.connected   = state.connected;
  copy.busy        = state.busy;
  copy.busyLabel   = state.busyLabel;
  copy.paused      = state.paused;
  copy.lastUpdated = state.lastUpdated;
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

/// @brief The URL a client uses to reach a room served by the router.
std::string ServingUrl(const std::string & url, const std::string & roomId)
{
  return BaseUrl(url) + "/?room=" + roomId;
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
    fState.connected = true;
    fState.error.clear();
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

  SetBusy(true, action.kind == Action::Kind::Open ? "opening " + action.room
                          : action.kind == Action::Kind::Close ? "closing " + action.room
                                                               : "reading " + action.room);

  NRoomResult result;
  switch (action.kind) {
  case Action::Kind::Open: result = Client().Open(action.room); break;
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
      fState.status = "opened " + action.room + "  " + StringMember(result.payload, "url");
      break;
    case Action::Kind::Status:
      fState.detailRoom = action.room;
      fState.detail     = result.payload;
      fState.status = "status for " + action.room + (BoolMember(result.payload, "ready") ? " - ready"
                                                                                        : " - not ready");
      break;
    case Action::Kind::Close:
      if (fState.detailRoom == action.room) {
        fState.detailRoom.clear();
        fState.detail = json();
      }
      fState.status = "closed " + action.room;
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

    // A long call keeps fState.busy set; the wake below is what animates the spinner.
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

  return vbox({
      hbox({text("ndmspc-room-tui") | bold, filler(), connection}),
      hbox({text("router: " + BaseUrl(options.serverUrl)) | dim, filler(), activity}),
      hbox({text(std::to_string(state.rooms.size()) + " room(s)") | dim, text(ttl) | dim, filler(), freshness}),
  });
}

/// @brief The room table.
Element RenderRoomTable(const Snapshot & state, size_t selected, int rows)
{
  std::vector<Element> lines;
  lines.push_back(hbox({
      text("ROOM") | bold | size(WIDTH, EQUAL, 22),
      text("STATE") | bold | size(WIDTH, EQUAL, 9),
      text("PODS") | bold | size(WIDTH, EQUAL, 6),
      text("SEEN") | bold,
  }));
  lines.push_back(separatorLight());

  if (state.rooms.empty()) {
    lines.push_back(text(""));
    lines.push_back(text("No rooms yet - press o to open one.") | dim | center);
    return vbox(std::move(lines));
  }

  // Plain rows rather than a scrolling component, so every cell can be styled: the
  // window is moved by hand to keep the selection visible.
  const int    visible = std::max(3, rows);
  const size_t first   = selected >= static_cast<size_t>(visible) ? selected - visible + 1 : 0;
  const size_t last    = std::min(state.rooms.size(), first + static_cast<size_t>(visible));

  if (first > 0) lines.push_back(text("  ^ more") | dim);

  for (size_t i = first; i < last; ++i) {
    const NRoomInfo & room = state.rooms[i];

    Element roomState = text("pending") | color(Color::Yellow);
    if (room.ready) {
      roomState = room.active ? text("active") | color(Color::Green) : text("idle") | dim;
    }
    const Element pods = room.active ? text(std::to_string(room.replicas)) : text("0") | dim;

    Element row = hbox({
        text(room.room) | size(WIDTH, EQUAL, 22),
        roomState | size(WIDTH, EQUAL, 9),
        pods | size(WIDTH, EQUAL, 6),
        text(FormatAge(room.lastSeen)),
    });
    if (i == selected) row = row | inverted;
    lines.push_back(row);
  }

  if (last < state.rooms.size()) lines.push_back(text("  v more") | dim);

  return vbox(std::move(lines));
}

/// @brief Details of the selected room, plus its cached status payload.
Element RenderDetail(const Snapshot & state, const NRoomUiOptions & options, size_t selected)
{
  if (state.rooms.empty()) return vbox({text("Select a room to see its details.") | dim}) | flex;

  const NRoomInfo & room = state.rooms[std::min(selected, state.rooms.size() - 1)];

  std::vector<Element> lines;
  lines.push_back(text(room.room) | bold);
  lines.push_back(separatorLight());
  lines.push_back(Field("resource", room.name));
  lines.push_back(Field("revision", room.revision.empty() ? "-" : room.revision));
  lines.push_back(Field("state", room.ready ? "ready" : "not ready"));
  lines.push_back(Field("pods", std::to_string(room.replicas) + (room.active ? " (active)" : " (idle)")));
  lines.push_back(Field("last seen", FormatAge(room.lastSeen)));

  lines.push_back(separatorLight());
  lines.push_back(text("client URL") | dim);
  lines.push_back(text(ServingUrl(options.serverUrl, room.room)) | color(Color::Cyan) | flex);

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
      text(" o ") | inverted, text(" open  "),
      text(" d ") | inverted, text(" close  "),
      text(" enter ") | inverted, text(" status  "),
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

/// @brief The open-room dialog, showing the room id typed so far.
Element RenderOpenDialog(const std::string & input, bool busy)
{
  return RenderDialogBox("Open a room",
                         {
                             text("Room id (any string; the router creates one Knative Service for it):"),
                             hbox({text("> ") | bold, text(input), text(busy ? " working..." : "_") | dim}),
                             text(""),
                             text("Enter: open    Esc: cancel") | dim,
                         });
}

/// @brief The close-room confirmation dialog.
Element RenderCloseDialog(const std::string & room, bool busy)
{
  return RenderDialogBox("Close a room",
                         {
                             text("Delete the room '" + room + "' and its Knative Service?"),
                             text(""),
                             text(busy ? "working..." : "y: close    n / Esc: cancel") | dim,
                         });
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
                             Field("o / a", "open (create) a room"),
                             Field("d / Del", "close (delete) a room"),
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
  int         frame = 0;

  worker.Start();

  auto renderer = Renderer([&] {
    ++frame;
    const Snapshot snapshot = TakeSnapshot(state);

    if (!snapshot.rooms.empty() && selected >= snapshot.rooms.size()) selected = snapshot.rooms.size() - 1;

    const int height = Terminal::Size().dimy;
    const int rows   = std::max(3, height - kChromeRows);

    auto main = vbox({
        RenderHeader(snapshot, options, frame),
        separator(),
        hbox({RenderRoomTable(snapshot, selected, rows) | size(WIDTH, EQUAL, 46), separator(),
              RenderDetail(snapshot, options, selected)}) |
            flex,
        separator(),
        RenderStatusLine(snapshot),
        RenderKeyBar(),
    }) | border;

    const auto selectedRoom = [&]() -> std::string {
      if (snapshot.rooms.empty()) return {};
      return snapshot.rooms[std::min(selected, snapshot.rooms.size() - 1)].room;
    };

    switch (dialog) {
    case Dialog::OpenRoom: return dbox({main, RenderOpenDialog(input, snapshot.busy)});
    case Dialog::ConfirmClose: return dbox({main, RenderCloseDialog(selectedRoom(), snapshot.busy)});
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
      if (event == Event::Character('y') && !room().empty()) {
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
    if (event == Event::Character('r')) {
      worker.Push({Action::Kind::Refresh, ""});
      return true;
    }
    if (event == Event::Character('o') || event == Event::Character('a')) {
      dialog = Dialog::OpenRoom;
      input.clear();
      return true;
    }
    if ((event == Event::Character('d') || event == Event::Delete) && !snapshot.rooms.empty()) {
      dialog = Dialog::ConfirmClose;
      return true;
    }
    if (event == Event::Return && !snapshot.rooms.empty()) {
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
  worker.Stop();
  return 0;
}

} // namespace Ndmspc
