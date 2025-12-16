#include <iostream>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <ctime>
#include <cstdarg>

#include "NLogger.h"

namespace Ndmspc {

// Singleton instance and mutex
std::mutex     NLogger::fgLoggerMutex;
logs::Severity NLogger::fgMinSeverity   = logs::Severity::kInfo;
std::string    NLogger::fgLogDirectory  = "/tmp/.ndmspc/logs";
bool           NLogger::fgConsoleOutput = true;
bool           NLogger::fgFileOutput    = false; // Default: no file logging
std::string    NLogger::fgProcessName   = "";

NLogger::NLogger()
{
  // Initialize the logger
  Init();
}

NLogger::~NLogger()
{
  Cleanup();
}

NLogger * NLogger::Instance()
{
  // Meyers' singleton for thread-safe, lazy initialization
  static NLogger instance;
  return &instance;
}

void NLogger::Init()
{
  // Init logger
  if (const char * env_severity = getenv("NDMSPC_LOG_LEVEL")) {
    fgMinSeverity = GetSeverityFromString(env_severity);
    if (fgMinSeverity != logs::Severity::kInfo) {
      std::cout << "NLogger: Setting log level to '" << env_severity << "' ... " << std::endl;
    }
  }
  if (const char * env_logdir = getenv("NDMSPC_LOG_DIR")) {
    fgLogDirectory = env_logdir;
  }

  // Check if process name is set via environment variable
  if (const char * env_process_name = getenv("NDMSPC_PROCESS_NAME")) {
    fgProcessName = env_process_name;
  }
  else {
    // Default to process ID if no process name is set
    fgProcessName = "ndmspc_" + std::to_string(getpid());
  }

  // Check if file output is enabled via environment variable
  if (const char * env_file_output = getenv("NDMSPC_LOG_FILE")) {
    std::string value(env_file_output);
    fgFileOutput = (value == "1" || value == "true" || value == "TRUE");
  } // Check if console output is controlled via environment variable

  if (const char * env_console_output = getenv("NDMSPC_LOG_CONSOLE")) {
    std::string value(env_console_output);
    if (value == "0" || value == "false" || value == "FALSE") {
      fgConsoleOutput = false;
    }
    else if (value == "1" || value == "true" || value == "TRUE") {
      fgConsoleOutput = true;
    }
  }

  // Set default name for main thread
  std::thread::id main_tid         = std::this_thread::get_id();
  std::string     main_thread_name = "main";

  // Check if main thread name is set via environment variable
  if (const char * env_main_thread = getenv("NDMSPC_MAIN_THREAD_NAME")) {
    main_thread_name = env_main_thread;
  }

  fThreadNames[main_tid] = main_thread_name;

  // Only create log directory if file output is enabled
  if (fgFileOutput) {
    try {
      std::filesystem::create_directories(fgLogDirectory);
    }
    catch (const std::exception & e) {
      std::cerr << "NLogger: Failed to create log directory: " << e.what() << std::endl;
      fgFileOutput = false; // Disable file output on error
    }
  }
}

void NLogger::Cleanup()
{
  // Cleanup logger
  std::lock_guard<std::mutex> lock(fStreamMapMutex);
  fThreadStreams.clear();
  fThreadNames.clear();
}

void NLogger::SetLogDirectory(const std::string & dir)
{
  fgLogDirectory = dir;
  if (fgFileOutput) {
    try {
      std::filesystem::create_directories(fgLogDirectory);
    }
    catch (const std::exception & e) {
      std::cerr << "NLogger: Failed to create log directory: " << e.what() << std::endl;
    }
  }
}

void NLogger::SetProcessName(const std::string & name)
{
  std::lock_guard<std::mutex> lock(fgLoggerMutex);
  fgProcessName = name;
}

void NLogger::SetThreadName(const std::string & name, std::thread::id thread_id)
{
  std::lock_guard<std::mutex> lock(Instance()->fStreamMapMutex);
  Instance()->fThreadNames[thread_id] = name;
}

std::string NLogger::GetThreadName()
{
  std::thread::id             tid = std::this_thread::get_id();
  std::lock_guard<std::mutex> lock(Instance()->fStreamMapMutex);

  auto it = Instance()->fThreadNames.find(tid);
  if (it != Instance()->fThreadNames.end()) {
    return it->second;
  }

  // Return thread ID as string if no custom name
  std::ostringstream oss;
  oss << tid;
  return oss.str();
}

std::string NLogger::GetThreadIdentifier()
{
  std::thread::id tid = std::this_thread::get_id();

  // Check if custom name exists (lock already held by caller)
  auto it = fThreadNames.find(tid);
  if (it != fThreadNames.end()) {
    return it->second;
  }

  // Return thread ID as string
  std::ostringstream oss;
  oss << tid;
  return oss.str();
}

std::ofstream & NLogger::GetThreadStream()
{
  std::thread::id tid = std::this_thread::get_id();

  std::lock_guard<std::mutex> lock(fStreamMapMutex);

  auto it = fThreadStreams.find(tid);
  if (it != fThreadStreams.end()) {
    return *(it->second);
  }

  // Get thread identifier (custom name or ID)
  std::string thread_id = GetThreadIdentifier();

  // Build filename with process name prefix
  std::string filename_base;
  if (!fgProcessName.empty()) {
    filename_base = fgProcessName + "_" + thread_id;
  }
  else {
    filename_base = thread_id;
  }

  // Sanitize filename (replace problematic characters)
  for (char & c : filename_base) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }

