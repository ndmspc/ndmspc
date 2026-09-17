#ifndef Ndmspc_room_ui_H
#define Ndmspc_room_ui_H

#include <string>

#include "ndmspc/http/NOidcTokenClient.h"

namespace Ndmspc {

/**
 * @struct NRoomUiOptions
 * @brief Everything the interactive room UI needs from the command line.
 */
struct NRoomUiOptions {
  std::string serverUrl;         ///< Router URL as given, shown in the header
  std::string mcpEndpoint;       ///< Derived MCP endpoint the client posts to
  int         refreshSeconds{5}; ///< Auto-refresh interval (0 = refresh on demand only)

  std::string clientCert;      ///< Client certificate for mutual TLS (optional)
  std::string clientKey;       ///< Client private key for mutual TLS (optional)
  std::string keyPasswordFile; ///< Base64-encoded passphrase for the private key (optional)
  std::string caFile;          ///< CA bundle used to verify the server (optional)
  std::string caPath;          ///< CA directory used to verify the server (optional)
  bool        insecure{false}; ///< Do not verify the server certificate

  NOidcTokenClientConfig oidc;                         ///< OIDC token configuration
  bool                   oidcEnabled{false};           ///< Whether to obtain a bearer token
  int                    oidcTokenRefreshSeconds{300}; ///< Re-obtain the token after this long
};

/**
 * @brief Run the interactive room UI until the user quits.
 *
 * Every room call is made on a worker thread: room_open blocks until the router's
 * Knative revision is ready, which would otherwise freeze the screen. The screen owns
 * the terminal, so NLogger's console output is switched off for the duration and
 * diagnostics are surfaced in the UI instead.
 *
 * @param options Router, TLS and authentication settings.
 * @return The process exit code (0 after a normal quit).
 */
int RunRoomUi(const NRoomUiOptions & options);

} // namespace Ndmspc
#endif
