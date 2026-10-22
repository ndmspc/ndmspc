// Usage recording for the room router: append-only CSV events and the aggregates the stats
// endpoints answer with. NRoomStats.h says what is recorded and what the numbers mean.

#include "NRoomStats.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace Ndmspc {

namespace {

/// @brief Reads an environment variable, falling back when it is unset or empty.
std::string StatsEnv(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

/// @brief Parses 1/0, true/false, yes/no, on/off; anything else keeps the fallback.
bool StatsBool(const std::string & text, bool fallback)
{
  std::string value;
  for (char c : text) value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
  if (value == "0" || value == "false" || value == "no" || value == "off") return false;
  return fallback;
}

/// @brief Parses "<n>[smhd]" into seconds.
long StatsDuration(const std::string & text, long fallback)
{
  if (text.empty()) return fallback;
  char *     end   = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == nullptr || end == text.c_str()) return fallback;
  const std::string unit(end);
  if (unit.empty() || unit == "s") return value;
  if (unit == "m") return value * 60;
  if (unit == "h") return value * 3600;
  if (unit == "d") return value * 86400;
  return fallback;
}

/// @brief Trims surrounding whitespace (CSV headers written by other tools).
std::string Trim(const std::string & text)
{
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

/// @brief Days since 1970-01-01 for a civil date (no dependency on the C library's timezone).
long DaysFromCivil(int year, unsigned month, unsigned day)
{
  year -= month <= 2;
  const int      era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<long>(era) * 146097 + static_cast<long>(doe) - 719468;
}

/// @brief Epoch seconds of a YYYY-MM-DD date at midnight UTC (0 when it does not parse).
long DateEpoch(const std::string & date)
{
  int year = 0, month = 0, day = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return 0;
  if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
  return DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400;
}

/// @brief Midnight UTC of a timestamp (epoch seconds).
long DayStart(long ts)
{
  const long day = 86400;
  long       value = ts % day;
  if (value < 0) value += day;
  return ts - value;
}

/// @brief Whether an event counts as a user being active (for "distinct users").
bool IsSeenEvent(const std::string & event)
{
  return event == "connect" || event == "ui_seen" || event == "ui_online";
}

/// @brief Whether an event is a room lifecycle event worth counting.
bool IsRoomEvent(const std::string & event)
{
  return event == "open" || event == "close" || event == "expire" || event == "fail" || event == "upgrade";
}

} // namespace

NRoomStatsConfig NRoomStatsConfig::FromEnv()
{
  NRoomStatsConfig config;
  config.enabled            = StatsBool(StatsEnv("NDMSPC_STATS", "true"), true);
  config.dir                = StatsEnv("NDMSPC_STATS_DIR", "");
  config.retentionDays      = StatsDuration(StatsEnv("NDMSPC_STATS_RETENTION", "90d"), 90 * 86400) / 86400;
  config.summaryTtlSec      = StatsDuration(StatsEnv("NDMSPC_STATS_SUMMARY_TTL", "30s"), 30);
  config.publicSummary      = StatsBool(StatsEnv("NDMSPC_STATS_PUBLIC", "false"), false);
  config.presenceIntervalSec = StatsDuration(StatsEnv("NDMSPC_STATS_PRESENCE_INTERVAL", "60s"), 60);
  if (config.retentionDays < 0) config.retentionDays = 0;
  if (config.summaryTtlSec < 0) config.summaryTtlSec = 0;
  if (config.presenceIntervalSec < 1) config.presenceIntervalSec = 1;
  return config;
}

NRoomStats::NRoomStats(NRoomStatsConfig config)
{
  Configure(config);
}

void NRoomStats::Configure(const NRoomStatsConfig & config)
{
  std::lock_guard<std::mutex> lock(fMutex);
  ConfigureLocked(config);
}

void NRoomStats::ConfigureLocked(const NRoomStatsConfig & config)
{
  fConfig  = config;
  fEnabled = false;
  fWarned  = false;
  fSummaryCache.clear();
  fPresence.clear();
  fSeen.clear();
  if (!fConfig.enabled) return;

  if (fConfig.dir.empty()) fConfig.dir = DefaultDir();

  std::error_code ec;
  std::filesystem::create_directories(fConfig.dir, ec);
  if (ec) {
    if (!fWarned) {
      NLogWarning("[stats] cannot create '%s': %s; usage is not recorded", fConfig.dir.c_str(), ec.message().c_str());
      fWarned = true;
    }
    return;
  }

  // Probe once: a directory that exists but is not writable (a volume mounted with the wrong
  // ownership, a read-only filesystem) disables recording instead of failing every room call.
  const std::string probe = fConfig.dir + "/.write-test";
  {
    std::ofstream out(probe, std::ios::app);
    if (!out) {
      if (!fWarned) {
        NLogWarning("[stats] '%s' is not writable; usage is not recorded", fConfig.dir.c_str());
        fWarned = true;
      }
      return;
    }
  }
  std::error_code ignored;
  std::filesystem::remove(probe, ignored);

  fEnabled = true;
  fLastPruneDay = 0;
  NLogInfo("[stats] recording usage in %s (retention %ldd)", fConfig.dir.c_str(), fConfig.retentionDays);
}

std::string NRoomStats::DefaultDir()
{
  std::error_code ec;
  std::string     cwd = std::filesystem::current_path(ec).string();
  if (ec || cwd.empty()) cwd = "/tmp";
  return cwd + "/.ndmspc/stats";
}

void NRoomStats::SetClock(Clock clock)
{
  std::lock_guard<std::mutex> lock(fMutex);
  fClock = std::move(clock);
  fSummaryCache.clear();
}

long NRoomStats::Now() const
{
  if (fClock) return fClock();
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

bool NRoomStats::Enabled() const
{
  std::lock_guard<std::mutex> lock(fMutex);
  return fEnabled;
}

bool NRoomStats::Public() const
{
  std::lock_guard<std::mutex> lock(fMutex);
  return fEnabled && fConfig.publicSummary;
}

std::string NRoomStats::Dir() const
{
  std::lock_guard<std::mutex> lock(fMutex);
  return fEnabled ? fConfig.dir : std::string();
}

std::string NRoomStats::DayKey(long ts)
{
  const std::time_t time = static_cast<std::time_t>(ts);
  std::tm           tm{};
  gmtime_r(&time, &tm);
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
  return buffer;
}

std::string NRoomStats::IsoTime(long ts)
{
  const std::time_t time = static_cast<std::time_t>(ts);
  std::tm           tm{};
  gmtime_r(&time, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

std::string NRoomStats::CsvField(const std::string & value)
{
  if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
  std::string out = "\"";
  for (char c : value) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += '"';
  return out;
}

std::vector<std::string> NRoomStats::SplitCsv(const std::string & line)
{
  std::vector<std::string> fields;
  std::string              field;
  bool                     quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
        field += '"';
        ++i;
      }
      else if (c == '"') {
        quoted = false;
      }
      else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c == ',') {
      fields.push_back(field);
      field.clear();
      continue;
    }
    field += c;
  }
  fields.push_back(field);
  return fields;
}

std::string NRoomStats::Header(const std::string & family)
{
  if (family == "rooms") return "ts,time,event,room,owner,profile,image,image_tag,server_version,detail";
  return "ts,time,event,room,user,role,client,client_version,server_version,session,detail";
}

const std::string & NRoomStats::Get(const std::map<std::string, std::string> & fields, const std::string & name)
{
  static const std::string empty;
  const auto               it = fields.find(name);
  return it == fields.end() ? empty : it->second;
}

long NRoomStats::ParseDetail(const std::string & detail, const std::string & key)
{
  size_t start = 0;
  while (start <= detail.size()) {
    const size_t end  = detail.find(';', start);
    const std::string part = detail.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const size_t equal = part.find('=');
    if (equal != std::string::npos && part.substr(0, equal) == key) {
      return std::strtol(part.c_str() + equal + 1, nullptr, 10);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return 0;
}

bool NRoomStats::ParseRange(const std::string & range, long now, long & from, long & to, long & bucket,
                            std::string & name)
{
  to     = now;
  bucket = 86400;
  if (range.empty() || range == "24h" || range == "1d" || range == "day") {
    name   = "24h";
    from   = now - 86400;
    bucket = 3600;
    return true;
  }
  if (range == "7d" || range == "week") {
    name = "7d";
    from = now - 7 * 86400;
    return true;
  }
  if (range == "30d" || range == "month") {
    name = "30d";
    from = now - 30 * 86400;
    return true;
  }
  // Everything still on disk (retention is the horizon). The caller replaces `from` with the oldest
  // file's day once it holds the recorder's lock.
  if (range == "all" || range == "total") {
    name   = "all";
    from   = 0;
    bucket = 86400;
    return true;
  }

  // A generic "<n>[hd]" is accepted, so a deployment can ask for another window without a new
  // build; the response says which window it answered for.
  const long seconds = StatsDuration(range, 0);
  if (seconds <= 0) return false;
  name   = range;
  from   = now - seconds;
  bucket = seconds <= 2 * 86400 ? 3600 : 86400;
  return true;
}

long NRoomStats::FirstDayOnDisk() const
{
  long            first = 0;
  std::error_code ec;
  if (!std::filesystem::is_directory(fConfig.dir, ec)) return 0;
  for (const auto & entry : std::filesystem::directory_iterator(fConfig.dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    const auto        dash = name.find('-');
    if (dash == std::string::npos || name.size() < dash + 11) continue;
    const std::string family = name.substr(0, dash);
    if (family != "rooms" && family != "users") continue;
    const long day = DateEpoch(name.substr(dash + 1, 10));
    if (day > 0 && (first == 0 || day < first)) first = day;
  }
  return first;
}

void NRoomStats::PruneLocked(long now)
{
  const long today = DayStart(now);
  if (fLastPruneDay == today) return;
  fLastPruneDay = today;
  if (fConfig.retentionDays <= 0) return; // keep everything

  const long cutoff = today - fConfig.retentionDays * 86400;
  std::error_code ec;
  std::vector<std::filesystem::path> stale;
  for (const auto & entry : std::filesystem::directory_iterator(fConfig.dir, ec)) {
    if (ec) return;
    if (!entry.is_regular_file()) continue;
    const std::string filename = entry.path().filename().string();
    const auto        dash     = filename.find('-');
    if (dash == std::string::npos) continue;
    const std::string family = filename.substr(0, dash);
    if (family != "rooms" && family != "users") continue;
    if (filename.size() < dash + 11) continue;
    const std::string date = filename.substr(dash + 1, 10);
    const long        day  = DateEpoch(date);
    if (day > 0 && day < cutoff) stale.push_back(entry.path());
  }
  for (const auto & path : stale) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
}

bool NRoomStats::AppendLocked(const std::string & family, const std::vector<std::string> & fields, long ts)
{
  if (!fEnabled) return false;
  PruneLocked(ts);

  const std::string path = fConfig.dir + "/" + family + "-" + DayKey(ts) + ".csv";
  std::error_code   ec;
  const bool        exists = std::filesystem::exists(path, ec) && !ec;

  std::ofstream out(path, std::ios::app);
  if (!out) {
    if (!fWarned) {
      NLogWarning("[stats] cannot write '%s'; usage is not recorded", path.c_str());
      fWarned = true;
    }
    return false;
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (!exists || ec || size == 0) out << Header(family) << '\n';

  std::string line;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) line += ',';
    line += CsvField(fields[i]);
  }
  out << line << '\n';
  return static_cast<bool>(out);
}

void NRoomStats::RoomEvent(const std::string & event, const std::string & room, const std::string & owner,
                           const std::string & profile, const std::string & image, const std::string & imageTag,
                           const std::string & serverVersion, const std::string & detail)
{
  if (!IsRoomEvent(event)) return;
  std::lock_guard<std::mutex> lock(fMutex);
  const long                  now = Now();
  AppendLocked("rooms", {std::to_string(now), IsoTime(now), event, room, owner, profile, image, imageTag,
                         serverVersion, detail},
               now);
}

void NRoomStats::UserEvent(const std::string & event, const std::string & room, const std::string & user,
                           const std::string & role, const std::string & client, const std::string & clientVersion,
                           const std::string & serverVersion, const std::string & session,
                           const std::string & detail)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const long                  now = Now();
  AppendLocked("users", {std::to_string(now), IsoTime(now), event, room, user, role, client, clientVersion,
                         serverVersion, session, detail},
               now);
}

int NRoomStats::RecordUsage(const json & usage)
{
  if (!usage.is_array()) return 0;
  std::lock_guard<std::mutex> lock(fMutex);
  const long                  now = Now();
  int                         recorded = 0;
  for (const auto & entry : usage) {
    if (!entry.is_object()) continue;
    std::string event = entry.value("event", std::string());
    if (event.empty()) event = entry.value("type", std::string());
    if (event.empty()) continue;

    std::string session;
    if (entry.contains("session")) {
      if (entry["session"].is_string()) session = entry["session"].get<std::string>();
      else if (entry["session"].is_number_integer()) session = std::to_string(entry["session"].get<long>());
      else if (entry["session"].is_number_unsigned()) session = std::to_string(entry["session"].get<unsigned long>());
    }
    std::string detail = entry.value("detail", std::string());
    if (detail.empty() && entry.contains("messages") && entry["messages"].is_number_integer()) {
      detail = "messages=" + std::to_string(entry["messages"].get<long>());
    }
    const std::string tool = entry.value("tool", std::string());
    if (!tool.empty()) {
      if (!detail.empty() && detail.back() != ';') detail += ';';
      detail += "tool=" + tool;
    }

    const bool written =
        AppendLocked("users",
                     {std::to_string(now), IsoTime(now), event, entry.value("room", std::string()),
                      entry.value("user", std::string()), entry.value("role", std::string()),
                      entry.value("client", std::string()), entry.value("client_version", std::string()),
                      entry.value("server_version", std::string()), session, detail},
                     now);
    if (written) ++recorded;
  }
  return recorded;
}

bool NRoomStats::Presence(const std::string & user, const std::string & role, const std::string & instance,
                          const std::string & state, const std::string & client, const std::string & clientVersion)
{
  if (user.empty() || instance.empty()) return false;

  std::string normalized = state;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (normalized == "online" || normalized == "active") normalized = "visible";
  if (normalized != "visible" && normalized != "hidden" && normalized != "offline") return false;

  std::lock_guard<std::mutex> lock(fMutex);
  const long                  now = Now();
  const auto                  it  = fPresence.find(instance);

  auto row = [&](const std::string & event, const std::string & previous, long duration) {
    const std::string detail =
        "previous=" + previous + ";duration_s=" + std::to_string(std::max<long>(0, duration));
    AppendLocked("users", {std::to_string(now), IsoTime(now), event, "", user, role, client, clientVersion, "",
                           instance, detail},
                 now);
  };

  if (it == fPresence.end()) {
    if (normalized == "offline") return false; // a session that was never seen is nothing to close
    PresenceEntry entry;
    entry.user          = user;
    entry.role          = role;
    entry.client        = client;
    entry.clientVersion = clientVersion;
    entry.state         = normalized;
    entry.since         = now;
    entry.lastSeen      = now;
    fPresence[instance] = entry;
    row("ui_online", "none", 0);
    return true;
  }

  it->second.lastSeen      = now;
  it->second.client        = client.empty() ? it->second.client : client;
  it->second.clientVersion = clientVersion.empty() ? it->second.clientVersion : clientVersion;
  it->second.role          = role.empty() ? it->second.role : role;

  if (it->second.state == normalized) return true;

  const long duration   = now - it->second.since;
  const std::string previous = it->second.state;
  if (normalized == "offline") {
    row("ui_offline", previous, duration);
    fPresence.erase(it);
    return true;
  }
  row(normalized == "hidden" ? "ui_hidden" : "ui_visible", previous, duration);
  it->second.state = normalized;
  it->second.since = now;
  return true;
}

void NRoomStats::SeenUi(const std::string & user, const std::string & role, const std::string & client,
                        const std::string & clientVersion)
{
  if (user.empty()) return;
  std::lock_guard<std::mutex> lock(fMutex);
  if (!fEnabled) return;
  const long now  = Now();
  const auto it   = fSeen.find(user);
  const long gap  = 15 * 60;
  if (it != fSeen.end() && now - it->second < gap) return;
  fSeen[user] = now;
  AppendLocked("users",
               {std::to_string(now), IsoTime(now), "ui_seen", "", user, role, client, clientVersion, "", "", ""},
               now);
}

void NRoomStats::SweepPresence()
{
  std::lock_guard<std::mutex> lock(fMutex);
  if (!fEnabled || fPresence.empty()) return;
  const long now   = Now();
  const long limit = std::max<long>(1, fConfig.presenceIntervalSec) * 3;

  for (auto it = fPresence.begin(); it != fPresence.end();) {
    if (now - it->second.lastSeen < limit) {
      ++it;
      continue;
    }
    // The tab stopped reporting, so the state it was last in ended at its last report (not now:
    // the silent gap is not evidence that it was still in use).
    const std::string detail = "previous=" + it->second.state + ";duration_s=" +
                               std::to_string(std::max<long>(0, it->second.lastSeen - it->second.since)) +
                               ";stale=1";
    AppendLocked("users",
                 {std::to_string(it->second.lastSeen), IsoTime(it->second.lastSeen), "ui_offline", "",
                  it->second.user, it->second.role, it->second.client, it->second.clientVersion, "",
                  it->first, detail},
                 it->second.lastSeen);
    it = fPresence.erase(it);
  }
}

std::vector<NRoomStats::Row> NRoomStats::LoadRows(const std::string & family, long from, long to) const
{
  std::vector<Row> rows;
  std::error_code  ec;
  if (!std::filesystem::is_directory(fConfig.dir, ec)) return rows;

  const long firstDay = DayStart(from) - 86400;
  const long lastDay  = DayStart(to) + 86400;

  std::vector<std::filesystem::path> paths;
  for (const auto & entry : std::filesystem::directory_iterator(fConfig.dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.rfind(family + "-", 0) != 0 || name.size() < family.size() + 15 ||
        name.substr(name.size() - 4) != ".csv") {
      continue;
    }
    const long day = DateEpoch(name.substr(family.size() + 1, 10));
    if (day <= 0 || day < firstDay || day > lastDay) continue;
    paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());

  for (const auto & path : paths) {
    std::ifstream in(path);
    std::string   line;
    if (!in || !std::getline(in, line)) continue;

    const std::vector<std::string> header = SplitCsv(line);
    std::map<std::string, size_t>  index;
    for (size_t i = 0; i < header.size(); ++i) index[Trim(header[i])] = i;

    auto value = [&index](const std::vector<std::string> & fields, const char * name) {
      const auto it = index.find(name);
      return it == index.end() || it->second >= fields.size() ? std::string() : fields[it->second];
    };

    while (std::getline(in, line)) {
      if (line.empty()) continue;
      const std::vector<std::string> fields = SplitCsv(line);
      Row                            row;
      const std::string              ts = value(fields, "ts");
      if (ts.empty()) continue;
      row.ts = std::atol(ts.c_str());
      if (row.ts < firstDay || row.ts > lastDay) continue;
      row.event          = value(fields, "event");
      row.room           = value(fields, "room");
      row.owner          = value(fields, "owner");
      row.user           = value(fields, "user");
      row.role           = value(fields, "role");
      row.profile        = value(fields, "profile");
      row.image          = value(fields, "image");
      row.imageTag       = value(fields, "image_tag");
      row.client         = value(fields, "client");
      row.clientVersion  = value(fields, "client_version");
      row.serverVersion  = value(fields, "server_version");
      row.session        = value(fields, "session");
      row.detail         = value(fields, "detail");
      if (row.event.empty()) continue;
      rows.push_back(std::move(row));
    }
  }

  std::sort(rows.begin(), rows.end(), [](const Row & a, const Row & b) { return a.ts < b.ts; });
  return rows;
}

json NRoomStats::Summary(const std::string & range)
{
  long        from = 0;
  long        to   = 0;
  long        bucket = 0;
  std::string name;
  const long  now = Now();
  if (!ParseRange(range, now, from, to, bucket, name)) {
    return json{{"enabled", Enabled()},
                {"error", "Unknown range '" + range + "' (use 24h, 7d, 30d or all)"}};
  }
  SweepPresence();

  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fSummaryCache.find(name);
    if (it != fSummaryCache.end() && now < it->second.first) return it->second.second;
  }

  std::lock_guard<std::mutex> lock(fMutex);
  if (!fEnabled) return json{{"enabled", false}, {"range", {{"name", name}}}};

  const auto cached = fSummaryCache.find(name);
  if (cached != fSummaryCache.end() && now < cached->second.first) return cached->second.second;

  if (name == "all") {
    from   = FirstDayOnDisk();
    if (from <= 0) from = to - 86400;
    bucket = (to - from) <= 366 * 86400 ? 86400 : 7 * 86400;
  }

  json summary = SummaryLocked(from, to, bucket, name);
  fSummaryCache[name] = {now + fConfig.summaryTtlSec, summary};
  return summary;
}

json NRoomStats::SummaryLocked(long from, long to, long bucket, const std::string & name)
{
  const long usersFrom = std::min(from, to - 30 * 86400);
  const auto users     = LoadRows("users", usersFrom, to);
  const auto rooms     = LoadRows("rooms", from, to);

  // --- distinct users, per window -------------------------------------------------------------
  auto distinct = [&](long windowFrom) {
    std::set<std::string> seen;
    for (const auto & row : users) {
      if (row.ts >= windowFrom && row.ts <= to && !row.user.empty() && IsSeenEvent(row.event)) seen.insert(row.user);
    }
    return static_cast<int>(seen.size());
  };

  // --- user room sessions ---------------------------------------------------------------------
  std::map<std::string, long> openSessions;
  std::vector<long>           sessionSeconds;
  long                        totalSessionSeconds = 0;
  std::set<std::string>       roomUsers;
  for (const auto & row : users) {
    const std::string key = row.room + '\x1f' + row.user + '\x1f' + row.session;
    if (row.event == "connect" && !row.user.empty()) {
      if (!openSessions.count(key)) openSessions[key] = row.ts;
      if (row.ts >= from) roomUsers.insert(row.user);
    }
    else if (row.event == "disconnect") {
      const auto it = openSessions.find(key);
      if (it == openSessions.end()) continue;
      const long start = std::max<long>(it->second, from);
      const long end   = std::min<long>(row.ts, to);
      if (end > start) {
        sessionSeconds.push_back(end - start);
        totalSessionSeconds += end - start;
      }
      openSessions.erase(it);
    }
  }
  for (const auto & entry : openSessions) {
    const long start = std::max<long>(entry.second, from);
    if (to > start) {
      sessionSeconds.push_back(to - start);
      totalSessionSeconds += to - start;
    }
  }

  // --- rooms: lifecycle, lifetime, occupancy and idle ------------------------------------------
  struct RoomAgg {
    long        openSince{-1};
    std::string profile;
    std::string imageTag;
    std::string serverVersion;
  };
  std::map<std::string, RoomAgg> roomAgg;
  int                            opened = 0, closed = 0, expired = 0, failed = 0, upgraded = 0;
  for (const auto & row : rooms) {
    if (row.event == "open") {
      ++opened;
      RoomAgg & agg = roomAgg[row.room];
      if (agg.openSince < 0) agg.openSince = row.ts;
      if (!row.profile.empty()) agg.profile = row.profile;
      if (!row.imageTag.empty()) agg.imageTag = row.imageTag;
      if (!row.serverVersion.empty()) agg.serverVersion = row.serverVersion;
    }
    else if (row.event == "upgrade") {
      ++upgraded;
      RoomAgg & agg = roomAgg[row.room];
      if (agg.openSince < 0) agg.openSince = row.ts;
      if (!row.imageTag.empty()) agg.imageTag = row.imageTag;
    }
    else if (row.event == "close" || row.event == "expire") {
      if (row.event == "close") ++closed;
      else ++expired;
      auto it = roomAgg.find(row.room);
      if (it != roomAgg.end() && it->second.openSince >= 0) {
        it->second.openSince = -1;
      }
    }
    else if (row.event == "fail") {
      ++failed;
    }
  }

  // Room intervals still open at `to`, clipped to the window.
  std::map<std::string, std::vector<Interval>> roomIntervals;
  for (auto & entry : roomAgg) {
    std::vector<Interval> intervals;
    long                  since = -1;
    for (const auto & row : rooms) {
      if (row.room != entry.first) continue;
      if (row.event == "open") {
        if (since < 0) since = row.ts;
      }
      else if (row.event == "close" || row.event == "expire") {
        if (since >= 0) {
          intervals.push_back({since, row.ts});
          since = -1;
        }
      }
    }
    if (since >= 0) intervals.push_back({since, to});
    std::vector<Interval> clipped;
    for (const auto & interval : intervals) {
      const long start = std::max<long>(interval.from, from);
      const long end   = std::min<long>(interval.to, to);
      if (end > start) clipped.push_back({start, end});
    }
    if (!clipped.empty()) roomIntervals[entry.first] = std::move(clipped);
  }

  // Occupancy per room: every user session, clipped to the window, merged. A room is active only
  // while somebody is connected to it; an open but empty room is idle, not active.
  std::map<std::string, long>              openByRoomSession;
  std::map<std::string, std::vector<Interval>> occupancy;
  for (const auto & row : users) {
    if (row.room.empty()) continue;
    const std::string key = row.room + '\x1f' + row.session;
    if (row.event == "connect") {
      if (!openByRoomSession.count(key)) openByRoomSession[key] = row.ts;
    }
    else if (row.event == "disconnect") {
      const auto it = openByRoomSession.find(key);
      if (it == openByRoomSession.end()) continue;
      occupancy[row.room].push_back({it->second, row.ts});
      openByRoomSession.erase(it);
    }
  }
  std::set<std::string> activeRooms;
  for (const auto & entry : openByRoomSession) {
    const std::string room = entry.first.substr(0, entry.first.find('\x1f'));
    if (room.empty()) continue;
    activeRooms.insert(room);
    occupancy[room].push_back({entry.second, to});
  }
  const int activeNow = static_cast<int>(activeRooms.size());

  long roomCount = 0;
  long totalLifetime = 0;
  long totalIdle = 0;
  std::map<std::string, int> byProfile;
  std::map<std::string, int> byImageTag;
  for (const auto & entry : roomIntervals) {
    const auto & intervals  = entry.second;
    const auto   occIt      = occupancy.find(entry.first);
    std::vector<Interval> merged;
    if (occIt != occupancy.end()) {
      for (auto interval : occIt->second) {
        interval.from = std::max<long>(interval.from, from);
        interval.to   = std::min<long>(interval.to, to);
        if (interval.to > interval.from) merged.push_back(interval);
      }
      std::sort(merged.begin(), merged.end(), [](const Interval & a, const Interval & b) { return a.from < b.from; });
      std::vector<Interval> combined;
      for (const auto & interval : merged) {
        if (!combined.empty() && interval.from <= combined.back().to) {
          combined.back().to = std::max(combined.back().to, interval.to);
        }
        else {
          combined.push_back(interval);
        }
      }
      merged.swap(combined);
    }

    for (const auto & interval : intervals) {
      ++roomCount;
      const long length = interval.to - interval.from;
      totalLifetime += length;
      long busy = 0;
      for (const auto & occupied : merged) {
        const long start = std::max<long>(occupied.from, interval.from);
        const long end   = std::min<long>(occupied.to, interval.to);
        if (end > start) busy += end - start;
      }
      totalIdle += std::max<long>(0, length - busy);
    }

    const auto agg = roomAgg.find(entry.first);
    if (agg != roomAgg.end()) {
      if (!agg->second.profile.empty()) byProfile[agg->second.profile]++;
      if (!agg->second.imageTag.empty()) byImageTag[agg->second.imageTag]++;
    }
  }

  // --- UI sessions and idle -------------------------------------------------------------------
  struct UiAgg {
    long        online{-1};
    long        hidden{-1};
    int         sessions{0};
    long        totalSeconds{0};
    long        idleSeconds{0};
    std::string user;
    std::string client;
    std::string clientVersion;
  };
  std::map<std::string, UiAgg> ui; // user + instance
  std::set<std::string>        uiUsers;
  for (const auto & row : users) {
    if (row.event.rfind("ui_", 0) != 0) continue;
    const std::string key = row.user + '\x1f' + row.session;
    UiAgg &           agg = ui[key];
    agg.user              = row.user;
    if (!row.client.empty()) agg.client = row.client;
    if (!row.clientVersion.empty()) agg.clientVersion = row.clientVersion;
    if (row.event == "ui_online") {
      if (agg.online < 0) {
        agg.online = row.ts;
        ++agg.sessions;
      }
      agg.hidden = -1;
      if (row.ts >= from) uiUsers.insert(row.user);
    }
    else if (row.event == "ui_hidden") {
      if (agg.hidden < 0) agg.hidden = row.ts;
    }
    else if (row.event == "ui_visible") {
      if (agg.hidden >= 0) {
        agg.idleSeconds += std::max<long>(0, row.ts - std::max<long>(agg.hidden, from));
        agg.hidden = -1;
      }
    }
    else if (row.event == "ui_offline") {
      if (agg.hidden >= 0) {
        agg.idleSeconds += std::max<long>(0, row.ts - std::max<long>(agg.hidden, from));
        agg.hidden = -1;
      }
      if (agg.online >= 0) {
        agg.totalSeconds += std::max<long>(0, row.ts - std::max<long>(agg.online, from));
        agg.online = -1;
      }
    }
  }
  long uiSessions = 0;
  long uiSeconds  = 0;
  long uiIdle     = 0;
  for (auto & entry : ui) {
    UiAgg & agg = entry.second;
    if (agg.online >= 0) {
      agg.totalSeconds += std::max<long>(0, to - std::max<long>(agg.online, from));
      agg.online = -1;
    }
    if (agg.hidden >= 0) {
      agg.idleSeconds += std::max<long>(0, to - std::max<long>(agg.hidden, from));
      agg.hidden = -1;
    }
    uiSessions += agg.sessions;
    uiSeconds += agg.totalSeconds;
    uiIdle += agg.idleSeconds;
  }
  for (const auto & row : users) {
    if (row.event == "ui_seen" && row.ts >= from && !row.user.empty()) uiUsers.insert(row.user);
  }

  // --- versions -------------------------------------------------------------------------------
  std::map<std::string, int> clientVersions;
  std::map<std::string, int> serverVersions;
  std::map<std::string, int> imageTags;
  for (const auto & row : users) {
    if (!row.clientVersion.empty()) clientVersions[row.clientVersion]++;
    if (!row.serverVersion.empty()) serverVersions[row.serverVersion]++;
  }
  for (const auto & row : rooms) {
    if (!row.serverVersion.empty()) serverVersions[row.serverVersion]++;
    if (!row.imageTag.empty()) imageTags[row.imageTag]++;
  }

  // What people actually ran: one `tool` row per successful action, its key in `detail` as
  // `tool=<action>` (the CSV keeps its fixed columns).
  std::map<std::string, int> toolCounts;
  for (const auto & row : users) {
    if (row.event != "tool" || row.detail.rfind("tool=", 0) != 0) continue;
    std::string name = row.detail.substr(5);
    const auto  end  = name.find(';');
    if (end != std::string::npos) name.resize(end);
    if (!name.empty()) toolCounts[name]++;
  }

  // --- series ---------------------------------------------------------------------------------
  struct Bucket {
    std::set<std::string> users;
    int                   sessions{0};
    int                   rooms{0};
  };
  std::map<long, Bucket> buckets;
  auto                   bucketOf = [bucket](long ts) { return ts - (ts % bucket); };
  for (const auto & row : users) {
    if (row.ts < from || row.ts > to) continue;
    if (row.event == "connect" || row.event == "ui_online") {
      Bucket & b = buckets[bucketOf(row.ts)];
      if (!row.user.empty()) b.users.insert(row.user);
      ++b.sessions;
    }
    else if (row.event == "ui_seen" && !row.user.empty()) {
      buckets[bucketOf(row.ts)].users.insert(row.user);
    }
  }
  for (const auto & row : rooms) {
    if (row.ts >= from && row.ts <= to && row.event == "open") ++buckets[bucketOf(row.ts)].rooms;
  }
  json series = json::array();
  for (long start = bucketOf(from); start <= to; start += bucket) {
    const Bucket & b = buckets[start];

    // Room state at the end of the bucket: active means somebody is connected, idle means the
    // room is open but empty. Sampled at the last second of the bucket so the latest bucket shows
    // the state now rather than at its beginning.
    long at = std::min(start + bucket, to);
    if (at >= to && to > from) at = to - 1;
    if (at < from) at = from;
    int activeRooms = 0;
    int idleRooms   = 0;
    for (const auto & entry : roomIntervals) {
      bool open = false;
      for (const auto & interval : entry.second) {
        if (interval.from <= at && at < interval.to) {
          open = true;
          break;
        }
      }
      if (!open) continue;
      bool busy = false;
      const auto occIt = occupancy.find(entry.first);
      if (occIt != occupancy.end()) {
        for (const auto & interval : occIt->second) {
          if (interval.from <= at && at < interval.to) {
            busy = true;
            break;
          }
        }
      }
      if (busy) ++activeRooms;
      else ++idleRooms;
    }

    series.push_back(json{{"ts", start},
                          {"users", static_cast<int>(b.users.size())},
                          {"sessions", b.sessions},
                          {"rooms", b.rooms},
                          {"activeRooms", activeRooms},
                          {"idleRooms", idleRooms}});
  }

  auto average = [](long total, long count) {
    return count <= 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(count);
  };

  json summary;
  summary["enabled"] = true;
  summary["range"]   = {{"name", name}, {"from", from}, {"to", to}, {"bucket", bucket}};
  summary["users"]   = {{"distinct", distinct(from)},
                        {"last24h", distinct(to - 86400)},
                        {"last7d", distinct(to - 7 * 86400)},
                        {"last30d", distinct(to - 30 * 86400)},
                        {"inRooms", static_cast<int>(roomUsers.size())},
                        {"sessions", static_cast<int>(sessionSeconds.size())},
                        {"totalSessionSeconds", totalSessionSeconds},
                        {"avgSessionSeconds", average(totalSessionSeconds, static_cast<long>(sessionSeconds.size()))}};
  summary["rooms"]   = {{"opened", opened},
                        {"closed", closed},
                        {"expired", expired},
                        {"failed", failed},
                        {"upgraded", upgraded},
                        {"active", activeNow},
                        {"intervals", roomCount},
                        {"totalLifetimeSeconds", totalLifetime},
                        {"totalIdleSeconds", totalIdle},
                        {"avgLifetimeSeconds", average(totalLifetime, roomCount)},
                        {"avgIdleSeconds", average(totalIdle, roomCount)},
                        {"byProfile", byProfile},
                        {"byImageTag", byImageTag}};
  summary["ui"]      = {{"distinct", static_cast<int>(uiUsers.size())},
                        {"sessions", uiSessions},
                        {"totalSessionSeconds", uiSeconds},
                        {"totalIdleSeconds", uiIdle},
                        {"avgSessionSeconds", average(uiSeconds, uiSessions)},
                        {"avgIdleSeconds", average(uiIdle, uiSessions)}};
  summary["versions"] = {{"client", clientVersions},
                         {"server", serverVersions},
                         {"imageTag", imageTags}};
  summary["tools"]    = toolCounts;
  summary["series"]   = series;
  summary["events"]   = {{"rooms", static_cast<int>(rooms.size())}, {"users", static_cast<int>(users.size())}};
  return summary;
}

json NRoomStats::Users(const std::string & range)
{
  long        from = 0, to = 0, bucket = 0;
  std::string name;
  const long  now = Now();
  if (!ParseRange(range, now, from, to, bucket, name)) {
    return json{{"enabled", Enabled()},
                {"error", "Unknown range '" + range + "' (use 24h, 7d, 30d or all)"}};
  }
  SweepPresence();
  std::lock_guard<std::mutex> lock(fMutex);
  if (!fEnabled) return json{{"enabled", false}, {"range", {{"name", name}}}};
  if (name == "all") {
    from = FirstDayOnDisk();
    if (from <= 0) from = to - 86400;
  }
  return UsersLocked(from, to);
}

json NRoomStats::UsersLocked(long from, long to)
{
  struct UserAgg {
    long                     firstSeen{0};
    long                     lastSeen{0};
    int                      roomSessions{0};
    long                     roomSeconds{0};
    int                      uiSessions{0};
    long                     uiSeconds{0};
    long                     uiIdle{0};
    std::set<std::string>    rooms;
    std::set<std::string>    clients;
    std::string              lastClientVersion;
  };
  std::map<std::string, UserAgg> users;
  // room|session -> (user, connect ts). The user is not derivable from the key (the key carries the
  // room and the ws session), and an open room session has to be credited to the user who opened it.
  std::map<std::string, std::pair<std::string, long>> openRoom;
  std::map<std::string, long>                         openUi;   // user|session -> online ts
  std::map<std::string, long>                         hiddenUi; // user|session -> hidden ts

  const auto rows = LoadRows("users", from, to);
  for (const auto & row : rows) {
    if (row.user.empty()) continue;
    UserAgg & agg = users[row.user];
    if (agg.firstSeen == 0 || row.ts < agg.firstSeen) agg.firstSeen = row.ts;
    if (row.ts > agg.lastSeen) agg.lastSeen = row.ts;
    if (!row.clientVersion.empty()) {
      agg.lastClientVersion = row.clientVersion;
      agg.clients.insert((row.client.empty() ? std::string("unknown") : row.client) + " " + row.clientVersion);
    }
    if (row.event == "connect") {
      const std::string key = row.room + '\x1f' + row.session;
      if (!openRoom.count(key)) openRoom[key] = {row.user, row.ts};
      if (!row.room.empty()) agg.rooms.insert(row.room);
    }
    else if (row.event == "disconnect") {
      const std::string key = row.room + '\x1f' + row.session;
      const auto        it  = openRoom.find(key);
      if (it != openRoom.end()) {
        agg.roomSeconds += std::max<long>(0, row.ts - std::max<long>(it->second.second, from));
        ++agg.roomSessions;
        openRoom.erase(it);
      }
    }
    else if (row.event == "ui_online") {
      const std::string key = row.user + '\x1f' + row.session;
      if (!openUi.count(key)) {
        openUi[key] = row.ts;
        ++agg.uiSessions;
      }
      hiddenUi.erase(key);
    }
    else if (row.event == "ui_hidden") {
      const std::string key = row.user + '\x1f' + row.session;
      if (!hiddenUi.count(key)) hiddenUi[key] = row.ts;
    }
    else if (row.event == "ui_visible") {
      const std::string key = row.user + '\x1f' + row.session;
      const auto        it  = hiddenUi.find(key);
      if (it != hiddenUi.end()) {
        agg.uiIdle += std::max<long>(0, row.ts - std::max<long>(it->second, from));
        hiddenUi.erase(it);
      }
    }
    else if (row.event == "ui_offline") {
      const std::string key = row.user + '\x1f' + row.session;
      const auto        hidden = hiddenUi.find(key);
      if (hidden != hiddenUi.end()) {
        agg.uiIdle += std::max<long>(0, row.ts - std::max<long>(hidden->second, from));
        hiddenUi.erase(hidden);
      }
      const auto open = openUi.find(key);
      if (open != openUi.end()) {
        agg.uiSeconds += std::max<long>(0, row.ts - std::max<long>(open->second, from));
        openUi.erase(open);
      }
    }
  }
  for (const auto & entry : openRoom) {
    const std::string & user = entry.second.first;
    auto                it   = users.find(user);
    if (it != users.end()) {
      it->second.roomSeconds += std::max<long>(0, to - std::max<long>(entry.second.second, from));
      ++it->second.roomSessions;
    }
  }
  for (const auto & entry : openUi) {
    const std::string user = entry.first.substr(0, entry.first.find('\x1f'));
    auto              it   = users.find(user);
    if (it != users.end()) it->second.uiSeconds += std::max<long>(0, to - std::max<long>(entry.second, from));
  }
  // A tab that is hidden at the end of the window is still idle: close its hidden stretch at `to`.
  for (const auto & entry : hiddenUi) {
    const std::string user = entry.first.substr(0, entry.first.find('\x1f'));
    auto              it   = users.find(user);
    if (it != users.end()) it->second.uiIdle += std::max<long>(0, to - std::max<long>(entry.second, from));
  }

  std::vector<std::pair<std::string, UserAgg>> ordered(users.begin(), users.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto & a, const auto & b) { return a.second.lastSeen > b.second.lastSeen; });

