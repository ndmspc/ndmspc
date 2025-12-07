#pragma once
#include <cstdarg>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef WITH_OPENTELEMETRY
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/logs/logger_provider.h"
namespace logs_api = opentelemetry::logs;
#endif

// #include <TObject.h>

#define NLog(level, format, ...) Ndmspc::NLogger::Log(__FILE__, __LINE__, level, format, ##__VA_ARGS__)
#define NLogTrace(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kTrace, format, ##__VA_ARGS__)
#define NLogDebug(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kDebug, format, ##__VA_ARGS__)
#define NLogInfo(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kInfo, format, ##__VA_ARGS__)
#define NLogWarning(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kWarn, format, ##__VA_ARGS__)
#define NLogError(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kError, format, ##__VA_ARGS__)
#define NLogFatal(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kFatal, format, ##__VA_ARGS__)

namespace Ndmspc {

namespace logs {
/**
 * @brief Provides logging functionality for the Ndmspc project.
 *
 * NLogger is a singleton class that wraps OpenTelemetry logging APIs, offering
 * severity-based logging methods and utilities for log message formatting.
 * It supports thread-safe logging and severity level mapping from strings.
 *
 * @author Martin Vala <mvala@cern.ch>
 */

#ifdef WITH_OPENTELEMETRY

enum class Severity {
  kTrace  = logs_api::Severity::kTrace,
  kTrace2 = logs_api::Severity::kTrace2,
  kTrace3 = logs_api::Severity::kTrace3,
  kTrace4 = logs_api::Severity::kTrace4,
  kDebug  = logs_api::Severity::kDebug,
  kDebug2 = logs_api::Severity::kDebug2,
  kDebug3 = logs_api::Severity::kDebug3,
  kDebug4 = logs_api::Severity::kDebug4,
  kInfo   = logs_api::Severity::kInfo,
  kInfo2  = logs_api::Severity::kInfo2,
  kInfo3  = logs_api::Severity::kInfo3,
  kInfo4  = logs_api::Severity::kInfo4,
  kWarn   = logs_api::Severity::kWarn,
  kWarn2  = logs_api::Severity::kWarn2,
  kWarn3  = logs_api::Severity::kWarn3,
  kWarn4  = logs_api::Severity::kWarn4,
  kError  = logs_api::Severity::kError,
  kError2 = logs_api::Severity::kError2,
  kError3 = logs_api::Severity::kError3,
  kError4 = logs_api::Severity::kError4,
  kFatal  = logs_api::Severity::kFatal,
  kFatal2 = logs_api::Severity::kFatal2,
  kFatal3 = logs_api::Severity::kFatal3,
  kFatal4 = logs_api::Severity::kFatal4
};
#else

enum class Severity {
  kTrace = 0,
  kTrace2,
  kTrace3,
  kTrace4,
  kDebug,
  kDebug2,
  kDebug3,
  kDebug4,
  kInfo,
  kInfo2,
  kInfo3,
  kInfo4,
  kWarn,
  kWarn2,
  kWarn3,
  kWarn4,
  kError,
  kError2,
  kError3,
  kError4,
  kFatal,
  kFatal2,
  kFatal3,
  kFatal4
};

#endif
static const std::unordered_map<std::string, Severity> fgSeverityMap = {
    {"TRACE", Severity::kTrace},   {"TRACE2", Severity::kTrace2}, {"TRACE3", Severity::kTrace3},
    {"TRACE4", Severity::kTrace4}, {"DEBUG", Severity::kDebug},   {"DEBUG2", Severity::kDebug2},
    {"DEBUG3", Severity::kDebug3}, {"DEBUG4", Severity::kDebug4}, {"INFO", Severity::kInfo},
    {"INFO2", Severity::kInfo2},   {"INFO3", Severity::kInfo3},   {"INFO4", Severity::kInfo4},
    {"WARN", Severity::kWarn},     {"WARN2", Severity::kWarn2},   {"WARN3", Severity::kWarn3},
    {"WARN4", Severity::kWarn4},   {"ERROR", Severity::kError},   {"ERROR2", Severity::kError2},
    {"ERROR3", Severity::kError3}, {"ERROR4", Severity::kError4}, {"FATAL", Severity::kFatal},
    {"FATAL2", Severity::kFatal2}, {"FATAL3", Severity::kFatal3}, {"FATAL4", Severity::kFatal4}};
} // namespace logs

/**
 * @class NLogger
 * @brief Singleton logger class for Ndmspc.
 */
class NLogger {
  public:
  /**
   * @brief Constructs a new NLogger instance.
   */
  NLogger();

  /**
   * @brief Destroys the NLogger instance.
   */
  virtual ~NLogger();

  // Delete copy constructor and assignment operator
  NLogger(const NLogger &)             = delete;
  NLogger & operator=(const NLogger &) = delete;

  /**
   * @brief Returns the singleton instance of NLogger.
   * @return Pointer to the singleton NLogger.
   */
  static NLogger * Instance();

#ifdef WITH_OPENTELEMETRY
  /**
   * @brief Gets the default OpenTelemetry logger.
   * @return Shared pointer to the default logger.
   */
  static opentelemetry::nostd::shared_ptr<logs_api::Logger> GetDefaultLogger();

  /**
   * @brief Converts a severity string to the corresponding Severity enum.
   * @param severity_str The severity level as a string.
   * @return The corresponding Severity enum value.
   */
  static logs_api::Severity GetSeverityFromString(const std::string & severity_str);

  // logs_api::Severity GetMinSeverity() const { return fMinSeverity; }

#endif

  /**
   * @brief Converts a logs::Severity enum value to its string representation.
   * @param level The severity level to convert.
   * @return The string representation of the severity level.
   */
  static std::string SeverityToString(logs::Severity level);

  /**
   * @brief Parses a string to obtain the corresponding logs::Severity enum value.
   * @param severity_str The string representation of the severity.
   * @return The corresponding logs::Severity value.
   */
  static logs::Severity GetSeverityFromString(const std::string & severity_str);

  static void Log(const char * file, int line, logs::Severity level, const char * format, ...);

  // /**
  //  * @brief Logs a debug message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Debug(const char * format, ...);
  //
  // /**
  //  * @brief Logs an info message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Info(const char * format, ...);
  //
  // /**
  //  * @brief Logs a warning message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Warning(const char * format, ...);
  //
  // /**
  //  * @brief Logs an error message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Error(const char * format, ...);
  //
  // /**
  //  * @brief Logs a fatal message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Fatal(const char * format, ...);
  //
  // /**
  //  * @brief Logs a trace message.
  //  * @param format Format string.
  //  * @param ... Variable arguments.
  //  */
  // static void Trace(const char * format, ...);
  //
  static std::mutex fgLoggerMutex; ///< Mutex for thread-safe logger operations

  /**
   * @brief Sets the minimum severity level for logging.
   * @param level The minimum severity level to be set.
   */
  void SetMinSeverity(logs::Severity level) { fMinSeverity = level; }

  /**
   * @brief Gets the current minimum severity level for logging.
   * @return The current minimum severity level.
   */
  logs::Severity GetMinSeverity() const { return fMinSeverity; }

  private:
  logs::Severity                  fMinSeverity{logs::Severity::kInfo}; ///< Minimum severity level for logging
  static std::unique_ptr<NLogger> fgLogger;                            ///< Singleton instance of NLogger

  /**
   * @brief Initializes the logger.
   */
  void Init();

  /**
   * @brief Cleans up logger resources.
   */
  void Cleanup();

  /// \cond CLASSIMP
  // ClassDefOverride(NLogger, 1);
  /// \endcond;
};
} // namespace Ndmspc
