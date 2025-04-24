#ifndef Ndmspc_NLogger_H
#define Ndmspc_NLogger_H
#include <cstdarg>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "opentelemetry/logs/severity.h"
#include "opentelemetry/logs/logger_provider.h"
namespace logs_api = opentelemetry::logs;

#include <TObject.h>

namespace Ndmspc {

///
/// \class NLogger
///
/// \brief NLogger object
///	\author Martin Vala <mvala@cern.ch>
///
static const std::unordered_map<std::string, logs_api::Severity> fgSeverityMap = {
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

class NLogger : public TObject {
  public:
  NLogger();
  virtual ~NLogger();
  // Delete copy constructor and assignment operator
  NLogger(const NLogger &)                                                             = delete;
  NLogger &                                                 operator=(const NLogger &) = delete;
  static NLogger *                                          Instance();
  static opentelemetry::nostd::shared_ptr<logs_api::Logger> GetDefaultLogger();

  static logs_api::Severity GetSeverityFromString(const std::string & severity_str);
  // logs_api::Severity        GetMinSeverity() const { return fMinSeverity; }

  static void Log(logs_api::Severity level, const char * format, va_list args);
  static void Debug(const char * format, ...);
  static void Info(const char * format, ...);
  static void Warning(const char * format, ...);
  static void Error(const char * format, ...);
  static void Fatal(const char * format, ...);
  static void Trace(const char * format, ...);

  static std::mutex fgLoggerMutex;

  private:
  // logs_api::Severity              fMinSeverity{logs_api::Severity::kInfo}; ///< Default log level
  static std::unique_ptr<NLogger> fgLogger;

  void Init();
  void Cleanup();

  /// \cond CLASSIMP
  ClassDef(NLogger, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
