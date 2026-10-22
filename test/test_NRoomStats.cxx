#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "ndmspc/http/NRoomStats.h"

using namespace Ndmspc;

namespace {

/// @brief A temporary directory removed when the test ends.
class TempDir {
  public:
  TempDir()
  {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fPath = std::filesystem::temp_directory_path() / ("ndmspc-stats-" + std::to_string(stamp));
    std::filesystem::create_directories(fPath);
  }
  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(fPath, ec);
  }

  std::string path() const { return fPath.string(); }
  std::filesystem::path file(const std::string & name) const { return fPath / name; }

  private:
  std::filesystem::path fPath;
};

/// @brief The files one day wrote, as text.
std::string ReadAll(const std::filesystem::path & path)
{
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// @brief A config writing into a fresh directory, with a controllable clock.
struct Fixture {
  TempDir  dir;
  long     now{1700000000}; // 2023-11-14T22:13:20Z
  NRoomStats stats;

  Fixture()
  {
    NRoomStatsConfig config;
    config.enabled = true;
    config.dir = dir.path();
    config.retentionDays = 90;
    config.summaryTtlSec = 0;
    config.publicSummary = true;
    config.presenceIntervalSec = 60;
    stats.Configure(config);
    stats.SetClock([this]() { return now; });
  }

  std::string usersCsv() const { return ReadAll(dir.file("users-2023-11-14.csv")); }
  std::string roomsCsv() const { return ReadAll(dir.file("rooms-2023-11-14.csv")); }
};

TEST(NRoomStatsTest, WritesHeadersAndEscapesFields)
{
  Fixture fixture;

  fixture.stats.RoomEvent("open", "room-1", "alice,admin", "small", "img:v1", "v1", "1.0.0",
                          "created;note=\"quoted\"");
  ASSERT_TRUE(std::filesystem::exists(fixture.dir.file("rooms-2023-11-14.csv")));

  const std::string csv = fixture.roomsCsv();
  EXPECT_NE(csv.find("ts,time,event,room,owner,profile,image,image_tag,server_version,detail"), std::string::npos);
  EXPECT_NE(csv.find("\"alice,admin\""), std::string::npos);
  EXPECT_NE(csv.find("\"created;note=\"\"quoted\"\"\""), std::string::npos);

  fixture.stats.UserEvent("connect", "room-1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "7", "");
  const std::string users = fixture.usersCsv();
  EXPECT_NE(users.find("ts,time,event,room,user,role,client,client_version,server_version,session,detail"),
            std::string::npos);
  EXPECT_NE(users.find(",alice,owner,ndmspc-ui,9.9.9,1.0.0,7,"), std::string::npos);
}

TEST(NRoomStatsTest, SummaryCountsUsersSessionsRoomsAndIdle)
{
  Fixture fixture;
  const long t0 = fixture.now;

  fixture.stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "created");

  fixture.now = t0 + 10;
  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "7", "");
  fixture.now = t0 + 70;
  fixture.stats.UserEvent("disconnect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "7",
                          "messages=3");

  fixture.now = t0 + 80;
  fixture.stats.UserEvent("connect", "r1", "bob", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "8", "");

  fixture.now = t0 + 110;
  fixture.stats.Presence("alice", "owner", "tab-1", "visible", "ndmspc-ui", "9.9.9");
  fixture.now = t0 + 170;
  fixture.stats.Presence("alice", "owner", "tab-1", "hidden", "ndmspc-ui", "9.9.9");
  fixture.now = t0 + 230;
  fixture.stats.Presence("alice", "owner", "tab-1", "visible", "ndmspc-ui", "9.9.9");
  fixture.now = t0 + 260;
  fixture.stats.Presence("alice", "owner", "tab-1", "offline", "ndmspc-ui", "9.9.9");

  fixture.now = t0 + 290;
  const json summary = fixture.stats.Summary("24h");

  ASSERT_TRUE(summary.value("enabled", false));
  EXPECT_EQ(summary["range"]["name"], "24h");
  EXPECT_EQ(summary["users"]["distinct"], 2);
  EXPECT_EQ(summary["users"]["last24h"], 2);
  EXPECT_EQ(summary["users"]["sessions"], 2);
  // alice 60s; bob 80 -> 290 (still connected).
  EXPECT_DOUBLE_EQ(summary["users"]["avgSessionSeconds"].get<double>(), 135.0);

  EXPECT_EQ(summary["rooms"]["opened"], 1);
  EXPECT_EQ(summary["rooms"]["active"], 1);
  EXPECT_EQ(summary["rooms"]["totalLifetimeSeconds"], 290);
  // alice occupied 10..70 and bob 80..290: 20s with nobody in the room.
  EXPECT_EQ(summary["rooms"]["totalIdleSeconds"], 20);
  EXPECT_DOUBLE_EQ(summary["rooms"]["avgIdleSeconds"].get<double>(), 20.0);

  EXPECT_EQ(summary["ui"]["sessions"], 1);
  EXPECT_EQ(summary["ui"]["totalIdleSeconds"], 60);
  EXPECT_EQ(summary["ui"]["totalSessionSeconds"], 150);
  // Three room-session rows and four presence rows all name the same client version.
  EXPECT_EQ(summary["versions"]["client"]["9.9.9"], 7);
  EXPECT_EQ(summary["versions"]["imageTag"]["v1"], 1);
  EXPECT_GE(summary["series"].size(), 24u);
  EXPECT_LE(summary["series"].size(), 25u);
}

