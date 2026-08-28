#include "NLlmHttpClient.h"
#include <stdexcept>
#include <format>

namespace Ndmspc::AI {
    HttpClientHelper::HttpClientHelper(const LLMBaseConfig &config)
        : config_{
              config
          }
          , client_{config.host}
          , headers_{} {
        // Configure client timeouts and SSL
        client_.enable_server_certificate_verification(config_.verify_ssl);
        client_.set_connection_timeout(config_.connection_timeout);
        client_.set_read_timeout(config_.read_timeout);
        client_.set_write_timeout(config_.write_timeout);

        // Build headers
        if (config_.provider == ApiProvider::OPENAI && !config_.api_key.empty()) {
            headers_.emplace("Authorization", "Bearer " + config_.api_key);
        }
    }

    std::string HttpClientHelper::post_request(const std::string &endpoint, const std::string &payload) {
        auto result = client_.Post(endpoint, headers_, payload, "application/json");
        if (!result) {
            throw std::runtime_error("HTTP request failed: " + httplib::to_string(result.error()));
        }

        if (result->status != 200) {
            throw std::runtime_error(
                std::format("HTTP error {}: {}", result->status, result->body)
            );
        }

        return result->body;
    }
}