#ifndef NDMSPC_NWS_CLIENT_INFO_H
#define NDMSPC_NWS_CLIENT_INFO_H

#include <string>   // For std::string
#include "Rtypes.h" // For ULong_t

namespace Ndmspc {

// Class to hold per-client data
class NWsClientInfo {
  private:
  ULong_t     fWsId;
  std::string fUsername;
  int         fMessageCount;

  public:
  // Default constructor
  NWsClientInfo();

  // Constructor with initial values
  NWsClientInfo(ULong_t id, const std::string & username);

  // Getters
  ULong_t             GetWsId() const;
  const std::string & GetUsername() const;
  int                 GetMessageCount() const;

  // Setter for username (if it can change)
  void SetUsername(const std::string & username);

  // Method to increment message count
  void IncrementMessageCount();
};

} // namespace Ndmspc

#endif // NDMSPC_NWS_CLIENT_INFO_H
