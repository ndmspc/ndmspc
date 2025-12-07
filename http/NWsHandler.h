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

class THttpCallArg;
class TTimer;
namespace Ndmspc {

/**
 * @class NWsHandler
 * @brief Handles WebSocket connections and messaging for NDMSPC.
 *
 * Inherits from THttpWSHandler to manage WebSocket events, broadcast messages,
 * and maintain client information in a thread-safe manner.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NWsHandler : public THttpWSHandler {
  public:
  /**
   * @brief Constructor.
   * @param name Optional handler name.
   * @param title Optional handler title.
   */
  NWsHandler(const char * name = nullptr, const char * title = nullptr);

  /**
   * @brief Destructor.
   */
  ~NWsHandler() override;

  /**
   * @brief Returns the default page content for the handler.
   * @return Default page content string.
   */
  TString GetDefaultPageContent() override { return "file:ws.htm"; }

  /**
   * @brief Processes a WebSocket event and broadcasts messages to clients.
   * @param arg Pointer to THttpCallArg containing event data.
   * @return True if processed successfully.
   */
  Bool_t ProcessWS(THttpCallArg * arg) override;

  /**
   * @brief Broadcasts a message to all connected clients (unsafe, not thread-safe).
   * @param message Message string to broadcast.
   */
  void BroadcastUnsafe(const std::string & message);

  /**
   * @brief Broadcasts a message to all connected clients (thread-safe).
   * @param message Message string to broadcast.
   */
  void Broadcast(const std::string & message);

  /**
   * @brief Handles timer events for the handler.
   * @param timer Pointer to TTimer object.
   * @return True if handled successfully.
   */
  Bool_t HandleTimer(TTimer * timer) override;

  private:
  std::map<ULong_t, NWsClientInfo> fClients;    ///< Map of active clients by ID
  std::mutex                       fMutex;      ///< Mutex for thread-safe client map access
  Int_t                            fServCnt{0}; ///< Service counter

  /// \cond CLASSIMP
  ClassDefOverride(NWsHandler, 1);
  /// \endcond;
};

} // namespace Ndmspc
#endif
