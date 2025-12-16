#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <fstream>
#include <thread>

/**
 * @def NLog
 * @brief Logs a message with the specified severity level.
 * @param level The severity level (e.g., Ndmspc::logs::Severity::kInfo).
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 *
 * Logs the message with file name and line number information.
 */
#define NLog(level, format, ...) Ndmspc::NLogger::Log(__FILE__, __LINE__, level, format, ##__VA_ARGS__)

/**
 * @def NLogTrace
 * @brief Logs a message with TRACE severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogTrace(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kTrace, format, ##__VA_ARGS__)

/**
 * @def NLogDebug
 * @brief Logs a message with DEBUG severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogDebug(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kDebug, format, ##__VA_ARGS__)

/**
 * @def NLogInfo
 * @brief Logs a message with INFO severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogInfo(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kInfo, format, ##__VA_ARGS__)

/**
 * @def NLogWarning
 * @brief Logs a message with WARNING severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogWarning(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kWarn, format, ##__VA_ARGS__)

/**
 * @def NLogError
 * @brief Logs a message with ERROR severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogError(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kError, format, ##__VA_ARGS__)

/**
 * @def NLogFatal
 * @brief Logs a message with FATAL severity.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
#define NLogFatal(format, ...) \
  Ndmspc::NLogger::Log(__FILE__, __LINE__, Ndmspc::logs::Severity::kFatal, format, ##__VA_ARGS__)

namespace Ndmspc {

namespace logs {

/**
 * @enum Severity
 * @brief Log message severity levels
 *
 * Each base level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL) has 4 variants
 * for fine-grained control. Default minimum severity is kInfo.
 */
enum class Severity {
  kTrace = 0, ///< Most verbose trace level
  kTrace2,    ///< Trace level 2
  kTrace3,    ///< Trace level 3
  kTrace4,    ///< Trace level 4
  kDebug,     ///< Debug information (includes file:line)
  kDebug2,    ///< Debug level 2
  kDebug3,    ///< Debug level 3
  kDebug4,    ///< Debug level 4
  kInfo,      ///< Informational messages (default)
  kInfo2,     ///< Info level 2
  kInfo3,     ///< Info level 3
  kInfo4,     ///< Info level 4
  kWarn,      ///< Warning messages
  kWarn2,     ///< Warning level 2
  kWarn3,     ///< Warning level 3
  kWarn4,     ///< Warning level 4
  kError,     ///< Error messages
  kError2,    ///< Error level 2
  kError3,    ///< Error level 3
  kError4,    ///< Error level 4
  kFatal,     ///< Fatal errors
  kFatal2,    ///< Fatal level 2
  kFatal3,    ///< Fatal level 3
  kFatal4     ///< Fatal level 4
};

