#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#ifdef WITH_OPENTELEMETRY
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
namespace logs_sdk = opentelemetry::sdk::logs;
#include "NLogExporter.h"
#endif

#include "NLogger.h"

/// \cond CLASSIMP
// ClassImp(Ndmspc::NLogger);
/// \endcond

namespace Ndmspc {
// Initialize pointer to null
std::unique_ptr<NLogger> NLogger::fgLogger = std::unique_ptr<NLogger>(new NLogger());
// std::unique_ptr<NLogger> NLogger::fgLogger = nullptr;
std::mutex NLogger::fgLoggerMutex;

// NLogger::NLoggerger() : TObject()
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

  const char * env_severity = getenv("NDMSPC_LOG_LEVEL");
  if (env_severity) {
    fMinSeverity = GetSeverityFromString(env_severity);
    if (fMinSeverity != logs::Severity::kInfo) {
      std::cout << "NLogger: Setting log level to '" << env_severity << "' ... " << std::endl;
    }
  }
#ifdef WITH_OPENTELEMETRY
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
#else
  // If OpenTelemetry is not enabled, no initialization is needed
#endif
}

void NLogger::Cleanup()
{
  ///
  /// Cleanup logger
  ///

#ifdef WITH_OPENTELEMETRY
  std::shared_ptr<logs_api::LoggerProvider> noop;
  logs_api::Provider::SetLoggerProvider(noop);
#else
  // If OpenTelemetry is not enabled, no cleanup is needed

#endif
}

#ifdef WITH_OPENTELEMETRY

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
#endif
std::string NLogger::SeverityToString(logs::Severity level)
{
  ///
  /// Convert enum Severity to string
  ///

  switch (level) {
  case logs::Severity::kTrace: return "TRACE";
  case logs::Severity::kDebug: return "DEBUG";
  case logs::Severity::kInfo: return "INFO";
  case logs::Severity::kWarn: return "WARN";
  case logs::Severity::kError: return "ERROR";
  case logs::Severity::kFatal: return "FATAL";
  default: return "UNKNOWN";
  }
}

logs::Severity NLogger::GetSeverityFromString(const std::string & severity_str)
{
  ///
  /// Returns the severity level from a string
  ///

  auto it = logs::fgSeverityMap.find(severity_str);
  if (it != logs::fgSeverityMap.end()) {
    return it->second;
  }
  // Default to INFO if unknown
  return logs::Severity::kInfo;
}

void NLogger::Log(const char * file, int line, logs::Severity level, const char * format, ...)
{
  ///
  /// Log the message with the given severity level
  ///

  if (fgLogger->GetMinSeverity() > level) {
    return; // Skip logging if below minimum severity
  }

  va_list args;
  va_start(args, format);

  // std::stringstream ss;

  // Format the string using vprintf-like functionality
  // TODO: Increase size of buffer if needed
  // char buffer[1024]; // Adjust size as needed
  // vsnprintf(buffer, sizeof(buffer), format, args);
  // ss << buffer;
#ifdef WITH_OPENTELEMETRY
  GetDefaultLogger()->Log(level, ss.str());
#else
  auto        now      = std::chrono::system_clock::now();
  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm *   now_tm   = std::localtime(&now_time);
  char        time_buf[24];
  auto        ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", now_tm);

  // Print file and line for DEBUG and lower
  if (level <= logs::Severity::kDebug) {
    std::filesystem::path abs_path(file);
    std::filesystem::path rel_path = abs_path.filename();
    std::cout << "[" << time_buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
              << "[" << SeverityToString(level) << "] "
              << "[" << rel_path.string() << ":" << line << "] ";
  }
  else {
    std::cout << "[" << time_buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
              << "[" << SeverityToString(level) << "] ";
  }
  vprintf(format, args);
  va_end(args);
  std::cout << std::endl;
#endif
}
// void NLOG_DEBUG(const char * format, ...)
// {
//   ///
//   /// Debug log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kDebug, format, args);
//   va_end(args);
// }
//
// void NLOG_INFO(const char * format, ...)
// {
//   ///
//   /// Info log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kInfo, format, args);
//   va_end(args);
// }
//
// void NLOG_WARN(const char * format, ...)
// {
//   ///
//   /// Warning log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kWarn, format, args);
//   va_end(args);
// }
//
// void NLOG_ERROR(const char * format, ...)
// {
//   ///
//   /// Error log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kError, format, args);
//   va_end(args);
// }
// void NLOG_FATAL(const char * format, ...)
// {
//   ///
//   /// Fatal log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kFatal, format, args);
//   va_end(args);
// }
//
// void NLOG_TRACE(const char * format, ...)
// {
//   ///
//   /// Trace log
//   ///
//
//   va_list args;
//   va_start(args, format);
//   Log(logs::Severity::kTrace, format, args);
//   va_end(args);
// }

} // namespace Ndmspc
