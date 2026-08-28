#pragma once

#include <string>
#include <optional>
#include "NLlmHttpClient.h"


namespace Ndmspc::AI {
    struct ChatConfig {
        LLMBaseConfig base;
        std::string model_name;
        int max_output_tokens {2000};
    };

    class ChatClient {
    private:
        HttpClientHelper http_;
        std::string model_name_;
        int max_output_tokens_;
        static constexpr const char* ENDPOINT = "/v1/responses";

    public:
        explicit ChatClient(const ChatConfig& config);

        std::string ask(const std::string& prompt,
                   const std::optional<std::string>& instructions = std::nullopt,
                   std::optional<int> max_output_tokens = std::nullopt);
    private:
        std::string build_request(const std::string& prompt,
                                 const std::optional<std::string>& instructions,
                                 int max_output_tokens);
        std::string parse_response(const std::string& body);
    };
}