  json list = json::array();
  for (const auto & entry : ordered) {
    const UserAgg & agg = entry.second;
    json            rooms = json::array();
    for (const auto & room : agg.rooms) rooms.push_back(room);
    json clients = json::array();
    for (const auto & client : agg.clients) clients.push_back(client);
    list.push_back(json{{"user", entry.first},
                        {"firstSeen", agg.firstSeen},
                        {"lastSeen", agg.lastSeen},
                        {"roomSessions", agg.roomSessions},
                        {"roomSeconds", agg.roomSeconds},
                        {"uiSessions", agg.uiSessions},
                        {"uiSeconds", agg.uiSeconds},
                        {"uiIdleSeconds", agg.uiIdle},
                        {"clientVersion", agg.lastClientVersion},
                        {"rooms", rooms},
                        {"clients", clients}});
  }
  return json{{"enabled", true}, {"users", list}};
}

std::string NRoomStats::Export(const std::string & range, const std::string & family)
{
  if (family != "rooms" && family != "users") return {};
  long        from = 0, to = 0, bucket = 0;
  std::string name;
  const long  now = Now();
  if (!ParseRange(range, now, from, to, bucket, name)) return {};

  std::lock_guard<std::mutex> lock(fMutex);
  if (!fEnabled) return {};
  if (name == "all") {
    from = FirstDayOnDisk();
    if (from <= 0) from = to - 86400;
  }

  std::error_code                    ec;
  std::vector<std::filesystem::path> paths;
  const long                         firstDay = DayStart(from) - 86400;
  const long                         lastDay  = DayStart(to) + 86400;
  for (const auto & entry : std::filesystem::directory_iterator(fConfig.dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    const std::string file = entry.path().filename().string();
    if (file.rfind(family + "-", 0) != 0 || file.size() < family.size() + 15) continue;
    const long day = DateEpoch(file.substr(family.size() + 1, 10));
    if (day <= 0 || day < firstDay || day > lastDay) continue;
    paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());

  std::string output;
  for (const auto & path : paths) {
    std::ifstream in(path);
    std::string   line;
    if (!in) continue;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      if (line.rfind("ts,", 0) == 0) {
        if (!output.empty()) continue; // one header is enough
      }
      output += line;
      output += '\n';
    }
  }
  return output;
}

} // namespace Ndmspc
