
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
namespace logs_sdk = opentelemetry::sdk::logs;

#include "NLogExporter.h"
#include "NLogger.h"

/// \cond CLASSIMP
// ClassImp(Ndmspc::NLogger);
/// \endcond

namespace Ndmspc {
// Initialize pointer to null
std::unique_ptr<NLogger> NLogger::fgLogger = std::unique_ptr<NLogger>(new NLogger());
// std::unique_ptr<NLogger> NLogger::fgLogger = nullptr;
std::mutex NLogger::fgLoggerMutex;

// NLogger::NLogger() : TObject()
NLogger::NLogger()
{
  ///
  /// Constructor
  ///

  // Initialize the logger
  Init();
}
NLogger::~NLogger()
{
  ///
  /// Destructor
  ///

  Cleanup();
}
NLogger * NLogger::Instance()
{
  ///
  /// Returns the singleton instance of NLogger
  ///

  std::lock_guard<std::mutex> lock(fgLoggerMutex);
  if (fgLogger == nullptr) {
    fgLogger = std::unique_ptr<NLogger>(new NLogger());
  }
  return fgLogger.get();
}
void NLogger::Init()
{
  ///
  /// Init logger
  ///

  logs_api::Severity min_severity = logs_api::Severity::kInfo;
  const char *       env_severity = getenv("NDMSPC_LOG_LEVEL");
  if (env_severity) {
    min_severity = GetSeverityFromString(env_severity);
    // if (min_severity < logs_api::Severity::kInfo) {
    //   std::cout << "NLogger: Setting log level to '" << env_severity << "' ..." << std::endl;
    // }
  }
  opentelemetry::sdk::resource::ResourceAttributes resource_attributes = {{"service.name", "ndmspc-service"},
                                                                          {"application", "ndmspc-app"}};
  auto resource = opentelemetry::sdk::resource::Resource::Create(resource_attributes);

  auto exporter  = std::unique_ptr<logs_sdk::LogRecordExporter>(new NLogExporter(std::cout, min_severity));
  auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));

  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> sdk_provider(
      logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource));

  // Set the global logger provider
  const std::shared_ptr<logs_api::LoggerProvider> & api_provider = sdk_provider;

  logs_api::Provider::SetLoggerProvider(api_provider);
}

void NLogger::Cleanup()
{
  ///
  /// Cleanup logger
  ///

  std::shared_ptr<logs_api::LoggerProvider> noop;
  logs_api::Provider::SetLoggerProvider(noop);
}
logs_api::Severity NLogger::GetSeverityFromString(const std::string & severity_str)
{
  ///
  /// Returns the severity level from a string
  ///

  auto it = fgSeverityMap.find(severity_str);
  if (it != fgSeverityMap.end()) {
    return it->second;
  }
  // Default to INFO if unknown
  return logs_api::Severity::kInfo;
}
opentelemetry::nostd::shared_ptr<logs_api::Logger> NLogger::GetDefaultLogger()
{
  ///
  /// Returns the default logger
  ///

  auto provider = logs_api::Provider::GetLoggerProvider();
  return provider->GetLogger("ndmpsc", "ndmspc");
}

void NLogger::Log(logs_api::Severity level, const char * format, va_list args)
{
  ///
  /// Log the message with the given severity level
  ///

  std::stringstream ss;

  // Format the string using vprintf-like functionality
  // TODO: Increase size of buffer if needed
  char buffer[1024]; // Adjust size as needed
  vsnprintf(buffer, sizeof(buffer), format, args);
  ss << buffer;
  GetDefaultLogger()->Log(level, ss.str());
}

void NLogger::Debug(const char * format, ...)
{
  ///
  /// Debug log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, format, args);
  va_end(args);
}

void NLogger::Info(const char * format, ...)
{
  ///
  /// Info log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, format, args);
  va_end(args);
}

void NLogger::Warning(const char * format, ...)
{
  ///
  /// Warning log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, format, args);
  va_end(args);
}

void NLogger::Error(const char * format, ...)
{
  ///
  /// Error log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, format, args);
  va_end(args);
}
void NLogger::Fatal(const char * format, ...)
{
  ///
  /// Fatal log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kFatal, format, args);
  va_end(args);
}

void NLogger::Trace(const char * format, ...)
{
  ///
  /// Trace log
  ///

  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, format, args);
  va_end(args);
}

} // namespace Ndmspc