/// Map of severity level strings to enum values
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
 * @brief Thread-safe singleton logger with per-thread file output
 *
 * NLogger provides a thread-safe logging system where each thread can write
 * to its own log file. Supports multiple severity levels, console and file
 * output, and custom naming for processes and threads.
 *
 * @author Martin Vala <mvala@cern.ch>
 *
 * @par Example Usage:
 * @code{.cpp}
 * // Configure logger
 * Ndmspc::NLogger::SetProcessName("MyApp");
 * Ndmspc::NLogger::SetFileOutput(true);
 *
 * // Log messages
 * NLogInfo("Application started");
 * NLogDebug("Debug info: value=%d", 42);
 * @endcode
 *
 * @par Thread Safety:
 * All methods are thread-safe. Each thread gets its own file stream.
 *
 * @par Environment Variables:
 * - NDMSPC_LOG_LEVEL: Set minimum severity
 * - NDMSPC_LOG_FILE: Enable file output (1/true/TRUE)
 * - NDMSPC_LOG_CONSOLE: Enable console output (0/1/false/true)
 * - NDMSPC_LOG_DIR: Set log directory
 * - NDMSPC_PROCESS_NAME: Set process name
 * - NDMSPC_MAIN_THREAD_NAME: Set main thread name
 *
 * @section intro_sec Introduction
 *
 * NLogger is a thread-safe logging system for C++ applications with per-thread
 * log files, configurable severity levels, and flexible output options.
 *
 * @section features_sec Key Features
 *
 * - Thread-safe logging: Multiple threads can log simultaneously without conflicts
 * - Per-thread log files: Each thread writes to its own dedicated log file
 * - Configurable severity levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
 * - Multiple output modes: Console only, file only, both, or neither
 * - Custom naming: Set process names and thread names
 * - Environment variable configuration: Configure without code changes
 * - Millisecond-precision timestamps
 *
 * @section quick_start Quick Start
 *
 * @subsection basic_usage Basic Usage
 *
 * @code{.cpp}
 * #include "NLogger.h"
 *
 * int main() {
 *   // Simple logging (console by default)
 *   NLogInfo("Application started");
 *   NLogDebug("Debug: value = %d", 42);
 *   NLogWarning("Warning message");
 *   NLogError("Error: %s", error_msg);
 *   return 0;
 * }
 * @endcode
 *
 * @subsection multi_thread Multi-threaded Example
 *
 * @code{.cpp}
 * void worker(int id) {
 *   Ndmspc::NLogger::SetThreadName("Worker_" + std::to_string(id));
 *   NLogInfo("Worker %d started", id);
 * }
 *
 * int main() {
 *   Ndmspc::NLogger::SetProcessName("MyApp");
 *   Ndmspc::NLogger::SetFileOutput(true);
 *   Ndmspc::NLogger::SetLogDirectory("./logs");
 *
 *   std::thread t1(worker, 1);
 *   std::thread t2(worker, 2);
 *   t1.join();
 *   t2.join();
 *   return 0;
 * }
 * @endcode
 *
 * @section macros_sec Logging Macros
 *
 * @subsection available_macros Available Macros
 *
 * | Macro | Level | Description |
 * |-------|-------|-------------|
 * | NLogTrace(format, ...) | TRACE | Detailed trace information |
 * | NLogDebug(format, ...) | DEBUG | Debug info (includes file:line) |
 * | NLogInfo(format, ...) | INFO | Informational messages |
 * | NLogWarning(format, ...) | WARN | Warning messages |
 * | NLogError(format, ...) | ERROR | Error messages |
 * | NLogFatal(format, ...) | FATAL | Fatal error messages |
 *
 * @subsection generic_macro Generic Macro
 *
 * @code{.cpp}
 * NLog(level, format, ...)
 * // Example:
 * NLog(Ndmspc::logs::Severity::kDebug, "Custom: %d", value);
 * @endcode
 *
 * @section severity_sec Severity Levels
 *
 * Each level has 4 variants (e.g., TRACE, TRACE2, TRACE3, TRACE4):
 *
 * TRACE (most verbose) → DEBUG → INFO (default) → WARN → ERROR → FATAL (least verbose)
 *
 * @code{.cpp}
 * // Set minimum severity
 * Ndmspc::NLogger::SetMinSeverity(Ndmspc::logs::Severity::kDebug);
 * @endcode
 *
 * @section config_sec Configuration API
 *
 * @subsection naming Process and Thread Naming
 *
 * @code{.cpp}
 * // Set process name (prefix for all log files)
 * Ndmspc::NLogger::SetProcessName("MyApp");
 *
 * // Set thread name for current thread
 * Ndmspc::NLogger::SetThreadName("WorkerThread");
 *
 * // Get names
 * std::string proc = Ndmspc::NLogger::GetProcessName();
 * std::string thread = Ndmspc::NLogger::GetThreadName();
 * @endcode
 *
 * @subsection output_config Output Configuration
 *
 * @code{.cpp}
 * Ndmspc::NLogger::SetFileOutput(true);        // default: false
 * Ndmspc::NLogger::SetConsoleOutput(true);     // default: true
 * Ndmspc::NLogger::SetLogDirectory("./logs");  // default: "./logs"
 * Ndmspc::NLogger::SetMinSeverity(Ndmspc::logs::Severity::kDebug);
 * @endcode
 *
 * @section env_vars Environment Variables
 *
 * | Variable | Default | Description |
 * |----------|---------|-------------|
 * | NDMSPC_LOG_LEVEL | INFO | Minimum severity (TRACE/DEBUG/INFO/WARN/ERROR/FATAL) |
 * | NDMSPC_LOG_FILE | false | Enable file output (1/true/TRUE) |
 * | NDMSPC_LOG_CONSOLE | true | Enable console output (0/1/false/true) |
 * | NDMSPC_LOG_DIR | ./logs | Log directory path |
 * | NDMSPC_PROCESS_NAME | pid_<PID> | Process name prefix |
 * | NDMSPC_MAIN_THREAD_NAME | MainThread | Main thread name |
 *
 * @subsection env_examples Environment Variable Examples
 *
 * @code{.bash}
 * # Enable file logging with DEBUG level
 * NDMSPC_LOG_FILE=1 NDMSPC_LOG_LEVEL=DEBUG ./my_app
 *
 * # File only, no console
 * NDMSPC_LOG_FILE=1 NDMSPC_LOG_CONSOLE=0 ./my_app
 *
 * # Full configuration
 * NDMSPC_PROCESS_NAME=DataProcessor \
 * NDMSPC_LOG_LEVEL=DEBUG \
 * NDMSPC_LOG_FILE=1 \
 * NDMSPC_LOG_DIR=/var/log/myapp \
 * ./my_app
 * @endcode
 *
 * @section naming_sec Log File Naming
 *
 * Pattern: `<LogDirectory>/<ProcessName>_<ThreadName>.log`
 *
 * Examples:
 * - ./logs/MyApp_MainThread.log
 * - ./logs/MyApp_Worker_1.log
 * - ./logs/pid_12345_DatabaseThread.log
 *
 * @section format_sec Log Format
 *
 * Standard format:
 * @code
 * [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] Message
 * @endcode
 *
 * Debug format (includes file:line):
 * @code
 * [YYYY-MM-DD HH:MM:SS.mmm] [DEBUG] [main.cpp:42] Message
 * @endcode
 *
 * Each log file starts with:
 * @code
 * === Log started at Mon Dec 16 17:30:45 2024
 * === Process: MyApplication (PID: 12345)
 * === Thread: WorkerThread
 * @endcode
 *
 * @section use_cases Common Use Cases
 *
 * @subsection console_only Console Only (Default)
 *
 * @code{.cpp}
 * NLogInfo("Simple console logging");
 * @endcode
 *
 * @subsection file_only File Only
 *
 * @code{.cpp}
 * Ndmspc::NLogger::SetProcessName("MyApp");
 * Ndmspc::NLogger::SetFileOutput(true);
 * Ndmspc::NLogger::SetConsoleOutput(false);
 * NLogInfo("File only");
 * @endcode
 *
 * @subsection multi_thread_named Multi-threaded with Named Threads
 *
 * @code{.cpp}
 * void processor(int id) {
 *   Ndmspc::NLogger::SetThreadName("Processor_" + std::to_string(id));
 *   NLogInfo("Processing started");
 *   // ... work ...
 *   NLogInfo("Processing complete");
 * }
 *
 * int main() {
 *   Ndmspc::NLogger::SetProcessName("DataPipeline");
 *   Ndmspc::NLogger::SetFileOutput(true);
 *
 *   std::thread t1(processor, 1);
 *   std::thread t2(processor, 2);
 *   t1.join();
 *   t2.join();
 * }
 * @endcode
 *
 * Creates:
 * - DataPipeline_MainThread.log
 * - DataPipeline_Processor_1.log
 * - DataPipeline_Processor_2.log
 *
 * @section best_practices Best Practices
 *
 * 1. Set severity appropriately: Use DEBUG/TRACE for development, INFO+ for production
 * 2. Name your threads: Easier debugging and log analysis
 * 3. Avoid tight loops: Don't log in high-frequency loops
 * 4. Use environment variables: Easy configuration without recompilation
 *
 * @code{.cpp}
 * // Good
 * void worker_thread() {
 *   Ndmspc::NLogger::SetThreadName("Worker");  // Set once
 *   for (int i = 0; i < 1000000; ++i) {
 *     if (i % 10000 == 0) NLogDebug("Progress: %d", i);  // Periodic
 *   }
 * }
 *
 * // Bad
 * for (int i = 0; i < 1000000; ++i) {
 *   NLogTrace("Processing %d", i);  // Too much overhead!
 * }
 * @endcode
 *
 * @section troubleshooting Troubleshooting
 *
 * @subsection no_files No log files created
 *
 * @code{.cpp}
 * Ndmspc::NLogger::SetFileOutput(true);  // Ensure enabled
 * @endcode
 *
 * Or:
 *
 * @code{.bash}
 * NDMSPC_LOG_FILE=1 ./my_app
 * @endcode
 *
 * @subsection no_console No console output
 *
 * @code{.cpp}
 * Ndmspc::NLogger::SetConsoleOutput(true);
 * Ndmspc::NLogger::SetMinSeverity(Ndmspc::logs::Severity::kTrace);
 * @endcode
 *
 * Or:
 *
 * @code{.bash}
 * NDMSPC_LOG_CONSOLE=1 NDMSPC_LOG_LEVEL=TRACE ./my_app`
 * @endcode
 *
 * @subsection too_verbose Too verbose
 *
 * @code{.cpp}
 * Ndmspc::NLogger::SetMinSeverity(Ndmspc::logs::Severity::kWarn);
 * @endcode
 *
 * Or:
 *
 * @code{.bash}
 * NDMSPC_LOG_LEVEL=ERROR ./my_app`
 * @endcode
 *
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

  /**
   * @brief Logs a formatted message with the specified severity level.
   *
   * @param file The name of the source file where the log is generated.
   * @param line The line number in the source file where the log is generated.
   * @param level The severity level of the log message.
   * @param format The printf-style format string for the log message.
   * @param ... Additional arguments for the format string.
   */
  static void Log(const char * file, int line, logs::Severity level, const char * format, ...);

  /**
   * @brief Sets the minimum severity level for logging.
   * @param level The minimum severity level to be set.
   */
  static void SetMinSeverity(logs::Severity level) { fgMinSeverity = level; }

  /**
   * @brief Gets the current minimum severity level for logging.
   * @return The current minimum severity level.
   */
  static logs::Severity GetMinSeverity() { return fgMinSeverity; }

  /**
   * @brief Sets the directory where log files will be stored.
   * @param dir The path to the log directory.
   */
  static void SetLogDirectory(const std::string & dir);

  /**
   * @brief Enables or disables logging output to the console.
   * @param enable True to enable console output, false to disable.
   */
  static void SetConsoleOutput(bool enable) { fgConsoleOutput = enable; }

  /**
   * @brief Enables or disables logging output to a file.
   * @param enable True to enable file output, false to disable.
   */
  static void SetFileOutput(bool enable) { fgFileOutput = enable; } // New: Set process name (prefix for all log files)

  /**
   * @brief Sets the name of the current process.
   * @param name The name to assign to the process.
   */
  static void SetProcessName(const std::string & name);

  /**
   * @brief Retrieves the name of the current process.
   * @return The process name as a string.
   */
  static std::string GetProcessName() { return fgProcessName; }

  /**
   * @brief Sets the name of a thread.
   * @param name The name to assign to the thread.
   * @param thread_id The ID of the thread to name (defaults to current thread).
   */
  static void SetThreadName(const std::string & name, std::thread::id thread_id = std::this_thread::get_id());

  /**
   * @brief Retrieves the name of the current thread.
   * @return The thread name as a string.
   */
  static std::string GetThreadName();

  static bool         GetConsoleOutput() { return fgConsoleOutput; } ///< Get console output flag
  static bool         GetFileOutput() { return fgFileOutput; }       ///< Get file output flag
  static std::string  GetLogDirectory() { return fgLogDirectory; }   ///< Get log directory path
  static std::mutex & GetLoggerMutex() { return fgLoggerMutex; }     ///< Get logger mutex reference

  private:
  static std::mutex               fgLoggerMutex;   ///< Mutex for thread-safe singleton access
  static logs::Severity           fgMinSeverity;   ///< Minimum severity level for logging
  static std::string              fgLogDirectory;  ///< Directory for log files
  static bool                     fgConsoleOutput; ///< Flag for console output
  static bool                     fgFileOutput;    ///< Flag for file output
  static std::string              fgProcessName;   ///< Process name prefix for log files
  static std::unique_ptr<NLogger> fgLogger;        ///< Singleton instance

  /**
   * @brief Initializes the logger.
   */
  void Init();

  /**
   * @brief Cleans up logger resources.
   */
  void Cleanup();
  /**
   * @brief Retrieves the thread-local output file stream for logging.
   *
   * Ensures that each thread has its own output file stream for writing log messages,
   * creating a new stream if one does not already exist for the current thread.
   *
   * @return Reference to the thread's std::ofstream.
   */
  std::ofstream & GetThreadStream();

  /**
   * @brief Closes and removes the output file stream associated with a specific thread.
   *
   * @param thread_id The ID of the thread whose stream should be closed.
   */
  void CloseThreadStream(std::thread::id thread_id);

  /** Mutex for synchronizing access to the thread stream map. */
  std::mutex fStreamMapMutex;

  /**
   * @brief Map storing unique output file streams for each thread.
   *
   * Maps thread IDs to their corresponding std::ofstream instances.
   */
  std::unordered_map<std::thread::id, std::unique_ptr<std::ofstream>> fThreadStreams;
  std::unordered_map<std::thread::id, std::string> fThreadNames;          ///< Map of thread IDs to custom thread names
  std::string                                      GetThreadIdentifier(); ///< Get thread name or ID as string

  /// \cond CLASSIMP
  // ClassDefOverride(NLogger, 1);
  /// \endcond;
};
} // namespace Ndmspc
