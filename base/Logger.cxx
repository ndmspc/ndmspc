#include <cstdarg>
#include <cstddef>
#include <memory>
#include <utility>

#include "SimpleOStreamLogRecordExporter.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/sdk/logs/logger_provider.h"

#include "opentelemetry/trace/provider.h"

#include "Logger.h"
#include <opentelemetry/sdk/logs/logger.h>

namespace logs_api      = opentelemetry::logs;
namespace logs_sdk      = opentelemetry::sdk::logs;
namespace logs_exporter = opentelemetry::exporter::logs;

namespace trace_api      = opentelemetry::trace;
namespace trace_sdk      = opentelemetry::sdk::trace;
namespace trace_exporter = opentelemetry::exporter::trace;

namespace Ndmspc {
// Initialize pointer to null
std::unique_ptr<Logger> Logger::instance = nullptr;
std::mutex              Logger::mutex_;

void Logger::InitTracer()
{
  // Create ostream span exporter instance
  auto exporter  = trace_exporter::OStreamSpanExporterFactory::Create();
  auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));

  std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> sdk_provider =
      trace_sdk::TracerProviderFactory::Create(std::move(processor));

  // Set the global trace provider
  const std::shared_ptr<trace_api::TracerProvider> & api_provider = sdk_provider;
  trace_api::Provider::SetTracerProvider(api_provider);
}

void Logger::CleanupTracer()
{
  std::shared_ptr<trace_api::TracerProvider> noop;
  trace_api::Provider::SetTracerProvider(noop);
}

void Logger::InitLogger(logs_api::Severity min_severity)
{
  // Create ostream log exporter instance
  // auto exporter  = std::unique_ptr<logs_sdk::LogRecordExporter>(new logs_exporter::OStreamLogRecordExporter);
  // 1. Define Resource attributes
  opentelemetry::sdk::resource::ResourceAttributes resource_attributes = {{"service.name", "ndmspc-service"},
                                                                          {"application", "ndmspc-app"}};
  auto resource = opentelemetry::sdk::resource::Resource::Create(resource_attributes);

  auto exporter =
      std::unique_ptr<logs_sdk::LogRecordExporter>(new SimpleOStreamLogRecordExporter(std::cout, min_severity_));
  auto processor = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));

  std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> sdk_provider(
      logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource));

  // Set the global logger provider
  const std::shared_ptr<logs_api::LoggerProvider> & api_provider = sdk_provider;

  logs_api::Provider::SetLoggerProvider(api_provider);
}

void Logger::CleanupLogger()
{
  std::shared_ptr<logs_api::LoggerProvider> noop;
  logs_api::Provider::SetLoggerProvider(noop);
}
void Logger::InitOTLLogger()
{
  // Create ostream log exporter instance
  // auto exporter  = std::unique_ptr<logs_sdk::LogRecordExporter>(new logs_exporter::OStreamLogRecordExporter);
  // 1. Define Resource attributes
  opentelemetry::sdk::resource::ResourceAttributes resource_attributes = {{"service.name", "ndmspc"},
                                                                          {"application", "ndmspc-app"}};
  auto resource = opentelemetry::sdk::resource::Resource::Create(resource_attributes);

  // logger_opts_.url = "http://localhost:4318/v1/logs";
  std::cout << "Using " << logger_opts_.url << " to export log records." << '\n';
  logger_opts_.console_debug = true;
  // Create OTLP exporter instance
  auto exporter        = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(logger_opts_);
  auto processor       = logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
  otl_logger_provider_ = logs_sdk::LoggerProviderFactory::Create(std::move(processor), resource);

  std::shared_ptr<logs_api::LoggerProvider> api_provider = otl_logger_provider_;
  logs_api::Provider::SetLoggerProvider(api_provider);
}

void Logger::CleanupOTLLogger()
{
  //   // We call ForceFlush to prevent to cancel running exportings, It's optional.
  // if (logger_provider_)
  // {
  //   logger_provider_->ForceFlush();
  // }
  //
  // logger_provider.reset();
  std::shared_ptr<logs_api::LoggerProvider> noop;
  logs_api::Provider::SetLoggerProvider(noop);
}

void Logger::Log(logs_api::Severity level, logs_api::Logger * logger, const char * format, va_list args)
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

void Logger::Debug(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, logger, format, args);
  va_end(args);
}

void Logger::Debug(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, nullptr, format, args);
  va_end(args);
}

void Logger::DebugOtl(const char * format, ...)
{
  if (GetOtlLogger() == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kDebug, GetOtlLogger(), format, args);
  va_end(args);
}

void Logger::Info(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, logger, format, args);
  va_end(args);
}

void Logger::Info(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, nullptr, format, args);
  va_end(args);
}

void Logger::InfoOtl(const char * format, ...)
{
  if (GetOtlLogger() == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kInfo, GetOtlLogger(), format, args);
  va_end(args);
}

void Logger::Warning(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, logger, format, args);
  va_end(args);
}

void Logger::Warning(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, nullptr, format, args);
  va_end(args);
}

void Logger::WarningOtl(const char * format, ...)
{
  if (GetOtlLogger() == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kWarn, GetOtlLogger(), format, args);
  va_end(args);
}

void Logger::Error(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, logger, format, args);
  va_end(args);
}
void Logger::Error(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, nullptr, format, args);
  va_end(args);
}

void Logger::ErrorOtl(const char * format, ...)
{
  if (GetOtlLogger() == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kError, GetOtlLogger(), format, args);
  va_end(args);
}

void Logger::Trace(logs_api::Logger * logger, const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, logger, format, args);
  va_end(args);
}

void Logger::Trace(const char * format, ...)
{
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, nullptr, format, args);
  va_end(args);
}
void Logger::TraceOtl(const char * format, ...)
{
  if (GetOtlLogger() == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  Log(logs_api::Severity::kTrace, GetOtlLogger(), format, args);
  va_end(args);
}
} // namespace Ndmspc
