// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iostream>
#include <memory>

#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/version.h"

namespace Ndmspc {

/**
 * Factory class for SimpleOStreamLogRecordExporterFactory.
 */
class OPENTELEMETRY_EXPORT SimpleOStreamLogRecordExporterFactory {
  public:
  /**
   * Creates an SimpleOStreamLogRecordExporterFactory writing to the default location.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create();

  /**
   * Creates an SimpleOStreamLogRecordExporterFactory writing to the given location.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(std::ostream & sout);
};

} // namespace Ndmspc
