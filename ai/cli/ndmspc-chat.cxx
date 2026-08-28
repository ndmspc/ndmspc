// #include <format>
#include "ndmspc/ai/llm/NLlmChatClient.h"
#include "ndmspc/ai/agents/NWritingAssistant.h"
#include "ndmspc/core/NLogger.h"
#include "ndmspc/ndmspc.h"
std::string safe_getenv(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : std::string();
}
int main(int /*argc*/, char ** /*argv*/)
{

  NLogInfo("NDMSPC CLI - NDM Storage Tree Chat Client");
    try {
        // Configure ChatClient (OpenAI recommended for JSON reliability)
        Ndmspc::AI::ChatConfig config {
            .base = {
                .provider = Ndmspc::AI::ApiProvider::OPENAI,
                .api_key = safe_getenv("OPENAI_API_KEY"),
                .host = "https://api.openai.com"
            },
            .model_name = "gpt-4o-mini",
            .max_output_tokens = 500
        };

        // Alternative: Use Ollama (local, free, no API key needed)
        // Note: Ollama may be less reliable with JSON formatting
        // llmclient::ChatConfig config {
        //     .base = {
        //         .provider = llmclient::ApiProvider::OLLAMA,
        //         .api_key = "",
        //         .host = "http://localhost:11434"
        //     },
        //     .model_name = "llama3.2:3b",
        //     .max_output_tokens = 500
        // };

        Ndmspc::AI::ChatClient chat(config);

        Ndmspc::AI::WritingAssistant assistant(chat);

        // Test improve feature
        std::string original_text =
            "me and my friend went to store yesterday. we buyed some foods and drinks.";
        std::string improved_text = assistant.improve(original_text);

        std::cout << "\nOriginal: " << original_text << "\n"
                  << "Improved: " << improved_text << "\n";

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n\n";
        return 1;
    }
    catch (...) {
        std::cerr << "\nERROR: Unknown exception occurred.\n\n";
        return 1;
    }




  return 0;
}
