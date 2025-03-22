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
  // Severity                       currentSeverity = Severity::INFO; // Default log level
  static std::unique_ptr<Logger>                                  instance;
  static std::mutex                                               mutex_;
  opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions logger_opts_;
  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider>       otl_logger_provider_;

  // Private constructor to prevent direct instantiation
  Logger() {}

  public:
  // Delete copy constructor and assignment operator
  Logger(const Logger &)             = delete;
  Logger & operator=(const Logger &) = delete;

  static Logger * getInstance(std::string url = "")
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance == nullptr) {
      instance = std::unique_ptr<Logger>(new Logger());
      if (!url.empty()) {
        instance->logger_opts_.url = url;
        instance->InitOTLLogger();
      }
      instance->InitTracer();
      instance->InitLogger();
    }
    return instance.get();
  }

  void InitTracer();
  void CleanupTracer();
  void InitLogger();
  void CleanupLogger();
  void InitOTLLogger();
  void CleanupOTLLogger();

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

  void Log(opentelemetry::logs::Severity level, logs_api::Logger * logger, const char * format, va_list args);
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
