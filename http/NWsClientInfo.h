#ifndef NDMSPC_NWS_CLIENT_INFO_H
#define NDMSPC_NWS_CLIENT_INFO_H

#include <string>   // For std::string
#include <chrono>   // For std::chrono::system_clock
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
  std::string fSubject;      ///< Verified token subject ("sub" claim)
  std::string fUsername;     ///< Username associated with the client
  int         fMessageCount; ///< Number of messages sent/received
  std::chrono::system_clock::time_point fConnectedAt; ///< Connection start time
  std::chrono::system_clock::time_point fTokenExpiresAt; ///< Expiry of the client's access token

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
   * @brief Constructor with a verified identity and token expiry.
   * @param id WebSocket client ID.
   * @param subject Verified token subject.
   * @param username Username for the client.
   * @param tokenExpiresAt Expiry of the client's access token.
   */
  NWsClientInfo(ULong_t id, std::string subject, std::string username,
                std::chrono::system_clock::time_point tokenExpiresAt);

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
   * @brief Get the verified token subject of the client.
   * @return Subject string.
   */
  const std::string & GetSubject() const;
  /**
   * @brief Get the expiry of the client's access token.
   * @return Token expiry time point.
   */
  std::chrono::system_clock::time_point GetTokenExpiresAt() const;
  /**
   * @brief Whether the client's token is still valid at a given time.
   * @param now Time to check against.
   * @return True when the token has not expired yet.
   */
  bool IsTokenValidAt(std::chrono::system_clock::time_point now) const;
  /**
   * @brief Replace the client's identity and token expiry.
   * @param subject New token subject.
   * @param username New username.
   * @param tokenExpiresAt New token expiry.
   */
  void ReplaceIdentity(std::string subject, std::string username,
                       std::chrono::system_clock::time_point tokenExpiresAt);

  /**
   * @brief Get the message count for the client.
   * @return Number of messages.
   */
  int GetMessageCount() const;

  /**
   * @brief Get the connection start time for the client.
   * @return Connection start time.
   */
  std::chrono::system_clock::time_point GetConnectedAt() const;

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
