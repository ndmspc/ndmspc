#pragma once

#include <string>
#include "httplib.h"

namespace Ndmspc::AI {

    // Enum to select API provider
    enum class ApiProvider {
        OPENAI,
        OLLAMA
    };

    struct LLMBaseConfig {
        ApiProvider provider;
        std::string api_key;
        std::string host;
        bool verify_ssl{false};
        int connection_timeout{30};
        int read_timeout{300};
        int write_timeout{30};
    };

    class HttpClientHelper {
    private:
        LLMBaseConfig config_;
        httplib::Client client_;
        httplib::Headers headers_;

    public:
        explicit HttpClientHelper(const LLMBaseConfig &config);

        std::string post_request(const std::string &endpoint, const std::string &payload);
    };
}
