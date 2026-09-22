#ifndef Ndmspc_NRoomSession_H
#define Ndmspc_NRoomSession_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h"      ///< provides the global `json` (nlohmann) alias
#include "ndmspc/http/NHttpRequest.h" ///< NHttpRequest, NHttpResponse

namespace Ndmspc {

/**
 * @class NRoomSession
 * @brief Captures and replays the session of a room server.
 *
 * A room is a Knative Service created with min-scale 0, so an idle room has no process
 * at all and everything it held is gone. A "session" is the three things worth bringing
 * back: the opened ROOT file, the navigator (binning and levels, rebuilt by
 * `ngnt/reshape`) and the drill-down state point.
 *
 * All three are read back from the room's own API and stored as a small, replayable
 * snapshot: the route names and the verbatim request bodies that produced them. Restoring
 * means posting those bodies again and then re-applying the state point, which no POST can
 * set - it is only ever written by a `PATCH` to `ngnt/map` or `ngnt/spectra`.
 *
 * Two rules keep this safe, and both are load-bearing:
 *
 * - **Capture never returns an empty snapshot.** A room that has no file open reports
 *   nothing, so a freshly started pod cannot overwrite a good snapshot with emptiness -
 *   which would otherwise happen on every wake, since a room is empty for the first
 *   seconds of its life.
 * - **Restore never runs over a live session.** Callers must check Probe() first; a room
 *   that already has a file open wins, and its snapshot is left alone.
 *
 * Restoring the state point is verified by reading it back instead of trusting the reply:
 * the room's PATCH also tries to render the projection and answers with an error
 * ("No entry and no projection found...") in a room that has just started, even though the
 * point itself is stored.
 *
 * The class never throws: failures come back as an error string.
 */
class NRoomSession {
  public:
  /// @brief Largest accepted encoded snapshot; an annotation has to stay small.
  static constexpr std::size_t kMaxEncodedBytes = 64 * 1024;

  /// @brief The route whose PATCH carries the drill-down state point.
  static constexpr const char * kPointRoute = "ngnt/map";

  /// @brief What a room currently holds.
  enum class State {
    Empty,       ///< The room answered and has no file open (safe to restore into)
    Active,      ///< The room has a file open; leave the live session alone
    Unreachable, ///< The room did not answer, or answered with something unreadable
  };

  /**
   * @brief Ask a room what it currently holds.
   * @param http Transport to use.
   * @param roomBaseUrl Room base URL, e.g. http://ndmspc-room-x-00001.default.svc.cluster.local:80.
   * @param file Filled with the opened file name when the room is Active.
   * @param error Filled when the room is Unreachable.
   * @param token The room's read-write access token, when it has one.
   * @return The room's state.
   */
  static State Probe(NHttpRequest & http, const std::string & roomBaseUrl, std::string & file, std::string & error,
                     const std::string & token = std::string());

  /**
   * @brief Assemble a snapshot from the three things a session is.
   *
   * Shared by the HTTP capture below and by a room building its own snapshot from its
   * in-process state.
   *
   * @param roomId Room id, recorded in the snapshot.
   * @param file The opened file; an empty name yields no snapshot.
   * @param history The server's history array (GET /api/ -> state.history).
   * @param point The drill-down state point (may be null).
   * @return The snapshot, or a null json when no file is open.
   */
  static json Build(const std::string & roomId, const std::string & file, const json & history,
                    const json & point);

  /**
   * @brief Capture a replayable snapshot of a room's session.
   *
   * Reads the room over HTTP with the transport given; the room's own server uses Build()
   * directly instead, to avoid a round trip through itself.
   *
   * @param http Transport to use.
   * @param roomBaseUrl Room base URL.
   * @param roomId Room id, recorded in the snapshot.
   * @param error Filled when the room cannot be read.
   * @param token The room's read-write access token, when it has one.
   * @return The snapshot, or a null json when the room has nothing open.
   */
  static json Capture(NHttpRequest & http, const std::string & roomBaseUrl, const std::string & roomId,
                      std::string & error, const std::string & token = std::string());

  /**
   * @brief Runs one replayed action.
   *
   * @param method HTTP verb to run ("POST" for the actions, "PATCH" for the state point,
   *               "GET" for the state read back).
   * @param route Route key, e.g. "ngnt/open" or "state".
   * @param body Request body.
   * @param error Filled when the action failed.
   * @return The action's response, or a null json when there was none.
   */
  using Dispatch =
      std::function<json(const std::string & method, const std::string & route, const json & body, std::string & error)>;

  /**
   * @brief Replay a snapshot through a caller-supplied dispatcher.
   *
   * This is what both replays are built on: Restore() passes a dispatcher that talks to a
   * room over HTTP, and a room restoring itself passes one that runs the actions through its
   * own request path.
   *
   * @param snapshot The snapshot, as produced by Build()/Capture().
   * @param dispatch Runs one action.
   * @param error Filled with the first failing step.
   * @return True when every step succeeded.
   */
  static bool RestoreInPlace(const json & snapshot, const Dispatch & dispatch, std::string & error);

  /**
   * @brief Replay a snapshot into a room over HTTP.
   *
   * Callers are responsible for checking that the room is empty first (see Probe).
   *
   * @param http Transport to use.
   * @param roomBaseUrl Room base URL.
   * @param snapshot The snapshot, as produced by Capture() and Decode().
   * @param error Filled with the first failing step.
   * @param token The room's read-write access token, when it has one.
   * @return True when every step succeeded.
   */
  static bool Restore(NHttpRequest & http, const std::string & roomBaseUrl, const json & snapshot,
                      std::string & error, const std::string & token = std::string());

  /**
   * @brief Whether an action defines session state and is therefore worth replaying.
   * @param routeName Full route key, e.g. "ngnt/reshape".
   * @return True for the actions Restore() replays.
   */
  static bool IsReplayable(const std::string & routeName);

  /**
   * @brief Serialize a snapshot for storage (e.g. in a Service annotation).
   * @param snapshot The snapshot.
   * @return The compact JSON text, or "" when it is empty or too large.
   */
  static std::string Encode(const json & snapshot);

  /**
   * @brief Parse a stored snapshot.
   * @param text The stored JSON text.
   * @param snapshot Filled with the parsed snapshot.
   * @return False when the text is empty or not a JSON object.
   */
  static bool Decode(const std::string & text, json & snapshot);
};
} // namespace Ndmspc
#endif
