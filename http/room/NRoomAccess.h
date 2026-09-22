#ifndef Ndmspc_NRoomAccess_h
#define Ndmspc_NRoomAccess_h

#include <string>

#include "ndmspc/core/NLogger.h" ///< provides the global `json` (nlohmann) alias

namespace Ndmspc {

/**
 * @class NRoomAccess
 * @brief The access-token contract between a room and whoever talks to it.
 *
 * A room is created with two tokens: one that may do anything in it ("rw") and one that may only
 * read it ("ro"). The router hands both to the room through its environment, keeps a copy on the
 * room's own Service so a router restart does not lose them, and reports them to clients so a link
 * can be handed out. The room refuses traffic that does not carry its token, and a read-only token
 * may only issue GETs.
 *
 * A room that is given no tokens - an older image, or a room created before this existed - enforces
 * nothing, so the switch is the presence of the environment variable.
 */
class NRoomAccess {
  public:
  /// @brief The query parameter a client presents its token in - what a page link carries.
  static constexpr const char * kParam = "token";
  /// @brief The header a programmatic client presents its token in.
  static constexpr const char * kHeader = "X-NDMSPC-Room-Token";
  /// @brief The cookie a browser is given once it has presented a valid token.
  ///
  /// A page carries the token in its link, and the page's own scripts have no way to add a header
  /// to their API and WebSocket calls - so the page request hands them a cookie instead, which the
  /// browser then sends on its own.
  static constexpr const char * kCookie = "ndmspc-room-access";
  /// @brief The Service annotation holding a room's tokens.
  static constexpr const char * kAnnotation = "ndmspc.io/room-access";
  /// @brief The environment variable that carries the tokens into the room itself.
  static constexpr const char * kEnv = "NDMSPC_ROOM_ACCESS";
  /// @brief The token level that may do anything.
  static constexpr const char * kReadWrite = "rw";
  /// @brief The token level that may only read.
  static constexpr const char * kReadOnly = "ro";

  /**
   * @brief Parse the access JSON a room is handed ({"rw":"...","ro":"..."}).
   * @param text The environment value, or the annotation text.
   * @return The parsed object, or an empty object when there is nothing usable in it.
   */
  static json Parse(const std::string & text);

  /**
   * @brief The level a presented token grants in a room.
   * @param access The room's parsed access object (see Parse).
   * @param token The token a request presented; "" when it presented none.
   * @return "rw", "ro", or "" when the token grants nothing.
   */
  static std::string LevelOf(const json & access, const std::string & token);

  /**
   * @brief The access token carried by a query string (`?room=x&token=<hex>`).
   * @param query Raw query string, with or without a leading '?'.
   * @return The token, or "" when the query carries none.
   */
  static std::string TokenFromQuery(const std::string & query);

  /**
   * @brief The access token carried by a Cookie header.
   * @param cookieHeader The raw Cookie header value ("a=b; ndmspc-room-access=<hex>").
   * @return The token, or "" when the header carries none.
   */
  static std::string TokenFromCookie(const std::string & cookieHeader);
};

} // namespace Ndmspc
#endif
