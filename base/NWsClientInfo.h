#ifndef NDMSPC_NWS_CLIENT_INFO_H
#define NDMSPC_NWS_CLIENT_INFO_H

#include <string>   // For std::string
#include "Rtypes.h" // For ULong_t

namespace Ndmspc {

/**
 * @class NWsClientInfo
 * @brief Holds per-client data for WebSocket connections.
 *
 * Stores client ID, username, and message count for tracking individual clients.
 */
class NWsClientInfo {
  private:
  ULong_t     fWsId;         ///< Unique WebSocket client ID
  std::string fUsername;     ///< Username associated with the client
  int         fMessageCount; ///< Number of messages sent/received

  public:
  /**
   * @brief Default constructor.
   */
  NWsClientInfo();

  /**
   * @brief Constructor with initial values.
   * @param id WebSocket client ID.
   * @param username Username for the client.
   */
  NWsClientInfo(ULong_t id, const std::string & username);

  /**
   * @brief Get the WebSocket client ID.
   * @return Client ID.
   */
  ULong_t GetWsId() const;

  /**
   * @brief Get the username of the client.
   * @return Username string.
   */
  const std::string & GetUsername() const;

  /**
   * @brief Get the message count for the client.
   * @return Number of messages.
   */
  int GetMessageCount() const;

  /**
   * @brief Set the username for the client.
   * @param username New username.
   */
  void SetUsername(const std::string & username);

  /**
   * @brief Increment the message count for the client.
   */
  void IncrementMessageCount();
};

} // namespace Ndmspc

#endif // NDMSPC_NWS_CLIENT_INFO_H
