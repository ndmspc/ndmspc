#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <TString.h>
#include <TBase64.h>
#include <curl/curl.h>
#include "NLogger.h"
#include "NHttpRequest.h"

namespace Ndmspc {
NHttpRequest::NHttpRequest() : curl(nullptr), headers(nullptr)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Error: curl_easy_init() failed");
  }
}

Ndmspc::NHttpRequest::~NHttpRequest()
{
  if (curl) {
    curl_easy_cleanup(curl);
  }
  if (headers) {
    curl_slist_free_all(headers);
  }
  curl_global_cleanup();
}

std::string Ndmspc::NHttpRequest::get(const std::string & url, const std::string & cert_path,
                                      const std::string & key_path, const std::string & key_password_file,
                                      bool insecure)
{
  std::ostringstream response_stream;
  CURLcode           res = request("GET", url, "", response_stream, cert_path, key_path, key_password_file, insecure);
  if (res != CURLE_OK) {
    throw_curl_error(res);
  }
  return response_stream.str();
}

std::string Ndmspc::NHttpRequest::post(const std::string & url, const std::string & post_data,
                                       const std::string & cert_path, const std::string & key_path,
                                       const std::string & key_password_file, bool insecure)
{
  std::ostringstream response_stream;
  CURLcode res = request("POST", url, post_data, response_stream, cert_path, key_path, key_password_file, insecure);
  if (res != CURLE_OK) {
    throw_curl_error(res);
  }
  return response_stream.str();
}

int Ndmspc::NHttpRequest::head(const std::string & url, const std::string & cert_path, const std::string & key_path,
                               const std::string & key_password_file, bool insecure)
{
  std::ostringstream response_stream;
  CURLcode           res = request("HEAD", url, "", response_stream, cert_path, key_path, key_password_file, insecure);
  if (res != CURLE_OK) {
    throw_curl_error(res);
  }
  // Find HTTP in header to get the response code
  int http_code = -1;
  for (const auto & header : received_headers) {
    if (header.find("HTTP/") != std::string::npos) {
      sscanf(header.c_str(), "HTTP/%*s %d", &http_code);
      break;
    }
  }

  return http_code;
}

CURLcode Ndmspc::NHttpRequest::request(const std::string & method, const std::string & url, const std::string & data,
                                       std::ostringstream & response_stream, const std::string & cert_path,
                                       const std::string & key_path, const std::string & key_password_file,
                                       bool insecure)
{
  CURLcode res = CURLE_OK;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  received_headers.clear();
  // Set method (GET or POST)
  if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_stream);
    // Set Content-Type header to application/json
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  else if (method == "HEAD") {
    // curl_easy_setopt(curl, CURLOPT_HTTPHEADER, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Set the header callback function
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    // Pass our vector address to the callback function via userdata
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &received_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 seconds
  }
  else if (method == "GET") {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_stream);
    // Set Content-Type header to application/json
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  else {
    return CURLE_UNSUPPORTED_PROTOCOL; // Unsupported method
  }

  // SSL certificate and key
  if (!cert_path.empty() && !key_path.empty()) {
    curl_easy_setopt(curl, CURLOPT_SSLCERT, cert_path.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLKEY, key_path.c_str());

    // Set the key password file
    if (!key_password_file.empty()) {
      curl_easy_setopt(curl, CURLOPT_SSLKEYPASSWD, NULL);
      curl_easy_setopt(curl, CURLOPT_KEYPASSWD, NULL);
      curl_easy_setopt(curl, CURLOPT_SSLKEYPASSWD, "");
      curl_easy_setopt(curl, CURLOPT_KEYPASSWD, "");

      std::ifstream passwordFile(key_password_file);
      std::string   encodedPassword;
      if (passwordFile.is_open()) {
        std::getline(passwordFile, encodedPassword);
        passwordFile.close();

        TString     encoded(encodedPassword);            // Use TString
        TString     decoded  = TBase64::Decode(encoded); // Decode with TBase64
        std::string password = decoded.Data();           // Convert TString back to std::string

        // Remove carriage return if it exists, to avoid authentication failure.
        if (!password.empty() && password.back() == '\r') {
          password.pop_back();
        }

        curl_easy_setopt(curl, CURLOPT_SSLKEYPASSWD, password.c_str());
        curl_easy_setopt(curl, CURLOPT_KEYPASSWD, password.c_str());
      }
      else {
        std::cerr << "Error: Could not open password file: " << key_password_file << std::endl;
        return CURLE_FILE_COULDNT_READ_FILE;
      }
    }
  }

  // Insecure option
  if (insecure) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  else {
    // Add the CA cert path here for secure connection
    // curl_easy_setopt(curl, CURLOPT_CAINFO, "/path/to/ca/cert.pem");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  }

  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    return res; // Return the error code
  }

  // // process received headers for HEAD request
  // for (const auto & header : received_headers) {
  //   NLogger::Info("Header: %s", header.c_str());
  // }

  return res;
}

size_t Ndmspc::NHttpRequest::WriteCallback(void * contents, size_t size, size_t nmemb, void * userp)
{
  (static_cast<std::ostringstream *>(userp))->write(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

size_t Ndmspc::NHttpRequest::HeaderCallback(char * buffer, size_t size, size_t nitems, void * userdata)
{
  size_t      len = size * nitems;
  std::string header(buffer, len);

  // Remove trailing newlines and carriage returns
  if (!header.empty() && header.back() == '\n') {
    header.pop_back();
  }
  if (!header.empty() && header.back() == '\r') {
    header.pop_back();
  }

  // Cast userdata back to the vector we passed
  std::vector<std::string> * headers = static_cast<std::vector<std::string> *>(userdata);
  if (headers) {
    headers->push_back(header);
  }

  return len; // Must return the number of bytes processed
}

void Ndmspc::NHttpRequest::throw_curl_error(CURLcode res) const
{
  throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(res));
}
} // namespace Ndmspc
