#include "NRoomAccess.h"

#include <cstddef>
#include <exception>

namespace Ndmspc {

namespace {

/// @brief Compare two tokens without letting the time taken say how much of one matched.
///
/// A room token is the only thing standing between a stranger and the room, so a byte-by-byte
/// early exit would let it be guessed one character at a time.
bool Matches(const std::string & expected, const std::string & given)
{
  if (expected.empty() || expected.size() != given.size()) return false;

  unsigned char difference = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    difference |= static_cast<unsigned char>(expected[i]) ^ static_cast<unsigned char>(given[i]);
  }
  return difference == 0;
}

/// @brief The value a query string carries for one parameter, or "" when it carries none.
///
/// The first occurrence wins, so a repeated parameter cannot shadow the one the client meant.
std::string ValueFromQuery(const std::string & query, const char * key)
{
  if (query.empty()) return {};

  const std::string text = query.front() == '?' ? query.substr(1) : query;
  const std::string want = std::string(key) + "=";

  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end  = text.find('&', start);
    const std::string pair = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (pair.compare(0, want.size(), want) == 0) return pair.substr(want.size());
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

} // namespace

json NRoomAccess::Parse(const std::string & text)
{
  if (text.empty()) return json::object();
  try {
    const json access = json::parse(text);
    return access.is_object() ? access : json::object();
  }
  catch (const std::exception &) {
    return json::object();
  }
}

std::string NRoomAccess::LevelOf(const json & access, const std::string & token)
{
  if (!access.is_object() || token.empty()) return {};

  // Read-only is tested first: the two tokens are minted apart and so cannot both match, but if a
  // room were ever given the same value for both, the answer must be the weaker of the two.
  if (Matches(access.value(kReadOnly, std::string()), token)) return kReadOnly;
  if (Matches(access.value(kReadWrite, std::string()), token)) return kReadWrite;
  return {};
}

std::string NRoomAccess::TokenFromQuery(const std::string & query)
{
  return ValueFromQuery(query, kParam);
}

std::string NRoomAccess::LevelFromQuery(const std::string & query)
{
  return ValueFromQuery(query, kLevelParam);
}

std::string NRoomAccess::TokenFromCookie(const std::string & cookieHeader)
{
  if (cookieHeader.empty()) return {};

  const std::string want = std::string(kCookie) + "=";
  std::size_t       start = 0;
  while (start <= cookieHeader.size()) {
    std::size_t end = cookieHeader.find(';', start);
    if (end == std::string::npos) end = cookieHeader.size();

    std::size_t begin = start;
    while (begin < end && (cookieHeader[begin] == ' ' || cookieHeader[begin] == '\t')) ++begin;

    if (cookieHeader.compare(begin, want.size(), want) == 0) {
      return cookieHeader.substr(begin + want.size(), end - begin - want.size());
    }
    if (end == cookieHeader.size()) break;
    start = end + 1;
  }
  return {};
}

} // namespace Ndmspc
