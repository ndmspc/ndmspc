#ifndef Ndmspc_NHttpRequest_H
#define Ndmspc_NHttpRequest_H
#include <map>
#include <string>

namespace Ndmspc {

/**
 * @struct NHttpResponse
 * @brief Minimal HTTP response used by the generic request() call.
 */
struct NHttpResponse {
  int         status{0};  ///< HTTP status code (0 when the transport itself failed)
  std::string body;       ///< Response body
};

/**
 * @class NHttpRequest
 * @brief Provides HTTP request functionality using cpp-httplib.
 *
 * NHttpRequest wraps cpp-httplib to perform HTTP GET, POST, and HEAD requests,
 * supporting client certificate authentication and custom error handling.
 * It offers methods for sending requests and retrieving responses, and
 * handles headers and SSL options.
 *
 * request() is the general entry point: arbitrary method, request headers,
 * request body, and an optional CA bundle/directory for verifying the server
 * (needed e.g. to talk to the Kubernetes API with its in-cluster CA), returning
 * both the status code and the body.
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
  virtual std::string get(const std::string & url, const std::string & cert_path = "",
                          const std::string & key_path = "", const std::string & key_password_file = "",
                          bool insecure = false);

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
  virtual std::string post(const std::string & url, const std::string & post_data, const std::string & cert_path = "",
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
  virtual int head(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
                   const std::string & key_password_file = "", bool insecure = false);

  /**
   * @brief Override the connect and read timeouts for this instance.
   *
   * The defaults (10s connect, 60s read) suit talking to the Kubernetes API. A caller that
   * has to answer a client quickly - a room restoring its session before serving a request -
   * wants shorter ones, so an unreachable or busy peer cannot stall it.
   *
   * @param connectMs Connect timeout in milliseconds; 0 or less keeps the default.
   * @param readMs Read timeout in milliseconds; 0 or less keeps the default.
   */
  void SetTimeout(int connectMs, int readMs);

  /**
   * @brief Performs an arbitrary HTTP request (GET, HEAD, POST, PUT, PATCH, DELETE).
   *
   * Unlike get()/post(), this sends the caller's headers verbatim (e.g.
   * `Authorization: Bearer ...`) and can verify the server against a specific CA
   * (or, when insecure, not at all). The response status is always returned, so
   * callers can distinguish e.g. 404 from a transport failure.
   *
   * @param method HTTP method (case-insensitive).
   * @param url The target URL.
   * @param body Request body (POST/PUT/PATCH/DELETE).
   * @param headers Request headers (Content-Type is taken from here; defaults to application/json).
   * @param cert_path Path to client certificate (optional).
   * @param key_path Path to private key (optional).
   * @param key_password_file Path to key password file (optional).
   * @param ca_file CA bundle used to verify the server certificate (optional).
   * @param ca_path Directory of hashed CA certificates used to verify the server (optional).
   * @param insecure If true, disables SSL verification.
   * @return The response status and body.
   * @throws std::runtime_error when the transport fails (no HTTP response at all).
   *
   * Declared virtual so tests can substitute a fake transport; the class already has a
   * virtual destructor for that purpose.
   */
  virtual NHttpResponse request(const std::string & method, const std::string & url, const std::string & body = "",
                                const std::map<std::string, std::string> & headers = {},
                                const std::string & cert_path = "", const std::string & key_path = "",
                                const std::string & key_password_file = "", const std::string & ca_file = "",
                                const std::string & ca_path = "", bool insecure = false);

  private:
  int fConnectTimeoutMs{10000}; ///< Connect timeout, overridable with SetTimeout()
  int fReadTimeoutMs{60000};    ///< Read timeout, overridable with SetTimeout()
};
} // namespace Ndmspc
#endif
