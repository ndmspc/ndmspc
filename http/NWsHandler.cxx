#include "NWsHandler.h"
#include <THttpCallArg.h>
#include <TTimer.h>
#include "NLogger.h"
#include "NUtils.h"
// ClassImp(Ndmspc::NWsHandler);
//
namespace Ndmspc {
NWsHandler::NWsHandler(const char * name, const char * title) : THttpWSHandler(name, title, kFALSE) {}
NWsHandler::~NWsHandler() {}

Bool_t NWsHandler::ProcessWS(THttpCallArg * arg)
{

  if (!arg || (arg->GetWSId() == 0)) return kTRUE;

  std::lock_guard<std::mutex> lock(fMutex);

  if (arg->IsMethod("WS_CONNECT")) {
    NLogTrace("WS_CONNECT received for path: /%s", arg->GetPathName());
    return true;
  }

  if (arg->IsMethod("WS_READY")) {
    ULong_t currentWsId = arg->GetWSId();
    NLogTrace("WS_READY received. Connection established with ID: %lld", currentWsId);

    std::string username  = "User_" + std::to_string(currentWsId);
    fClients[currentWsId] = NWsClientInfo(currentWsId, username); // Use the constructor

    NLogDebug("New client connected with ID %lld and username '%s'.", currentWsId, username.c_str());
    // Call the global SendCharStarWS function
    SendCharStarWS(currentWsId, ("Welcome, " + username + "!").c_str());

    for (const auto & pair : fClients) {
      if (pair.first != currentWsId) {
        SendCharStarWS(pair.first, (username + " has joined the chat!").c_str());
      }
    }

    return kTRUE;
  }

  if (arg->IsMethod("WS_CLOSE")) {
    ULong_t closedWsId = arg->GetWSId();
    NLogTrace("WS_CLOSE received for ID: %lld", closedWsId);

    std::string username = "Unknown";
    auto        it       = fClients.find(closedWsId);
    if (it != fClients.end()) {
      username = it->second.GetUsername();
      fClients.erase(it);
    }

    NLogDebug("Client with ID %lld and username '%s' has disconnected.", closedWsId, username.c_str());

    BroadcastUnsafe(username + " has left the chat.");

    return kTRUE;
  }

  if (arg->IsMethod("WS_DATA")) {
    ULong_t     senderWsId = arg->GetWSId();
    std::string receivedStr((const char *)arg->GetPostData(), arg->GetPostDataLength());
    NLogTrace("WS_DATA from ID %lld: %s", senderWsId, receivedStr.c_str());

    std::string senderUsername = "Unknown";
    auto        it             = fClients.find(senderWsId);
    if (it != fClients.end()) {
      it->second.IncrementMessageCount();
      senderUsername = it->second.GetUsername();
    }

    std::string replyMsg = "Server received from " + senderUsername + ": " + receivedStr;
    // return kTRUE;
    // std::string broadcastMsg = senderUsername + ": " + receivedStr;
    std::string broadcastMsg = receivedStr;

    // return kTRUE;
    // Call the global SendCharStarWS function
    // SendCharStarWS(senderWsId, replyMsg.c_str());

    // Cache only the most recent message for each client
    static std::unordered_map<ULong_t, std::string> recentMessages;
    recentMessages[senderWsId] = broadcastMsg;

    for (const auto & pair : fClients) {
      if (pair.first != senderWsId) {
        auto itMsg = recentMessages.find(senderWsId);
        if (itMsg != recentMessages.end()) {
          SendCharStarWS(pair.first, itMsg->second.c_str());
        }
      }
    }

    return kTRUE;
  }

  NLogError("Unknown WS method received: {}", arg->GetMethod());
  return kFALSE;
}

void NWsHandler::Broadcast(const std::string & message)
{
  // This method is protected by a mutex to ensure thread safety
  std::lock_guard<std::mutex> lock(fMutex);
  BroadcastUnsafe(message);
}

void NWsHandler::BroadcastUnsafe(const std::string & message)
{
  NLogDebug("Broadcasting to %d clients : %s", fClients.size(), message.c_str());
  for (const auto & pair : fClients) {
    ULong_t wsId = pair.first;
    // Call the global SendCharStarWS function
    SendCharStarWS(wsId, message.c_str());
  }
}

Bool_t NWsHandler::HandleTimer(TTimer *)
{
  ///
  /// Handle timer event for heartbeat
  ///

  json data;
  data["event"]            = "heartbeat";
  data["payload"]["count"] = ++fServCnt;
  BroadcastUnsafe(data.dump());

  return kTRUE;
}

} // namespace Ndmspc
