// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// #include "opentelemetry/exporters/ostream/log_record_exporter_factory.h"
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
// #include "opentelemetry/version.h"

#include "NLogExporterFactory.h"
#include "NLogExporter.h"

namespace logs_sdk = opentelemetry::sdk::logs;

namespace Ndmspc {

std::unique_ptr<logs_sdk::LogRecordExporter> NLogExporterFactory::Create()
{
  return Create(std::cout);
}

std::unique_ptr<logs_sdk::LogRecordExporter> NLogExporterFactory::Create(std::ostream & sout)
{
  std::unique_ptr<logs_sdk::LogRecordExporter> exporter(new NLogExporter(sout));
  return exporter;
}

} // namespace Ndmspc
