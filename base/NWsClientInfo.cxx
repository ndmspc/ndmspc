#include "NWsClientInfo.h" // Include header from within its namespace

namespace Ndmspc {

// Default constructor implementation
NWsClientInfo::NWsClientInfo() : fWsId(0), fUsername(""), fMessageCount(0) {}

// Constructor with initial values implementation
NWsClientInfo::NWsClientInfo(ULong_t id, const std::string & username)
    : fWsId(id), fUsername(username), fMessageCount(0)
{
}

// Getters implementation
ULong_t NWsClientInfo::GetWsId() const
{
  return fWsId;
}
const std::string & NWsClientInfo::GetUsername() const
{
  return fUsername;
}
int NWsClientInfo::GetMessageCount() const
{
  return fMessageCount;
}

// Setter for username implementation
void NWsClientInfo::SetUsername(const std::string & username)
{
  fUsername = username;
}

// Method to increment message count implementation
void NWsClientInfo::IncrementMessageCount()
{
  fMessageCount++;
}

} // namespace Ndmspc
