#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <NGnHttpServer.h>
#include <TSystem.h>

struct FormCsvAppendResult {
  std::string              outputFile;
  std::vector<std::string> ignoredFields;
};

struct FormQuestion {
  std::string name;
  std::string columnName;
  std::string answer;
};

struct FormSection {
  std::string               name;
  std::vector<FormQuestion> questions;
};

struct FormResponse {
  std::string               timestamp;
  std::vector<FormQuestion> rootQuestions;
  std::vector<FormSection>  sections;
};

std::string GetFormCsvPath()
{
  const char * path = std::getenv("NDMSPC_FORM_CSV_PATH");
  if (path && path[0] != '\0') return path;
  return "form_responses.csv";
}

std::string GetFormTimestamp()
{
  std::time_t now = std::time(nullptr);
  std::tm     tm {};
#if defined(_WIN32)
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string FormJsonValueToString(const json & value)
{
  if (value.is_string()) return value.get<std::string>();
  if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
  if (value.is_number_integer()) return std::to_string(value.get<long long>());
  if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
  if (value.is_number_float()) {
    std::ostringstream out;
    out << value.get<double>();
    return out.str();
  }
  if (value.is_null()) return "";
  return value.dump();
}

std::string FormAnswerToString(const json & value)
{
  if (value.is_array()) {
    std::ostringstream out;
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i > 0) out << ", ";
      out << FormJsonValueToString(value[i]);
    }
    return out.str();
  }

  return FormJsonValueToString(value);
}

void AddQuestionsToSection(FormSection & section, const json & value, const std::string & columnPrefix)
{
  for (auto it = value.begin(); it != value.end(); ++it) {
    const std::string columnName = columnPrefix.empty() ? it.key() : columnPrefix + "_" + it.key();

    if (it.value().is_object()) {
      AddQuestionsToSection(section, it.value(), columnName);
      continue;
    }

    section.questions.push_back({it.key(), columnName, FormAnswerToString(it.value())});
  }
}

FormResponse ParseFormResponse(const json & form)
{
  FormResponse response;
  response.timestamp = GetFormTimestamp();

  for (auto it = form.begin(); it != form.end(); ++it) {
    if (it.value().is_object()) {
      FormSection section;
      section.name = it.key();
      AddQuestionsToSection(section, it.value(), it.key());
      response.sections.push_back(section);
      continue;
    }

    response.rootQuestions.push_back({it.key(), it.key(), FormAnswerToString(it.value())});
  }

  return response;
}

std::vector<FormQuestion> GetOrderedQuestions(const FormResponse & response, const json & fieldOrder)
{
  std::vector<FormQuestion> questions;
  for (const auto & question : response.rootQuestions) {
    questions.push_back(question);
  }

  for (const auto & section : response.sections) {
    for (const auto & question : section.questions) {
      questions.push_back(question);
    }
  }

  if (!fieldOrder.is_array()) return questions;

  std::vector<FormQuestion> orderedQuestions;
  for (const auto & field : fieldOrder) {
    if (!field.is_string()) continue;

    const std::string columnName = field.get<std::string>();
    auto question = std::find_if(questions.begin(), questions.end(), [&](const auto & item) {
      return item.columnName == columnName;
    });

    if (question == questions.end()) continue;

    auto alreadyAdded = std::find_if(orderedQuestions.begin(), orderedQuestions.end(), [&](const auto & item) {
      return item.columnName == columnName;
    });
    if (alreadyAdded == orderedQuestions.end()) orderedQuestions.push_back(*question);
  }

  for (const auto & question : questions) {
    auto alreadyAdded = std::find_if(orderedQuestions.begin(), orderedQuestions.end(), [&](const auto & item) {
      return item.columnName == question.columnName;
    });
    if (alreadyAdded == orderedQuestions.end()) orderedQuestions.push_back(question);
  }

  return orderedQuestions;
}

