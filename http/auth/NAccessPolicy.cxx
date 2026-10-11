#include "NAccessPolicy.h"

#include <cstdlib>

namespace Ndmspc {
namespace {

/// @brief The value of an environment variable, or "" when it is unset.
std::string Env(const char * name)
{
  const char * value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
}

/// @brief The value with surrounding whitespace removed.
std::string Trim(const std::string & value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

/// @brief The comma-separated list split into its non-empty, trimmed entries.
std::vector<std::string> SplitList(const std::string & list)
{
  std::vector<std::string> entries;
  std::size_t              start = 0;
  while (start <= list.size()) {
    const std::size_t comma = list.find(',', start);
    const std::string item =
        Trim(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    if (!item.empty()) entries.push_back(item);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return entries;
}

} // namespace

const char * NAccessPolicy::DefaultMessage()
{
  return "Request access via email (martin.vala@cern.ch)";
}

NAccessPolicy::NAccessPolicy(std::vector<std::string> allowed, std::string message)
    : fAllowed(std::move(allowed)), fMessage(message.empty() ? DefaultMessage() : std::move(message))
{
}

NAccessPolicy NAccessPolicy::FromEnvironment()
{
  return NAccessPolicy(SplitList(Env("NDMSPC_ALLOWED_USERS")), Env("NDMSPC_ACCESS_DENIED_MESSAGE"));
}

bool NAccessPolicy::Allows(const NRequestIdentity & identity) const
{
  // No list: the deployment lets everyone who can authenticate in.
  if (fAllowed.empty()) return true;
  for (const auto & entry : fAllowed) {
    // An entry may be any of the forms a user is known by - the same matcher admin lists already use
    // (see NDMSPC_ROOM_ADMINS), so `alice`, `alice@example.org` and a subject all work.
    if (identity.Matches(entry)) return true;
  }
  return false;
}

} // namespace Ndmspc
