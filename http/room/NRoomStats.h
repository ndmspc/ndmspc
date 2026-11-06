#ifndef Ndmspc_NRoomStats_H
#define Ndmspc_NRoomStats_H

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ndmspc/core/NLogger.h"

namespace Ndmspc {

/**
 * @brief How the room router records usage, from the environment.
 *
 *   NDMSPC_STATS                     enable recording                     (default: true)
 *   NDMSPC_STATS_DIR                 where the CSV files live             (default: .ndmspc/stats under the process working directory)
 *   NDMSPC_STATS_RETENTION           how long files are kept              (default: 90d)
 *   NDMSPC_STATS_SUMMARY_TTL         how long a computed summary is cached(default: 30s)
 *   NDMSPC_STATS_PUBLIC              serve the aggregate summary anonymously (default: false)
 *   NDMSPC_STATS_PRESENCE_INTERVAL   how often a UI tab reports itself     (default: 60s; stale after 3x)
 *
 * The files are append-only CSV, one per day per family (`rooms-YYYY-MM-DD.csv`,
 * `users-YYYY-MM-DD.csv`), so they can be read with awk or a spreadsheet. The router is
 * expected to be the only writer (its Knative Service scales 1..1 in the deployment).
 */
struct NRoomStatsConfig {
  bool        enabled{true};             ///< Whether anything is recorded
  std::string dir;                       ///< Directory holding the CSV files
  long        retentionDays{90};         ///< Files older than this are pruned
  long        summaryTtlSec{30};         ///< How long a computed summary is reused
  bool        publicSummary{false};      ///< Whether the aggregate summary may be read anonymously
  long        presenceIntervalSec{60};   ///< Expected interval between UI presence reports

  /// @brief Reads every knob above from the environment.
  static NRoomStatsConfig FromEnv();
};

/**
 * @brief The room router's usage recorder: events to CSV, aggregates for the stats endpoints.
 *
 * Events are written as they happen and never fail a room operation: if the directory cannot be
 * created or written the recorder disables itself, logs once, and every method becomes a no-op.
 *
 * Two families are recorded:
 *  - rooms: open, upgrade, close, expire (idle sweep), fail.
 *  - users: connect/disconnect in a room, ui_seen (a UI user talked to the router), and the UI
 *    presence transitions ui_online/ui_hidden/ui_visible/ui_offline.
 *
 * What the summary means (also documented in http/README.md):
 *  - users in a window: distinct users seen in a room or in the UI in that window;
 *  - average time spent: the length of a user's room session (connect -> disconnect);
 *  - idle in a room: time a room existed with no user connected;
 *  - idle in the UI: time a UI tab was hidden inside a session.
 */
class NRoomStats {
  public:
  /// @brief The clock used for every timestamp (injectable for tests).
  using Clock = std::function<long()>;

  /// @brief A disabled recorder; call Configure() to use it.
  NRoomStats() = default;
  /// @brief A recorder configured explicitly (used by tests).
  explicit NRoomStats(NRoomStatsConfig config);

  /// @brief Applies a configuration, creating the directory when recording is enabled.
  void Configure(const NRoomStatsConfig & config);

  /// @brief Replaces the clock (tests only).
  void SetClock(Clock clock);

  /// @brief Whether recording is on and the directory is writable.
  bool Enabled() const;
  /// @brief Whether the aggregate summary may be served without authentication.
  bool Public() const;
  /// @brief The directory the files are written to ("" when recording is off).
  std::string Dir() const;

  /// @brief Records one room lifecycle event.
  void RoomEvent(const std::string & event, const std::string & room, const std::string & owner,
                 const std::string & profile, const std::string & image, const std::string & imageTag,
                 const std::string & serverVersion, const std::string & detail = "");

  /// @brief Records one user event (room session, UI seen, UI presence transition).
  void UserEvent(const std::string & event, const std::string & room, const std::string & user,
                 const std::string & role, const std::string & client, const std::string & clientVersion,
                 const std::string & serverVersion, const std::string & session, const std::string & detail = "");