std::vector<std::string> BuildFormCsvFields(const FormResponse & response, const json & fieldOrder)
{
  std::vector<std::string> fields;
  fields.push_back("timestamp");

  for (const auto & question : GetOrderedQuestions(response, fieldOrder)) {
    if (std::find(fields.begin(), fields.end(), question.columnName) == fields.end()) fields.push_back(question.columnName);
  }

  return fields;
}

std::string CsvEscape(const std::string & value)
{
  bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
  if (!needsQuotes) return value;

  std::string escaped = "\"";
  for (char c : value) {
    if (c == '"') escaped += "\"\"";
    else escaped += c;
  }
  escaped += "\"";
  return escaped;
}

std::vector<std::string> ReadCsvHeader(const std::string & outputFile)
{
  std::ifstream in(outputFile);
  if (!in.good()) return {};

  std::string line;
  if (!std::getline(in, line)) return {};

  std::vector<std::string> fields;
  std::string              field;
  bool                     inQuotes = false;

  for (std::size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        field += '"';
        ++i;
      }
      else {
        inQuotes = !inQuotes;
      }
    }
    else if (c == ',' && !inQuotes) {
      fields.push_back(field);
      field.clear();
    }
    else {
      field += c;
    }
  }
  fields.push_back(field);
  return fields;
}

void EnsureFormCsvOutputDirectory(const std::string & outputFile)
{
  const std::size_t slash = outputFile.find_last_of("/\\");
  if (slash == std::string::npos) return;

  const std::string directory = outputFile.substr(0, slash);
  if (!directory.empty()) gSystem->mkdir(directory.c_str(), kTRUE);
}

FormCsvAppendResult AppendFormResponseToCsv(const json & form, const json & fieldOrder = json::array(),
                                            const std::string & outputFile = GetFormCsvPath())
{
  if (!form.is_object()) throw std::runtime_error("Invalid form response: expected object");

  EnsureFormCsvOutputDirectory(outputFile);

  const auto               response = ParseFormResponse(form);
  const auto               questions = GetOrderedQuestions(response, fieldOrder);
  std::vector<std::string> fields = ReadCsvHeader(outputFile);
  const bool               createFile = fields.empty();

  if (createFile) {
    fields = BuildFormCsvFields(response, fieldOrder);
  }

  std::ofstream out(outputFile, createFile ? std::ios::out : std::ios::app);
  if (!out.good()) throw std::runtime_error("Cannot open CSV output file: " + outputFile);

  if (createFile) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
      if (i > 0) out << ',';
      out << CsvEscape(fields[i]);
    }
    out << '\n';
  }

  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i > 0) out << ',';
    if (fields[i] == "timestamp") {
      out << CsvEscape(response.timestamp);
      continue;
    }

    auto value = std::find_if(questions.begin(), questions.end(), [&](const auto & item) {
      return item.columnName == fields[i];
    });
    if (value != questions.end()) out << CsvEscape(value->answer);
  }
  out << '\n';

  FormCsvAppendResult result;
  result.outputFile = outputFile;
  for (const auto & question : questions) {
    if (std::find(fields.begin(), fields.end(), question.columnName) == fields.end()) {
      result.ignoredFields.push_back(question.columnName);
    }
  }
  return result;
}

void httpNgntForm()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  handlers["form"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    if (method.find("GET") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {
      NLogInfo("/form POST httpIn=%s", httpIn.dump().c_str());
      try {
        if (!httpIn.is_object() || !httpIn.contains("form") || !httpIn["form"].is_object()) {
          httpOut["result"] = "failure";
          httpOut["error"]  = "Invalid form request: root object must contain object field 'form'";
          return;
        }

        auto result = AppendFormResponseToCsv(httpIn["form"], httpIn.value("fieldOrder", json::array()));

        httpOut["result"]     = "success";
        httpOut["outputFile"] = result.outputFile;
        if (!result.ignoredFields.empty()) httpOut["ignoredFields"] = result.ignoredFields;
      } catch (const std::exception & e) {
        NLogError("/form POST failed: %s", e.what());
        httpOut["result"] = "failure";
        httpOut["error"]  = e.what();
      }
    }
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for form action";
    }
  };
}