TEST(NRoomStatsTest, UsersBreakdownAndExport)
{
  Fixture fixture;
  const long t0 = fixture.now;

  fixture.stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "created");
  fixture.now = t0 + 10;
  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "1", "");
  fixture.now = t0 + 70;
  fixture.stats.UserEvent("disconnect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "1", "");
  fixture.now = t0 + 80;
  fixture.stats.UserEvent("ui_seen", "", "carol", "owner", "ndmspc-ui", "9.9.9", "", "", "");

  const json users = fixture.stats.Users("24h");
  ASSERT_TRUE(users.value("enabled", false));
  ASSERT_EQ(users["users"].size(), 2u);

  bool foundAlice = false;
  for (const auto & user : users["users"]) {
    if (user["user"] == "alice") {
      foundAlice = true;
      EXPECT_EQ(user["roomSessions"], 1);
      EXPECT_EQ(user["roomSeconds"], 60);
      EXPECT_EQ(user["clientVersion"], "9.9.9");
    }
  }
  EXPECT_TRUE(foundAlice);

  const std::string csv = fixture.stats.Export("24h", "users");
  EXPECT_NE(csv.find("ts,time,event,room,user"), std::string::npos);
  EXPECT_EQ(csv.find("ts,time,event,room,user", 10), std::string::npos) << "the header must appear once";
  EXPECT_NE(csv.find(",alice,"), std::string::npos);
  EXPECT_NE(csv.find(",carol,"), std::string::npos);
  EXPECT_TRUE(fixture.stats.Export("24h", "bogus").empty());
}

TEST(NRoomStatsTest, SweepsStalePresenceAndClosesTheSession)
{
  Fixture fixture;
  fixture.now = 1700000000;
  EXPECT_TRUE(fixture.stats.Presence("alice", "owner", "tab-1", "visible", "ndmspc-ui", "9.9.9"));

  fixture.now += 60;
  EXPECT_TRUE(fixture.stats.Presence("alice", "owner", "tab-1", "hidden", "ndmspc-ui", "9.9.9"));

  fixture.now += 1000; // no report for far longer than 3x the interval
  fixture.stats.SweepPresence();

  const std::string csv = fixture.usersCsv();
  EXPECT_NE(csv.find("ui_online"), std::string::npos);
  EXPECT_NE(csv.find("ui_hidden"), std::string::npos);
  EXPECT_NE(csv.find("ui_offline"), std::string::npos);
  EXPECT_NE(csv.find("stale=1"), std::string::npos);

  // A report after the sweep opens a new session rather than resuming the old one.
  fixture.stats.Presence("alice", "owner", "tab-1", "visible", "ndmspc-ui", "9.9.9");
  EXPECT_EQ(fixture.stats.Users("24h")["users"].size(), 1u);
}

TEST(NRoomStatsTest, OpenSessionsAndHiddenIdleCountUpToNow)
{
  Fixture fixture;
  const long now = fixture.now;

  // A room session and a visible UI tab at the same instant.
  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "ws-1", "");
  fixture.stats.UserEvent("tool", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "ws-1",
                          "tool=ndmspc/ngnt/open");
  ASSERT_TRUE(fixture.stats.Presence("alice", "owner", "tab-1", "visible", "ndmspc-ui", "9.9.9"));

  fixture.now = now + 60;
  ASSERT_TRUE(fixture.stats.Presence("alice", "owner", "tab-1", "hidden", "ndmspc-ui", "9.9.9"));

  // No disconnect and no ui_offline: both sessions are still open at the end of the window.
  fixture.now = now + 90;
  const json users = fixture.stats.Users("24h");

  bool found = false;
  for (const auto & user : users["users"]) {
    if (user["user"] != "alice") continue;
    found = true;
    EXPECT_EQ(user["roomSessions"], 1);
    EXPECT_EQ(user["roomSeconds"], 90);
    EXPECT_EQ(user["uiSessions"], 1);
    EXPECT_EQ(user["uiSeconds"], 90);
    EXPECT_EQ(user["uiIdleSeconds"], 30);
  }
  EXPECT_TRUE(found);
  const json summary = fixture.stats.Summary("24h");
  EXPECT_EQ(summary["tools"]["ndmspc/ngnt/open"], 1);
  EXPECT_EQ(summary["rooms"]["active"], 1);
}

