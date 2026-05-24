#ifndef Ndmspc_NHttpRequest_H
#define Ndmspc_NHttpRequest_H
#include <curl/curl.h>
#include <sstream>
#include <string>
#include <vector>

namespace Ndmspc {

/**
 * @class NHttpRequest
 * @brief Provides HTTP request functionality using libcurl.
 *
 * NHttpRequest wraps libcurl to perform HTTP GET, POST, and HEAD requests,
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

  private:
  /**
   * @brief Internal method to perform an HTTP request.
   * @param method HTTP method ("GET", "POST", "HEAD").
   * @param url The target URL.
   * @param data Data to send (for POST).
   * @param response Output stream for response data.
   * @param cert_path Path to client certificate.
   * @param key_path Path to private key.
   * @param key_password_file Path to key password file.
   * @param insecure If true, disables SSL verification.
   * @return CURLcode result.
   */
  CURLcode request(const std::string & method, const std::string & url, const std::string & data,
                   std::ostringstream & response, const std::string & cert_path, const std::string & key_path,
                   const std::string & key_password_file, bool insecure);

  /**
   * @brief Callback for writing response data.
   * @param contents Pointer to received data.
   * @param size Size of a data unit.
   * @param nmemb Number of data units.
   * @param userp User pointer (output stream).
   * @return Number of bytes handled.
   */
  static size_t WriteCallback(void * contents, size_t size, size_t nmemb, void * userp);

  /**
   * @brief Callback for handling response headers.
   * @param buffer Pointer to header data.
   * @param size Size of a data unit.
   * @param nitems Number of data units.
   * @param userdata User pointer (header storage).
   * @return Number of bytes handled.
   */
  static size_t HeaderCallback(char * buffer, size_t size, size_t nitems, void * userdata);

  CURL *                   curl;             ///< libcurl handle
  struct curl_slist *      headers;          ///< List of custom headers
  std::vector<std::string> received_headers; ///< Received response headers

  /**
   * @brief Throws an exception if a CURL error occurs.
   * @param res CURLcode result.
   */
  void throw_curl_error(CURLcode res) const;
};
} // namespace Ndmspc
#endif
