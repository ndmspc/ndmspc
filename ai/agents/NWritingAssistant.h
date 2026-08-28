#pragma once

#include "ndmspc/ai/llm/NLlmChatClient.h"
#include <string>
#include <map>

namespace Ndmspc::AI {
    // Prompt types for different writing operations
    enum class PromptType {
        IMPROVE,
        EXPAND,
        SUMMARIZE,
        CHANGE_TONE,
        TRANSLATE
    };

    class WritingAssistant {
    private:
        ChatClient& client_;
        std::map<PromptType, std::string> prompts_;

        // Initialize all prompt templates
        void initialize_prompts();

    public:
        // Constructor - accepts ChatClient via dependency injection
        explicit WritingAssistant(ChatClient& client);

        // Improve grammar, clarity, and style
        std::string improve(const std::string& text);

        // Expand text to target word count
        std::string expand(const std::string& text, int target_words = 300);

        // Summarize text to target word count
        std::string summarize(const std::string& text, int target_words = 100);

        // Change tone (formal, casual, professional, friendly, etc.)
        std::string change_tone(const std::string& text, const std::string& tone);

        // Translate to target language
        std::string translate(const std::string& text, const std::string& target_language);
    };

} // namespace Ndmspc::AI

