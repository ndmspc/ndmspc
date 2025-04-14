
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
namespace logs_sdk = opentelemetry::sdk::logs;

#include "NLogExporter.h"
#include "NLogger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NLogger);
/// \endcond

namespace Ndmspc {
// Initialize pointer to null
std::unique_ptr<NLogger> NLogger::fgLogger = nullptr;
std::mutex               NLogger::fgMutex;

NLogger::NLogger() : TObject(), fMinSeverity(logs_api::Severity::kInfo)
{
  // Initialize the logger
  const char * env_severity = getenv("NDMSPC_LOG_LEVEL");
  if (env_severity) {
    fMinSeverity = GetSeverityFromString(env_severity);
  }

  InitLogger();
}
NLogger::~NLogger()
{
  CleanupLogger();
}
NLogger * NLogger::Instance()
{
  std::lock_guard<std::mutex> lock(fgMutex);
  if (fgLogger == nullptr) {
    fgLogger = std::unique_ptr<NLogger>(new NLogger());
  }
  return fgLogger.get();
}
void NLogger::InitLogger()
{
  // Create ostream log exporter instance
  // auto exporter  = std::unique_ptr<logs_sdk::LogRecordExporter>(new logs_exporter::OStreamLogRecordExporter);
  // 1. Define Resource attributes
  opentelemetry::sdk::resource::ResourceAttributes resource_attributes = {{"service.name", "ndmspc-service"},
                                                                          {"application", "ndmspc-app"}};
  auto resource = opentelemetry::sdk::resource::Resource::Create(resource_attributes);

  auto exporter  = std::unique_ptr<logs_sdk::LogRecordExporter>(new NLogExporter(std::cout, fMinSeverity));
  auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));

  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> sdk_provider(
      logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource));

  // Set the global logger provider
  const std::shared_ptr<logs_api::LoggerProvider> & api_provider = sdk_provider;

  logs_api::Provider::SetLoggerProvider(api_provider);
}

void NLogger::CleanupLogger()
{
  std::shared_ptr<logs_api::LoggerProvider> noop;
  logs_api::Provider::SetLoggerProvider(noop);
}
logs_api::Severity NLogger::GetSeverityFromString(const std::string & severity_str)
{

  auto it = fgSeverityMap.find(severity_str);
  if (it != fgSeverityMap.end()) {
    return it->second;
  }
  // Default to INFO if unknown
  return logs_api::Severity::kInfo;
}
opentelemetry::nostd::shared_ptr<logs_api::Logger> NLogger::GetDefaultLogger()
{
  auto provider = logs_api::Provider::GetLoggerProvider();
  return provider->GetLogger("ndmpsc", "ndmspc");
}

void NLogger::Log(logs_api::Severity level, logs_api::Logger * logger, const char * format, va_list args)
{
  std::stringstream ss;

  // Format the string using vprintf-like functionality
  char buffer[1024]; // Adjust size as needed
  vsnprintf(buffer, sizeof(buffer), format, args);
  ss << buffer;
  if (logger != nullptr) {
    logger->Log(level, ss.str());
  }
  else {
    GetDefaultLogger()->Log(level, ss.str());
  }
}

void NLogger::Debug(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, logger, format, args);
  va_end(args);
}

void NLogger::Debug(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, nullptr, format, args);
  va_end(args);
}

void NLogger::Info(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, logger, format, args);
  va_end(args);
}

void NLogger::Info(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, nullptr, format, args);
  va_end(args);
}

void NLogger::Warning(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, logger, format, args);
  va_end(args);
}

void NLogger::Warning(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, nullptr, format, args);
  va_end(args);
}

void NLogger::Error(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, logger, format, args);
  va_end(args);
}
void NLogger::Error(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, nullptr, format, args);
  va_end(args);
}
void NLogger::Fatal(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kFatal, logger, format, args);
  va_end(args);
}
void NLogger::Fatal(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kFatal, nullptr, format, args);
  va_end(args);
}

void NLogger::Trace(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, logger, format, args);
  va_end(args);
}

void NLogger::Trace(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, nullptr, format, args);
  va_end(args);
}

} // namespace Ndmspc
