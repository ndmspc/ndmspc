#include "NWsHandler.h"
#include <NLogger.h>
#include <iostream>
#include "THttpCallArg.h"
#include "TDatime.h"
#include "TTimer.h"
// ClassImp(Ndmspc::NWsHandler);
//
namespace Ndmspc {
NWsHandler::NWsHandler(const char * name, const char * title) : THttpWSHandler(name, title) {}
NWsHandler::~NWsHandler() {}

Bool_t NWsHandler::ProcessWS(THttpCallArg * arg)
{

  if (!arg || (arg->GetWSId() == 0)) return kTRUE;

  std::lock_guard<std::mutex> lock(fMutex);

  if (arg->IsMethod("WS_CONNECT")) {
    NLogger::Debug("WS_CONNECT received for path: /%s", arg->GetPathName());
    return true;
  }

  if (arg->IsMethod("WS_READY")) {
    ULong_t currentWsId = arg->GetWSId();
    NLogger::Debug("WS_READY received. Connection established with ID: %lld", currentWsId);

    std::string username  = "User_" + std::to_string(currentWsId);
    fClients[currentWsId] = NWsClientInfo(currentWsId, username); // Use the constructor

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
    NLogger::Debug("WS_CLOSE received for ID: %lld", closedWsId);

    std::string username = "Unknown";
    auto        it       = fClients.find(closedWsId);
    if (it != fClients.end()) {
      username = it->second.GetUsername();
      fClients.erase(it);
    }

    NLogger::Debug("Client with ID %lld and username '%s' has disconnected.", closedWsId, username.c_str());

    BroadcastUnsafe(username + " has left the chat.");

    return kTRUE;
  }

  if (arg->IsMethod("WS_DATA")) {
    ULong_t senderWsId = arg->GetWSId();
    NLogger::Debug("WS_DATA received from ID: %lld", senderWsId);
    std::string receivedStr((const char *)arg->GetPostData(), arg->GetPostDataLength());

    NLogger::Debug("WS_DATA from ID %lld: %s", senderWsId, receivedStr.c_str());

    std::string senderUsername = "Unknown";
    auto        it             = fClients.find(senderWsId);
    if (it != fClients.end()) {
      it->second.IncrementMessageCount();
      senderUsername = it->second.GetUsername();
    }

    std::string replyMsg = "Server received from " + senderUsername + ": " + receivedStr;
    // std::string broadcastMsg = senderUsername + ": " + receivedStr;
    std::string broadcastMsg = receivedStr;

    // Call the global SendCharStarWS function
    SendCharStarWS(senderWsId, replyMsg.c_str());

    for (const auto & pair : fClients) {
      if (pair.first != senderWsId) {
        SendCharStarWS(pair.first, broadcastMsg.c_str());
      }
    }

    return kTRUE;
  }

  NLogger::Error("Unknown WS method received: {}", arg->GetMethod());
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
  // NLogger::Debug("Broadcasting: %s", message.c_str());
  for (const auto & pair : fClients) {
    ULong_t wsId = pair.first;
    // Call the global SendCharStarWS function
    SendCharStarWS(wsId, message.c_str());
  }
}

// Bool_t NWsHandler::ProcessWS(THttpCallArg * arg)
// {
//   NLogger::Debug("ProcessWS: WSId: %d", arg->GetWSId());
//
//   if (!arg || (arg->GetWSId() == 0)) return kTRUE;
//   //
//   NLogger::Debug("Method %s", arg->GetMethod());
//
//   if (arg->IsMethod("WS_CONNECT")) {
//     // accept only if connection not established
//     return fWSId == 0;
//   }
//
//   if (arg->IsMethod("WS_READY")) {
//     fWSId = arg->GetWSId();
//     printf("Client connected %d\n", fWSId);
//     return kTRUE;
//   }
//   //
//   // if (arg->IsMethod("WS_CLOSE")) {
//   //   fWSId = 0;
//   //   printf("Client disconnected\n");
//   //   return kTRUE;
//   // }
//   //
//   // if (arg->IsMethod("WS_DATA")) {
//   //   TString str;
//   //   str.Append((const char *)arg->GetPostData(), arg->GetPostDataLength());
//   //   printf("Client msg: %s\n", str.Data());
//   //   TDatime now;
//   //   SendCharStarWS(arg->GetWSId(), TString::Format("Server replies:%s server counter:%d", now.AsString(),
//   //   fServCnt++)); return kTRUE;
//   // }
//
//   return kFALSE;
// }
//
/// per timeout sends data portion to the client
Bool_t NWsHandler::HandleTimer(TTimer *)
{
  // TDatime now;
  // if (fWSId)
  //   SendCharStarWS(fWSId, TString::Format("Server sends data:%s server counter:%d", now.AsString(), fServCnt++));
  return kTRUE;
}

} // namespace Ndmspc