  /**
   * @brief Records a batch a room reports through ndmspc/room/state.
   *
   * Each entry is `{type|event, user, role, client, client_version, server_version, session, detail}`.
   * Entries without a recognized event are ignored.
   * @return How many entries were recorded.
   */
  int RecordUsage(const json & usage);

  /**
   * @brief Records a UI tab's presence.
   *
   * `state` is visible, hidden or offline. The first report opens a session (ui_online); every
   * change writes a transition row carrying the previous state and its duration in `detail`.
   * @return True when a row was written.
   */
  bool Presence(const std::string & user, const std::string & role, const std::string & instance,
                const std::string & state, const std::string & client, const std::string & clientVersion);

  /// @brief Records that a UI user used the router, at most once per 15 minutes per user.
  void SeenUi(const std::string & user, const std::string & role, const std::string & client,
              const std::string & clientVersion);

  /// @brief Closes presence sessions that stopped reporting (3x the presence interval).
  void SweepPresence();

  /// @brief The aggregate summary for 24h, 7d or 30d.
  json Summary(const std::string & range);

  /// @brief The per-user breakdown for a window (admin endpoint).
  json Users(const std::string & range);

  /// @brief The raw rows for a window as CSV (`family` is rooms or users).
  std::string Export(const std::string & range, const std::string & family);

  private:
  /// @brief One parsed CSV row (both families share this shape).
  struct Row {
    long        ts{0};
    std::string event;
    std::string room;
    std::string owner;
    std::string user;
    std::string role;
    std::string profile;
    std::string image;
    std::string imageTag;
    std::string client;
    std::string clientVersion;
    std::string serverVersion;
    std::string session;
    std::string detail;
  };

  /// @brief A tab the router has seen report itself.
  struct PresenceEntry {
    std::string user;
    std::string role;
    std::string client;
    std::string clientVersion;
    std::string state; ///< visible or hidden
    long        since{0};
    long        lastSeen{0};
  };

  /// @brief One time interval, used for room lifetimes and occupancy.
  struct Interval {
    long from{0};
    long to{0};
  };

  long Now() const;
  void ConfigureLocked(const NRoomStatsConfig & config);
  bool AppendLocked(const std::string & family, const std::vector<std::string> & fields, long ts);
  void PruneLocked(long now);
  static std::string DefaultDir();
  static std::string Header(const std::string & family);
  static std::string DayKey(long ts);
  static std::string IsoTime(long ts);
  static std::string CsvField(const std::string & value);
  static std::vector<std::string> SplitCsv(const std::string & line);
  static bool  ParseRange(const std::string & range, long now, long & from, long & to, long & bucket,
                          std::string & name);
  /// @brief The day of the oldest CSV file on disk (0 when there is none); the start of "all".
  long FirstDayOnDisk() const;
  static long  ParseDetail(const std::string & detail, const std::string & key);
  static const std::string & Get(const std::map<std::string, std::string> & fields, const std::string & name);
  std::vector<Row> LoadRows(const std::string & family, long from, long to) const;
  json             SummaryLocked(long from, long to, long bucket, const std::string & name);
  json             UsersLocked(long from, long to);

  NRoomStatsConfig  fConfig;                    ///< The configuration in force
  bool              fEnabled{false};            ///< Whether the directory was usable
  bool              fWarned{false};             ///< Whether the failure was logged already
  long              fLastPruneDay{0};           ///< Day key of the last retention pass
  Clock             fClock;                     ///< Clock (empty means system_clock)
  mutable std::mutex fMutex;                    ///< Guards everything below and the files
  std::map<std::string, PresenceEntry> fPresence;   ///< instance -> its current state
  std::map<std::string, long>          fSeen;       ///< user -> last ui_seen timestamp
  std::map<std::string, std::pair<long, json>> fSummaryCache; ///< range -> (expiry, summary)
};

} // namespace Ndmspc

#endif // Ndmspc_NRoomStats_H
