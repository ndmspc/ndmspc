#include <ndmspc/http/NGnHttpServer.h>

#include <filesystem>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <string>
#include <stdexcept>
#include <optional>

namespace fs = std::filesystem;

std::optional<std::string> decodeUrlComponent(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    const auto hexValue = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };

    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (value[index] != '%') {
            decoded.push_back(value[index]);
            continue;
        }

        if (index + 2 >= value.size()) {
            return std::nullopt;
        }

        const int high = hexValue(value[index + 1]);
        const int low = hexValue(value[index + 2]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }

        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }

    return decoded;
}

std::optional<std::string> parsePathFromQuery(const std::string& query) {
    if (query.empty()) {
        return std::nullopt;
    }

    std::size_t parameterStart = 0;
    while (parameterStart <= query.size()) {
        const std::size_t parameterEnd = query.find('&', parameterStart);
        const std::string parameter = query.substr(parameterStart, parameterEnd - parameterStart);
        const std::size_t separator = parameter.find('=');

        if (separator != std::string::npos) {
            const std::string name = parameter.substr(0, separator);
            if (name == "location") {
                const auto path = decodeUrlComponent(parameter.substr(separator + 1));
                if (!path || path->empty()) {
                    return std::nullopt;
                }
                return path;
            }
        }

        if (parameterEnd == std::string::npos) break;
        parameterStart = parameterEnd + 1;
    }

    return std::nullopt;
}

std::string getQueryFromHttpInput(const json& httpIn) {
    if (!httpIn.is_object()) {
        return std::string();
    }

    return httpIn.value("_query", std::string());
}

bool doesPathExist(const std::string& path, struct stat& sb);
bool isFile(const struct stat& sb);
bool isJson(const std::string& path);

std::vector<std::string> getQuestionnaireFilesFromPath(const std::string& path) {
    struct stat sb;
    std::vector<std::string> fileList;

    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            const std::string filePath = entry.path().string();

            if (doesPathExist(filePath, sb) && isFile(sb) && isJson(filePath)) {
                fileList.push_back(filePath);
            }
        }
    } catch (const fs::filesystem_error&) {
    }
    return fileList;
}

bool doesPathExist(const std::string& path, struct stat& sb) {
    return stat(path.c_str(), &sb) == 0;
}

bool isFile(const struct stat& sb) {
    return S_ISREG(sb.st_mode);
}

bool isJson(const std::string& path) {
    return path.find(".json") != std::string::npos;
}

std::optional<json> loadQuestionnaire(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        return std::nullopt;
    }

    try {
        json questionnaire;
        file >> questionnaire;

        return questionnaire;
    } catch (const json::parse_error&) {
        return std::nullopt;
    }
}

std::vector<json> loadQuestionnaires(const std::vector<std::string>& filePaths) {
    std::vector<json> questionnaires;

    for (const auto& filePath : filePaths) {
        const auto questionnaire = loadQuestionnaire(filePath);
        if (questionnaire) questionnaires.push_back(*questionnaire);
    }

    return questionnaires;
}

json constractGetQuestionnairesResponse(const std::string& location, const std::vector<json>& questionnaires) {
    json response;

    response["location"] = location;
    response["forms"] = json::array();

    for (const auto& questionnaire : questionnaires) {
        json form;

        form["id"] = questionnaire.value("id", "");
        form["title"] = questionnaire.value("title", "");
        form["path"] = questionnaire.value("path", "");
        form["description"] = questionnaire.value("description", "");
        form["schema"] = questionnaire.value("schema", json::object());
        response["forms"].push_back(form);
    }

    return response;
}

void httpNgntQuestionnaires () {
    auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

    handlers["questionnaires"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    NLogInfo("questionnaires: Received '%s' request", method.c_str());
    if (method.find("GET") != std::string::npos) {
        httpOut["forms"] = json::array();
        NLogInfo("questionnaires: Initialized response with an empty forms array");

        const std::string query = getQueryFromHttpInput(httpIn);
        if (query.empty()) {
            NLogWarning("questionnaires: Cannot respond because the 'location' query parameter is missing");
            httpOut["result"] = "failure";
            httpOut["error"] = "Missing 'location' query parameter";
            return;
        }
        NLogInfo("questionnaires: Extracted query '%s'", query.c_str());

        const auto path = parsePathFromQuery(query);
        if (!path) {
            NLogWarning("questionnaires: Cannot respond because the 'location' query parameter is invalid");
            httpOut["result"] = "failure";
            httpOut["error"] = "Invalid 'location' query parameter";
            return;
        }
        NLogInfo("questionnaires: Parsed location '%s'", path->c_str());

        NLogInfo("questionnaires: Scanning directory '%s'", path->c_str());
        const std::vector<std::string> questionnaireFilesList = getQuestionnaireFilesFromPath(*path);
        if (questionnaireFilesList.empty()) {
            NLogWarning("questionnaires: No questionnaire files found in '%s'", path->c_str());
            httpOut["result"] = "failure";
            httpOut["error"] = "Failed to get list of questionnaire files";
            return;
        }
        NLogInfo("questionnaires: Found %zu questionnaire file(s)", questionnaireFilesList.size());

        NLogInfo("questionnaires: Loading questionnaire files");
        const std::vector<json> questionnaires = loadQuestionnaires(questionnaireFilesList);
        if (questionnaires.empty()) {
            NLogWarning("questionnaires: No questionnaire files could be loaded from '%s'", path->c_str());
            httpOut["result"] = "failure";
            httpOut["error"] = "Failed to get questionnaire files";
            return;
        }
        NLogInfo("questionnaires: Loaded %zu questionnaire(s)", questionnaires.size());

        httpOut["result"] = "success";
        httpOut["forms"] = questionnaires;
        NLogInfo("questionnaires: Responding with %zu form(s) loaded from '%s'", questionnaires.size(), path->c_str());
    } else {
      NLogWarning("questionnaires: Unsupported HTTP method '%s'", method.c_str());
      httpOut["error"] = "Unsupported HTTP method for questionnaires action";
    }
  };
}
