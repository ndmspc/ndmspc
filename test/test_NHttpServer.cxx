#include <gtest/gtest.h>

#include "ndmspc/http/NHttpServer.h"

#include <cstdlib>
#include <string>

namespace {

using Ndmspc::NHttpServer;

/// @brief Sets an environment variable for one test and restores it afterwards.
class EnvGuard {
  public:
  EnvGuard(const char * name, const char * value) : fName(name)
  {
    if (const char * old = std::getenv(name); old != nullptr) {
      fHad = true;
      fOld = old;
    }
    if (value != nullptr) ::setenv(name, value, 1);
    else ::unsetenv(name);
  }
  ~EnvGuard()
  {
    if (fHad) ::setenv(fName.c_str(), fOld.c_str(), 1);
    else ::unsetenv(fName.c_str());
  }

  private:
  std::string fName;
  std::string fOld;
  bool        fHad{false};
};

} // namespace

TEST(NHttpServerRuntimeEnvTest, OnlyViteNamesAreHandedToThePage)
{
  EnvGuard setting("VITE_NDMSPC_SERVICES", "rooms,room,tools");
  // Everything else the process was started with is internal: it must not reach the page.
  EnvGuard internal("NDMSPC_TEST_INTERNAL", "do-not-leak");

  const json env = NHttpServer::RuntimeEnv();
  EXPECT_EQ(env["VITE_NDMSPC_SERVICES"].get<std::string>(), "rooms,room,tools");
  EXPECT_EQ(env.find("NDMSPC_TEST_INTERNAL"), env.end());
}

TEST(NHttpServerRuntimeEnvTest, AValueCannotCloseTheScriptElement)
{
  const std::string value = "</script><script>alert(1)</script>";
  const std::string text  = NHttpServer::JsonForHtml(json{{"VITE_X", value}});

  EXPECT_EQ(text.find("</script>"), std::string::npos);
  EXPECT_EQ(text.find("<script>"), std::string::npos);
  // ... and it is still the same JSON value.
  EXPECT_EQ(json::parse(text)["VITE_X"].get<std::string>(), value);
}

TEST(NHttpServerRuntimeEnvTest, TheEscapesCoverTheOtherMarkupAndLineTerminators)
{
  const std::string value = "a\u2028b\u2029c&<>";
  const std::string text  = NHttpServer::JsonForHtml(json{{"VITE_X", value}});

  EXPECT_EQ(text.find("\xE2\x80\xA8"), std::string::npos);
  EXPECT_EQ(text.find("&"), std::string::npos);
  EXPECT_EQ(text.find("<"), std::string::npos);
  EXPECT_EQ(json::parse(text)["VITE_X"].get<std::string>(), value);
}

TEST(NHttpServerRuntimeEnvTest, TheSettingsGoInAheadOfThePagesOwnScripts)
{
  const std::string html =
      "<!doctype html>\n<html><head>\n<script type=\"module\" src=\"/assets/app.js\"></script>\n</head><body></body></html>";
  const std::string page = NHttpServer::InjectRuntimeEnv(html, json{{"VITE_NDMSPC_SERVICES", "room"}});

  const auto injected = page.find("window.__NDMSPC_ENV__");
  ASSERT_NE(injected, std::string::npos);
  EXPECT_LT(injected, page.find("/assets/app.js")); // ahead of the app's own script
  EXPECT_NE(page.find("<!doctype html>"), std::string::npos);

  // Nothing to inject leaves the page alone, so the caller serves the built one.
  EXPECT_EQ(NHttpServer::InjectRuntimeEnv(html, json::object()), "");
}

TEST(NHttpServerRuntimeEnvTest, APageWithoutAHeadStillGetsTheSettingsFirst)
{
  const std::string page = NHttpServer::InjectRuntimeEnv("<p>hi</p>", json{{"VITE_X", "1"}});
  EXPECT_EQ(page.rfind("<script>window.__NDMSPC_ENV__", 0), 0u);
}