TEST(NRoomStatsTest, ActiveAndIdleRoomsInSeries)
{
  Fixture fixture;
  const long now = fixture.now;

  fixture.stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "created");
  fixture.stats.RoomEvent("open", "r2", "alice", "small", "img:v1", "v1", "1.0.0", "created");
  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "ws-1", "");

  fixture.now = now + 60;
  const json summary = fixture.stats.Summary("24h");
  ASSERT_FALSE(summary["series"].empty());
  const json & last = summary["series"].back();
  EXPECT_EQ(last["activeRooms"], 1);
  EXPECT_EQ(last["idleRooms"], 1);
}

TEST(NRoomStatsTest, ActiveRoomsAreOccupiedNotJustOpen)
{
  Fixture fixture;

  fixture.stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "created");
  fixture.stats.RoomEvent("open", "r2", "alice", "small", "img:v1", "v1", "1.0.0", "created");
  EXPECT_EQ(fixture.stats.Summary("24h")["rooms"]["active"], 0);

  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "ws-1", "");
  EXPECT_EQ(fixture.stats.Summary("24h")["rooms"]["active"], 1);

  fixture.stats.UserEvent("disconnect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "ws-1", "");
  EXPECT_EQ(fixture.stats.Summary("24h")["rooms"]["active"], 0);
}

TEST(NRoomStatsTest, PrunesFilesOlderThanRetention)
{
  Fixture fixture;

  const auto stale = fixture.dir.file("users-2020-01-01.csv");
  {
    std::ofstream out(stale);
    out << "ts,time,event,room,user,role,client,client_version,server_version,session,detail\n";
  }
  ASSERT_TRUE(std::filesystem::exists(stale));

  NRoomStatsConfig config;
  config.enabled = true;
  config.dir = fixture.dir.path();
  config.retentionDays = 1;
  fixture.stats.Configure(config);
  fixture.stats.SetClock([&fixture]() { return fixture.now; });
  fixture.stats.UserEvent("ui_seen", "", "alice", "owner", "ndmspc-ui", "9.9.9", "", "", "");

  EXPECT_FALSE(std::filesystem::exists(stale));
}

TEST(NRoomStatsTest, DisablesItselfWhenTheDirectoryCannotBeUsed)
{
  TempDir  dir;
  const auto file = dir.file("not-a-directory");
  {
    std::ofstream out(file);
    out << "x";
  }

  NRoomStatsConfig config;
  config.enabled = true;
  config.dir = file.string();

  NRoomStats stats(config);
  EXPECT_FALSE(stats.Enabled());
  stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "");
  EXPECT_FALSE(stats.Summary("24h").value("enabled", true));
}

} // namespace

TEST(NRoomStatsTest, AllCoversEverythingOnDisk)
{
  Fixture     fixture;
  const long  t0  = fixture.now;
  const long  day = t0 - (t0 % 86400);

  fixture.stats.RoomEvent("open", "r1", "alice", "small", "img:v1", "v1", "1.0.0", "created");

  fixture.now = t0 + 3 * 86400;
  fixture.stats.UserEvent("connect", "r1", "alice", "owner", "ndmspc-ui", "9.9.9", "1.0.0", "1", "");

  const json all = fixture.stats.Summary("all");
  ASSERT_TRUE(all.value("enabled", false));
  EXPECT_EQ(all["range"]["name"], "all");
  EXPECT_EQ(all["range"]["from"].get<long>(), day);
  EXPECT_EQ(all["range"]["to"].get<long>(), fixture.now);
  EXPECT_GE(all["series"].size(), 4u);
  EXPECT_EQ(all["users"]["distinct"], 1);

  EXPECT_NE(fixture.stats.Export("all", "rooms").find(",open,r1,"), std::string::npos);
  EXPECT_EQ(fixture.stats.Users("all")["users"].size(), 1u);
}
