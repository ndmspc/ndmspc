#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>

#include "opentelemetry/logs/severity.h"
#include "opentelemetry/sdk/logs/exporter.h"

namespace Ndmspc {

/**
 * @brief An exporter for log records that writes to a specified output stream.
 *
 * NLogExporter exports log records to a given std::ostream, filtering by minimum severity.
 * It implements the OpenTelemetry LogRecordExporter interface.
 */
class NLogExporter final : public opentelemetry::sdk::logs::LogRecordExporter {
  public:
  /**
   * @brief Constructs a new NLogExporter.
   * @param sout The output stream to write logs to (default: std::cout).
   * @param min_severity The minimum severity level to export (default: kTrace).
   */
  explicit NLogExporter(std::ostream &                sout         = std::cout,
                        opentelemetry::logs::Severity min_severity = opentelemetry::logs::Severity::kTrace) noexcept;

  /**
   * @brief Creates a new recordable log entry.
   * @return A unique pointer to a new Recordable instance.
   */
  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override;

  /**
   * @brief Exports a batch of log records.
   * @param records Span of unique pointers to log records.
   * @return ExportResult indicating success or failure.
   */
  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> & records) noexcept
      override;

  /**
   * @brief Forces the exporter to flush any buffered data.
   * @param timeout Maximum time to wait for flush.
   * @return True if flush was successful, false otherwise.
   */
  bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  /**
   * @brief Shuts down the exporter, releasing any resources.
   * @param timeout Maximum time to wait for shutdown.
   * @return True if shutdown was successful, false otherwise.
   */
  bool Shutdown(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  private:
  std::ostream &    sout_;               ///< The OStream to send the logs to
  std::atomic<bool> is_shutdown_{false}; ///< Whether this exporter has been shut down
  /**
   * @brief Checks if the exporter has been shut down.
   * @return True if shut down, false otherwise.
   */
  bool                          isShutdown() const noexcept;
  opentelemetry::logs::Severity min_severity_; ///< Minimum severity level to export
};
} // namespace Ndmspc
