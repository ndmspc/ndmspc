// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iostream>
#include <memory>

#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/version.h"

namespace Ndmspc {

class OPENTELEMETRY_EXPORT NLogExporterFactory {
  public:
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create();
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(std::ostream & sout);
};

} // namespace Ndmspc
