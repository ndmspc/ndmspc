#include "NWritingAssistant.h"
#include <format>

namespace Ndmspc::AI {
    // Constructor - store reference to ChatClient and initialize prompts
    WritingAssistant::WritingAssistant(ChatClient &client)
        : client_(client) {
        initialize_prompts();
    }

    // Initialize all prompt templates in one place
    void WritingAssistant::initialize_prompts() {
        prompts_[PromptType::IMPROVE] =
                "You are a professional writing editor. Improve the following text by:\n"
                "- Fixing grammar and spelling errors\n"
                "- Enhancing clarity and readability\n"
                "- Improving word choice and sentence structure\n"
                "- Maintaining the original meaning and tone\n"
                "Return ONLY the improved text, no explanations or comments.";

        // Other prompts will be added in Phase 2
    }

    // Improve grammar, spelling and clarity.
    std::string WritingAssistant::improve(const std::string &text) {
        return client_.ask(text, prompts_.at(PromptType::IMPROVE));
    }

}
