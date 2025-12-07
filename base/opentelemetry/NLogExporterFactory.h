#pragma once

#include <iostream>
#include <memory>

#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/version.h"

namespace Ndmspc {

/**
 * @brief Factory class for creating log exporters.
 *
 * NLogExporterFactory provides static methods to create instances of LogRecordExporter,
 * optionally allowing specification of an output stream for log records.
 */
class OPENTELEMETRY_EXPORT NLogExporterFactory {
  public:
  /**
   * @brief Creates a LogRecordExporter that writes to the default output stream.
   * @return A unique pointer to a LogRecordExporter instance.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create();

  /**
   * @brief Creates a LogRecordExporter that writes to the specified output stream.
   * @param sout The output stream to write logs to.
   * @return A unique pointer to a LogRecordExporter instance.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(std::ostream & sout);
};

} // namespace Ndmspc
