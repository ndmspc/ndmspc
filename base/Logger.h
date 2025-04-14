#ifndef NDMSPC_LOGGER
#define NDMSPC_LOGGER
#include <cstdarg>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/tracer_provider.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/version.h"
#include "opentelemetry/sdk/version/version.h"
#include "opentelemetry/exporters/otlp/otlp_environment.h"
#include "opentelemetry/exporters/otlp/otlp_http.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"

namespace logs_api  = opentelemetry::logs;
namespace trace_api = opentelemetry::trace;

namespace Ndmspc {

class Logger {
  private:
  logs_api::Severity             min_severity_{logs_api::Severity::kInfo}; // Default log level
  static std::unique_ptr<Logger> instance;
  static std::mutex              mutex_;
  opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions logger_opts_;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider>       otl_logger_provider_;

  // Private constructor to prevent direct instantiation
  Logger(logs_api::Severity min_severity = logs_api::Severity::kInfo) : min_severity_(min_severity) {}

  public:
  // Delete copy constructor and assignment operator
  Logger(const Logger &)             = delete;
  Logger & operator=(const Logger &) = delete;
  virtual ~Logger()
  {
    try {
      Cleanup();
    }
    catch (...) {
      // Handle any exceptions that may occur during cleanup
    }
  }

  static Logger * getInstance(std::string url = "")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance == nullptr) {
      logs_api::Severity min_severity = logs_api::Severity::kInfo;
      const char *       env_severity = getenv("NDMSPC_LOG_LEVEL");
      if (env_severity) {
        min_severity = GetSeverityFromString(env_severity);
      }
      instance = std::unique_ptr<Logger>(new Logger(min_severity));
      if (!url.empty()) {
        instance->logger_opts_.url = url;
        instance->InitOTLLogger();
      }
      instance->InitTracer();
      instance->InitLogger();
    }
    return instance.get();
  }
  static logs_api::Severity GetSeverityFromString(const std::string & severity_str)
  {
    static const std::unordered_map<std::string, logs_api::Severity> severity_map = {
        {"TRACE", logs_api::Severity::kTrace},   {"TRACE2", logs_api::Severity::kTrace2},
        {"TRACE3", logs_api::Severity::kTrace3}, {"TRACE4", logs_api::Severity::kTrace4},
        {"DEBUG", logs_api::Severity::kDebug},   {"DEBUG2", logs_api::Severity::kDebug2},
        {"DEBUG3", logs_api::Severity::kDebug3}, {"DEBUG4", logs_api::Severity::kDebug4},
        {"INFO", logs_api::Severity::kInfo},     {"INFO2", logs_api::Severity::kInfo2},
        {"INFO3", logs_api::Severity::kInfo3},   {"INFO4", logs_api::Severity::kInfo4},
        {"WARN", logs_api::Severity::kWarn},     {"WARN2", logs_api::Severity::kWarn2},
        {"WARN3", logs_api::Severity::kWarn3},   {"WARN4", logs_api::Severity::kWarn4},
        {"ERROR", logs_api::Severity::kError},   {"ERROR2", logs_api::Severity::kError2},
        {"ERROR3", logs_api::Severity::kError3}, {"ERROR4", logs_api::Severity::kError4},
        {"FATAL", logs_api::Severity::kFatal},   {"FATAL2", logs_api::Severity::kFatal2},
        {"FATAL3", logs_api::Severity::kFatal3}, {"FATAL4", logs_api::Severity::kFatal4},
    };

    auto it = severity_map.find(severity_str);
    if (it != severity_map.end()) {
      return it->second;
    }
    // Default to INFO if unknown
    return logs_api::Severity::kInfo;
  }
  logs_api::Severity GetMinSeverity() const { return min_severity_; }

  void InitTracer();
  void CleanupTracer();
  void InitLogger(logs_api::Severity min_severity = logs_api::Severity::kInfo);
  void CleanupLogger();
  void InitOTLLogger();
  void CleanupOTLLogger();

  void Cleanup()
  {
    CleanupTracer();
    CleanupLogger();
    CleanupOTLLogger();
  }
  opentelemetry::nostd::shared_ptr<trace_api::Tracer> GetDefaultTracer()
  {
    auto provider = trace_api::Provider::GetTracerProvider();
    return provider->GetTracer("ndmspc", OPENTELEMETRY_SDK_VERSION);
  }

  opentelemetry::nostd::shared_ptr<logs_api::Logger> GetDefaultLogger()
  {
    auto provider = logs_api::Provider::GetLoggerProvider();
    return provider->GetLogger("ndmpsc_logger", "ndmspc");
  }
  logs_api::Logger * GetOtlLogger()
  {
    if (otl_logger_provider_ == nullptr) return nullptr;

    return otl_logger_provider_->GetLogger("ndmpsc_logger", "ndmspc").get();
  }
  // void     SetSeverity(Severity level);
  // Severity GetSeverity() const { return currentSeverity; }

  void Log(logs_api::Severity level, logs_api::Logger * logger, const char * format, va_list args);
  void Debug(logs_api::Logger * logger, const char * format, ...);
  void Debug(const char * format, ...);
  void DebugOtl(const char * format, ...);
  void Info(logs_api::Logger * logger, const char * format, ...);
  void Info(const char * format, ...);
  void InfoOtl(const char * format, ...);
  void Warning(logs_api::Logger * logger, const char * format, ...);
  void Warning(const char * format, ...);
  void WarningOtl(const char * format, ...);
  void Error(logs_api::Logger * logger, const char * format, ...);
  void Error(const char * format, ...);
  void ErrorOtl(const char * format, ...);
  void Trace(logs_api::Logger * logger, const char * format, ...);
  void Trace(const char * format, ...);
  void TraceOtl(const char * format, ...);
};

} // namespace Ndmspc
#endif // NDMSPC_BASE_LOGGER
