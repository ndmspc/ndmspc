#ifndef Ndmspc_NHttpRequest_H
#define Ndmspc_NHttpRequest_H
#include <string>

namespace Ndmspc {

/**
 * @class NHttpRequest
 * @brief Provides HTTP request functionality using cpp-httplib.
 *
 * NHttpRequest wraps cpp-httplib to perform HTTP GET, POST, and HEAD requests,
 * supporting client certificate authentication and custom error handling.
 * It offers methods for sending requests and retrieving responses, and
 * handles headers and SSL options.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NHttpRequest {
  public:
  /**
   * @brief Constructs a new NHttpRequest instance.
   */
  NHttpRequest();

  /**
   * @brief Destroys the NHttpRequest instance.
   */
  virtual ~NHttpRequest();

  /**
   * @brief Performs an HTTP GET request.
   * @param url The target URL.
   * @param cert_path Path to client certificate (optional).
   * @param key_path Path to private key (optional).
   * @param key_password_file Path to key password file (optional).
   * @param insecure If true, disables SSL verification.
   * @return Response body as a string.
   */
  std::string get(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
                  const std::string & key_password_file = "", bool insecure = false);

  /**
   * @brief Performs an HTTP POST request.
   * @param url The target URL.
   * @param post_data Data to post.
   * @param cert_path Path to client certificate (optional).
   * @param key_path Path to private key (optional).
   * @param key_password_file Path to key password file (optional).
   * @param insecure If true, disables SSL verification.
   * @return Response body as a string.
   */
  std::string post(const std::string & url, const std::string & post_data, const std::string & cert_path = "",
                   const std::string & key_path = "", const std::string & key_password_file = "",
                   bool insecure = false);

  /**
   * @brief Performs an HTTP HEAD request.
   * @param url The target URL.
   * @param cert_path Path to client certificate (optional).
   * @param key_path Path to private key (optional).
   * @param key_password_file Path to key password file (optional).
   * @param insecure If true, disables SSL verification.
   * @return HTTP status code.
   */
  int head(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
           const std::string & key_password_file = "", bool insecure = false);
};
} // namespace Ndmspc
#endif
