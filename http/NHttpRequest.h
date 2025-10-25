#ifndef Ndmspc_NHttpRequest_H
#define Ndmspc_NHttpRequest_H
#include <curl/curl.h>
#include <sstream>
#include <string>
#include <vector>

namespace Ndmspc {

///
/// \class NHttpRequest
///
/// \brief NHttpRequest object
///	\author Martin Vala <mvala@cern.ch>
///

class NHttpRequest {
  public:
  NHttpRequest();
  virtual ~NHttpRequest();

  std::string get(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
                  const std::string & key_password_file = "", bool insecure = false);

  std::string post(const std::string & url, const std::string & post_data, const std::string & cert_path = "",
                   const std::string & key_path = "", const std::string & key_password_file = "",
                   bool insecure = false);
  int         head(const std::string & url, const std::string & cert_path = "", const std::string & key_path = "",
                   const std::string & key_password_file = "", bool insecure = false);

  private:
  CURLcode request(const std::string & method, const std::string & url, const std::string & data,
                   std::ostringstream & response, const std::string & cert_path, const std::string & key_path,
                   const std::string & key_password_file, bool insecure);

  static size_t WriteCallback(void * contents, size_t size, size_t nmemb, void * userp);
  static size_t HeaderCallback(char * buffer, size_t size, size_t nitems, void * userdata);

  CURL *                   curl;
  struct curl_slist *      headers;
  std::vector<std::string> received_headers;

  void throw_curl_error(CURLcode res) const;
};
} // namespace Ndmspc
#endif