  // Create new log file for this thread
  std::ostringstream filename;
  filename << fgLogDirectory << "/" << filename_base << ".log";

  auto stream = std::make_unique<std::ofstream>(filename.str(), std::ios::app);

  if (!stream->is_open()) {
    std::cerr << "NLogger: Failed to open log file: " << filename.str() << std::endl;
  }
  else {
    // Write header with process and thread info
    auto        now      = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    *stream << "=== Log started at " << std::ctime(&now_time);
    *stream << "=== Process: " << fgProcessName << " (PID: " << getpid() << ")" << std::endl;
    *stream << "=== Thread: " << thread_id << std::endl;
    stream->flush();
  }

  auto & ref          = *stream;
  fThreadStreams[tid] = std::move(stream);
  return ref;
}
void NLogger::CloseThreadStream(std::thread::id thread_id)
{
  std::lock_guard<std::mutex> lock(fStreamMapMutex);
  fThreadStreams.erase(thread_id);
  fThreadNames.erase(thread_id);
}

std::string NLogger::SeverityToString(logs::Severity level)
{
  switch (level) {
  case logs::Severity::kTrace: return "TRACE";
  case logs::Severity::kDebug: return "DEBUG";
  case logs::Severity::kInfo: return "INFO";
  case logs::Severity::kWarn: return "WARN";
  case logs::Severity::kError: return "ERROR";
  case logs::Severity::kFatal: return "FATAL";
  default: return "UNKNOWN";
  }
}

logs::Severity NLogger::GetSeverityFromString(const std::string & severity_str)
{
  auto it = logs::fgSeverityMap.find(severity_str);
  if (it != logs::fgSeverityMap.end()) {
    return it->second;
  }
  // Default to INFO if unknown
  return logs::Severity::kInfo;
}

void NLogger::Log(const char * file, int line, logs::Severity level, const char * format, ...)
{
  if (Instance()->GetMinSeverity() > level) {
    return;
  }

  va_list args;
  va_start(args, format);

  // Format timestamp
  auto        now      = std::chrono::system_clock::now();
  std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm *   now_tm   = std::localtime(&now_time);
  char        time_buf[24];
  auto        ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", now_tm);

  // Format message
  char message_buf[4096];
  vsnprintf(message_buf, sizeof(message_buf), format, args);
  va_end(args);

  // Build log line
  std::ostringstream log_line;
  log_line << "[" << time_buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
           << "[" << SeverityToString(level) << "] ";

  if (level <= logs::Severity::kDebug4) {
    log_line << "[" << std::filesystem::path(file).filename().string() << ":" << line << "] ";
  }

  log_line << message_buf;

  // Thread-safe file output (only if enabled)
  if (fgFileOutput) {
    auto & stream = Instance()->GetThreadStream();
    if (stream.is_open()) {
      stream << log_line.str() << std::endl;
      stream.flush();
    }
  }

  // Console output (if enabled)
  if (fgConsoleOutput) {
    std::lock_guard<std::mutex> lock(fgLoggerMutex);
    std::cout << log_line.str() << std::endl;
  }
}

} // namespace Ndmspc
