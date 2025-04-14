// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>

#include "opentelemetry/logs/severity.h"
#include "opentelemetry/sdk/logs/exporter.h"

namespace Ndmspc {

class NLogExporter final : public opentelemetry::sdk::logs::LogRecordExporter {
  public:
  explicit NLogExporter(std::ostream &                sout         = std::cout,
                        opentelemetry::logs::Severity min_severity = opentelemetry::logs::Severity::kTrace) noexcept;
  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override;
  opentelemetry::sdk::common::ExportResult              Export(
                   const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> & records) noexcept
      override;
  bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;
  bool Shutdown(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  private:
  std::ostream &                sout_;               ///< The OStream to send the logs to
  std::atomic<bool>             is_shutdown_{false}; ///< Whether this exporter has been shut down
  bool                          isShutdown() const noexcept;
  opentelemetry::logs::Severity min_severity_;
};
} // namespace Ndmspc
