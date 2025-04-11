#ifndef Ndmspc_HttpRequestCurl_H
#define Ndmspc_HttpRequestCurl_H
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Ndmspc {
class HttpRequestCurl {
  public:
  HttpRequestCurl();
  ~HttpRequestCurl();

  std::string get(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
                  const std::string & key_password_file = "", bool insecure = false);

  std::string post(const std::string & url, const std::string & post_data, const std::string & cert_path = "",
                   const std::string & key_path = "", const std::string & key_password_file = "",
                   bool insecure = false);

  private:
  CURLcode request(const std::string & method, const std::string & url, const std::string & data,
                   std::ostringstream & response, const std::string & cert_path, const std::string & key_path,
                   const std::string & key_password_file, bool insecure);

  static size_t WriteCallback(void * contents, size_t size, size_t nmemb, void * userp);

  CURL *              curl;
  struct curl_slist * headers;

  void throw_curl_error(CURLcode res) const;
};

} // namespace Ndmspc

#endif // Ndmspc_HttpRequestCurl_H
