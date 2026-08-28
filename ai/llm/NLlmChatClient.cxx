// #include "nlohmann/json.hpp"
#include <format>
#include "NLlmChatClient.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Ndmspc::AI {
    ChatClient::ChatClient(const ChatConfig& config)
    : http_(config.base)
    , model_name_(config.model_name)
    , max_output_tokens_(config.max_output_tokens)
    {
    }

    std::string ChatClient::ask(const std::string& prompt,
                           const std::optional<std::string>& instructions,
                           std::optional<int> max_output_tokens) {
        // Use override if provided, otherwise use default from config
        int tokens = max_output_tokens.value_or(max_output_tokens_);

        std::string request_body = build_request(prompt, instructions, tokens);
        std::string response_body = http_.post_request(ENDPOINT, request_body);
        return parse_response(response_body);
    }

    std::string ChatClient::build_request(const std::string& prompt,
                                         const std::optional<std::string>& instructions,
                                         int max_output_tokens) {
        json request;
        request["model"] = model_name_;
        request["input"] = prompt;
        request["max_output_tokens"] = max_output_tokens;  // Use parameter

        if (instructions.has_value()) {
            request["instructions"] = instructions.value();
        }

        return request.dump();
    }

    std::string ChatClient::parse_response(const std::string& body) {
        json response = json::parse(body);
        // Validate status
        const std::string status = response.at("status").get<std::string>();
        if (status == "failed") {
            const std::string error_code = response.at("error").at("code").get<std::string>();
            const std::string error_message = response.at("error").at("message").get<std::string>();
            throw std::runtime_error(
                std::format("API Error {}: {}", error_code, error_message)
            );
        }
        if (status != "completed") {
            throw std::runtime_error(std::format("Unexpected status: {}", status));
        }
        // Extract text from output array
        std::string complete_text;
        const auto& output_array = response.at("output");
        for (const auto& item : output_array) {
            if (item.at("type").get<std::string>() == "message") {
                const auto& content_array = item.at("content");
                for (const auto& content_item : content_array) {
                    if (content_item.at("type").get<std::string>() == "output_text") {
                        complete_text += content_item.at("text").get<std::string>();
                    }
                }
            }
        }

        if (complete_text.empty()) {
            throw std::runtime_error("No output_text found in response");
        }

        return complete_text;
    }

}