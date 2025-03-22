#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/ostream/common_utils.h"
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/read_write_log_record.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"
#include "SimpleOStreamLogRecordExporter.h"

namespace sdklogs   = opentelemetry::sdk::logs;
namespace sdkcommon = opentelemetry::sdk::common;

namespace Ndmspc {

/*********************** Constructor ***********************/

SimpleOStreamLogRecordExporter::SimpleOStreamLogRecordExporter(std::ostream & sout) noexcept : sout_(sout) {}

/*********************** Exporter methods ***********************/

std::unique_ptr<sdklogs::Recordable> SimpleOStreamLogRecordExporter::MakeRecordable() noexcept
{
  return std::unique_ptr<sdklogs::Recordable>(new sdklogs::ReadWriteLogRecord());
}

opentelemetry::sdk::common::ExportResult SimpleOStreamLogRecordExporter::Export(
    const opentelemetry::nostd::span<std::unique_ptr<sdklogs::Recordable>> & records) noexcept
{
  if (isShutdown()) {
    OTEL_INTERNAL_LOG_ERROR("[Simple Ostream Log Exporter] Exporting " << records.size()
                                                                       << " log(s) failed, exporter is shutdown");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  for (auto & record : records) {
    auto log_record =
        std::unique_ptr<sdklogs::ReadWriteLogRecord>(static_cast<sdklogs::ReadWriteLogRecord *>(record.release()));

    if (log_record == nullptr) {
      // TODO: Log Internal SDK error "recordable data was lost"
      continue;
    }

    int64_t event_id = log_record->GetEventId();

    // Convert trace, spanid, traceflags into exportable representation
    constexpr int trace_id_len    = 32;
    constexpr int span_id_len     = 16;
    constexpr int trace_flags_len = 2;

    char trace_id[trace_id_len]       = {0};
    char span_id[span_id_len]         = {0};
    char trace_flags[trace_flags_len] = {0};

    log_record->GetTraceId().ToLowerBase16(trace_id);
    log_record->GetSpanId().ToLowerBase16(span_id);
    log_record->GetTraceFlags().ToLowerBase16(trace_flags);

    auto observed_timestamp = log_record->GetObservedTimestamp();
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(observed_timestamp.time_since_epoch());
    auto seconds          = std::chrono::duration_cast<std::chrono::seconds>(time_since_epoch);
    std::time_t time_t_seconds = seconds.count();
    std::tm     timeinfo;
    localtime_r(&time_t_seconds, &timeinfo);

    sout_ << "[" << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << "] ";
    // sout_ << "." << std::setw(9) << std::setfill('0')
    //       << (time_since_epoch.count() - std::chrono::duration_cast<std::chrono::nanoseconds>(seconds).count());

    // Print out each field of the log record, noting that severity is separated
    // into severity_num and severity_text
    sout_ << "[";

    std::uint32_t severity_index = static_cast<std::uint32_t>(log_record->GetSeverity());
    if (severity_index >= std::extent<decltype(opentelemetry::logs::SeverityNumToText)>::value) {
      sout_ << "Invalid severity(" << severity_index << ")";
    }
    else {
      sout_ << opentelemetry::logs::SeverityNumToText[severity_index];
    }

    sout_ << "] ";
    opentelemetry::exporter::ostream_common::print_value(log_record->GetBody(), sout_);

    sout_ << "\n";
  }

  return opentelemetry::sdk::common::ExportResult::kSuccess;
}

bool SimpleOStreamLogRecordExporter::ForceFlush(std::chrono::microseconds /* timeout */) noexcept
{
  sout_.flush();
  return true;
}

bool SimpleOStreamLogRecordExporter::Shutdown(std::chrono::microseconds) noexcept
{
  is_shutdown_ = true;
  return true;
}

bool SimpleOStreamLogRecordExporter::isShutdown() const noexcept
{
  return is_shutdown_;
}

void SimpleOStreamLogRecordExporter::printAttributes(
    const std::unordered_map<std::string, sdkcommon::OwnedAttributeValue> & map, const std::string & prefix)
{
  for (const auto & kv : map) {
    sout_ << prefix << kv.first << ": ";
    opentelemetry::exporter::ostream_common::print_value(kv.second, sout_);
  }
}

void SimpleOStreamLogRecordExporter::printAttributes(
    const std::unordered_map<std::string, opentelemetry::common::AttributeValue> & map, const std::string & prefix)
{
  for (const auto & kv : map) {
    sout_ << prefix << kv.first << ": ";
    opentelemetry::exporter::ostream_common::print_value(kv.second, sout_);
  }
}

} // namespace Ndmspc
