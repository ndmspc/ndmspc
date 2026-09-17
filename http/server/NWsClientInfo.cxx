#include "NWsClientInfo.h" // Include header from within its namespace

namespace Ndmspc {

// Default constructor implementation
NWsClientInfo::NWsClientInfo()
  : fWsId(0), fUsername(""), fMessageCount(0), fConnectedAt(std::chrono::system_clock::now()),
    fTokenExpiresAt(std::chrono::system_clock::time_point::max())
{
}

// Constructor with initial values implementation
NWsClientInfo::NWsClientInfo(ULong_t id, const std::string & username)
  : fWsId(id), fUsername(username), fMessageCount(0), fConnectedAt(std::chrono::system_clock::now()),
    fTokenExpiresAt(std::chrono::system_clock::time_point::max())
{
}

NWsClientInfo::NWsClientInfo(ULong_t id, std::string subject, std::string username,
                             std::chrono::system_clock::time_point tokenExpiresAt)
  : fWsId(id), fSubject(std::move(subject)), fUsername(std::move(username)), fMessageCount(0),
    fConnectedAt(std::chrono::system_clock::now()), fTokenExpiresAt(tokenExpiresAt)
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
const std::string & NWsClientInfo::GetSubject() const
{
  return fSubject;
}
std::chrono::system_clock::time_point NWsClientInfo::GetTokenExpiresAt() const
{
  return fTokenExpiresAt;
}
bool NWsClientInfo::IsTokenValidAt(std::chrono::system_clock::time_point now) const
{
  return now < fTokenExpiresAt;
}
void NWsClientInfo::ReplaceIdentity(std::string subject, std::string username,
                                    std::chrono::system_clock::time_point tokenExpiresAt)
{
  fSubject = std::move(subject);
  fUsername = std::move(username);
  fTokenExpiresAt = tokenExpiresAt;
}
int NWsClientInfo::GetMessageCount() const
{
  return fMessageCount;
}

std::chrono::system_clock::time_point NWsClientInfo::GetConnectedAt() const
{
  return fConnectedAt;
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
