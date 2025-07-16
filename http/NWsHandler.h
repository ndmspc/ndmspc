#ifndef NdmspcNWsHandler_H
#define NdmspcNWsHandler_H
#include <map>    // For std::map
#include <string> // For std::string
#include <mutex>  // For std::mutex
#include <cstdio>
#include <THttpWSHandler.h>
#include <TString.h>
#include <THttpCallArg.h>  // For THttpCallArg from ROOT
#include "NWsClientInfo.h" // Include our client info class in the same namespace

// Forward declare SendCharStarWS in the global namespace if it's truly global
// If it's part of THttpServer, then MyWebSocketHandler needs a THttpServer* member
// and calls it via that. For now, we'll assume a global function needs to be exposed.
// extern void SendCharStarWS(ULong_t wsId, const char * msg);

class THttpCallArg;
class TTimer;
namespace Ndmspc {

///
/// \class NWsHandler
///
/// \brief NWsHandler object
///	\author Martin Vala <mvala@cern.ch>
///
class NWsHandler : public THttpWSHandler {
  public:
  NWsHandler(const char * name = nullptr, const char * title = nullptr);
  ~NWsHandler() override;

  TString GetDefaultPageContent() override { return "file:ws.htm"; }

  Bool_t ProcessWS(THttpCallArg * arg) override; // Method to broadcast a message to all connected clients
  void   BroadcastUnsafe(const std::string & message);
  void   Broadcast(const std::string & message);
  /// /// Allow processing of WS actions in arbitrary thread
  /// Bool_t AllowMTProcess() const override { return kTRUE; }
  ///
  /// /// Allows usage of special threads for send operations
  /// Bool_t AllowMTSend() const override { return kTRUE; }
  /// /// per timeout sends data portion to the client
  Bool_t HandleTimer(TTimer *) override;

  // ClassDefOverride(NWsHandler, 1);

  private:
  // Map to store information about each active client
  std::map<ULong_t, NWsClientInfo> fClients;
  std::mutex                       fMutex; // Protect fClients map for thread safety
  Int_t                            fServCnt{0};
};

} // namespace Ndmspc
#endif
