#include "NWsHandler.h"
#include <THttpCallArg.h>
#include <TTimer.h>
#include <chrono>
#include <unordered_map>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NGnHttpServer.h"
// ClassImp(Ndmspc::NWsHandler);
//
namespace Ndmspc {
NWsHandler::NWsHandler(const char * name, const char * title)
    : THttpWSHandler(name, title, kFALSE),
      fServerStartedAt(std::chrono::system_clock::now())
{
}
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
    json welcomeData;
    welcomeData["event"]               = "welcome";
    welcomeData["payload"]["username"] = username;
    welcomeData["payload"]["wsId"]     = currentWsId;
    SendCharStarWS(currentWsId, welcomeData.dump().c_str());

    for (const auto & pair : fClients) {
      if (pair.first != currentWsId) {
        SendCharStarWS(pair.first, (username + " has joined the chat!").c_str());
      }
    }

    json clientsData;
    clientsData["event"]            = "clients";
    clientsData["payload"]["count"] = static_cast<int>(fClients.size());
    for (const auto & pair : fClients) {
      json userData;
      userData["wsId"]     = pair.first;
      userData["username"] = pair.second.GetUsername();
      const auto connectedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          pair.second.GetConnectedAt().time_since_epoch()).count();
      userData["connectedAt"] = connectedAtMs;
      clientsData["payload"]["users"].push_back(userData);
    }
    BroadcastUnsafe(clientsData.dump());

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

    json disconnectData;
    disconnectData["event"]   = "goodbye";
    disconnectData["payload"] = "Goodbye, " + username + "!";
    BroadcastUnsafe(disconnectData.dump());

    json clientsData;
    clientsData["event"]            = "clients";
    clientsData["payload"]["count"] = static_cast<int>(fClients.size());
    for (const auto & pair : fClients) {
      json userData;
      userData["wsId"]     = pair.first;
      userData["username"] = pair.second.GetUsername();
      const auto connectedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          pair.second.GetConnectedAt().time_since_epoch()).count();
      userData["connectedAt"] = connectedAtMs;
      clientsData["payload"]["users"].push_back(userData);
    }
    BroadcastUnsafe(clientsData.dump());

    return kTRUE;
  }

  if (arg->IsMethod("WS_DATA")) {
    ULong_t     senderWsId = arg->GetWSId();
    std::string receivedStr((const char *)arg->GetPostData(), arg->GetPostDataLength());
    NLogTrace("WS_DATA from ID %lld: %s", senderWsId, receivedStr.c_str());

    // Route JSON messages carrying a "path" through the same HTTP API dispatch used for
    // real HTTP requests; anything else falls back to the legacy chat-relay demo below.
    // Note: an empty "path" is valid and maps to the API root (welcome/state) endpoint.
    json apiRequest;
    bool isApiRequest = false;
    try {
      apiRequest   = json::parse(receivedStr);
      isApiRequest = apiRequest.is_object() && apiRequest.contains("path") && apiRequest["path"].is_string();
    }
    catch (const json::parse_error &) {
      isApiRequest = false;
    }

    if (isApiRequest) {
      json requestId = apiRequest.contains("requestId") ? apiRequest["requestId"] : json(nullptr);
      std::string method  = apiRequest.value("method", "POST");
      std::string path    = apiRequest["path"].get<std::string>();
      std::string query   = apiRequest.value("query", "");
      json        payload = apiRequest.contains("payload") ? apiRequest["payload"] : json::object();

      if (!Ndmspc::gNGnHttpServer) {
        NLogError("Cannot route WS_DATA to HTTP API: gNGnHttpServer is not set");
        return kTRUE;
      }

      auto httpArg = std::make_shared<THttpCallArg>();
      httpArg->SetMethod(method.c_str());
      // Mirror NGnHttpServer::ProcessRequest's fullpath = "/" + path + "/" + filename + "/"
      // construction: keep path fixed to "api" and put the actual route in filename, same
      // shape as the already-tested empty-path case (both parts get collapsed correctly).
      httpArg->SetPathName("api");
      httpArg->SetFileName(path.c_str());
      if (!query.empty()) httpArg->SetQuery(query.c_str());
      httpArg->SetWSId(senderWsId);
      httpArg->SetPostData(payload.dump());

      Ndmspc::gNGnHttpServer->ProcessRequest(httpArg);

      std::string content((const char *)httpArg->GetContent(), httpArg->GetContentLength());
      json        reply;
      reply["event"]       = "ngnt_reply";
      reply["requestId"]   = requestId;
      reply["contentType"] = httpArg->GetContentType();
      try {
        reply["payload"] = json::parse(content);
      }
      catch (const json::parse_error &) {
        reply["payload"] = content;
      }

      NLogTrace("Replying to WS_DATA API request from ID %lld for path: %s", senderWsId, path.c_str());
      SendCharStarWS(senderWsId, reply.dump().c_str());
      return kTRUE;
    }

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

size_t NWsHandler::GetClientCount() const
{
  return fClients.size();
}

void NWsHandler::Broadcast(const std::string & message)
{
  // This method is protected by a mutex to ensure thread safety
  std::lock_guard<std::mutex> lock(fMutex);
  BroadcastUnsafe(message);
}

void NWsHandler::BroadcastUnsafe(const std::string & message)
{
  NLogTrace("Broadcasting to %d clients : %s", fClients.size(), message.c_str());
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

  std::lock_guard<std::mutex> lock(fMutex);

  json data;
  data["event"]              = "heartbeat";
  data["payload"]["count"]   = ++fServCnt;
  data["payload"]["clients"] = static_cast<int>(fClients.size());
  data["payload"]["serverStartedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
      fServerStartedAt.time_since_epoch()).count();
  // add system and TFile IO statistics
  try {
    data["payload"]["system"] = NUtils::GetSystemStats();
    json currFile = NUtils::GetTFileIOStats();
    data["payload"]["file"]["counters"] = currFile;

    // compute TFile IO speeds if we have previous snapshot
    if (fHavePrevFile) {
      auto nowFile = std::chrono::steady_clock::now();
      double dtFile = std::chrono::duration_cast<std::chrono::milliseconds>(nowFile - fPrevFileTs).count() / 1000.0;
      if (dtFile <= 0) dtFile = 1.0;

      unsigned long long prevRead = static_cast<unsigned long long>(fPrevFileStats.value("totalRead", 0LL));
      unsigned long long prevWritten = static_cast<unsigned long long>(fPrevFileStats.value("totalWritten", 0LL));
      unsigned long long currRead = static_cast<unsigned long long>(currFile.value("totalRead", 0LL));
      unsigned long long currWritten = static_cast<unsigned long long>(currFile.value("totalWritten", 0LL));

      double read_bps = double(currRead - prevRead) / dtFile;
      double write_bps = double(currWritten - prevWritten) / dtFile;

      data["payload"]["file"]["speed"]["total_read_bps"] = read_bps;
      data["payload"]["file"]["speed"]["total_write_bps"] = write_bps;

      // per-file speeds
      std::unordered_map<std::string, std::pair<long long, long long>> prevFileMap;
      if (fPrevFileStats.contains("files")) {
        for (const auto & fj : fPrevFileStats["files"]) {
          std::string name = fj.value("name", std::string());
          long long br = static_cast<long long>(fj.value("bytesRead", 0LL));
          long long bw = static_cast<long long>(fj.value("bytesWritten", 0LL));
          prevFileMap[name] = {br, bw};
        }
      }
      for (const auto & fj : currFile.value("files", json::array())) {
        std::string name = fj.value("name", std::string());
        long long br = static_cast<long long>(fj.value("bytesRead", 0LL));
        long long bw = static_cast<long long>(fj.value("bytesWritten", 0LL));
        long long pbr = 0, pbw = 0;
        auto itf = prevFileMap.find(name);
        if (itf != prevFileMap.end()) { pbr = itf->second.first; pbw = itf->second.second; }
        double br_bps = double(br - pbr) / dtFile;
        double bw_bps = double(bw - pbw) / dtFile;
        data["payload"]["file"]["speed"]["files"].push_back({{"name", name}, {"read_bps", br_bps}, {"write_bps", bw_bps}});
      }
    }

    // store file snapshot
    fPrevFileStats = currFile;
    fPrevFileTs = std::chrono::steady_clock::now();
    fHavePrevFile = true;

    // network stats and speeds
    auto now = std::chrono::steady_clock::now();
    json currNet = NUtils::GetNetDevStats();

    // copy raw counters
    data["payload"]["net"]["counters"] = currNet;

    if (fHavePrevNet) {
      double dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - fPrevNetTs).count() / 1000.0;
      if (dt <= 0) dt = 1.0; // avoid div0

      unsigned long long prevTotalRx = static_cast<unsigned long long>(fPrevNetStats.value("total_rx", 0ULL));
      unsigned long long prevTotalTx = static_cast<unsigned long long>(fPrevNetStats.value("total_tx", 0ULL));
      unsigned long long currTotalRx = static_cast<unsigned long long>(currNet.value("total_rx", 0ULL));
      unsigned long long currTotalTx = static_cast<unsigned long long>(currNet.value("total_tx", 0ULL));

      double total_rx_bps = double(currTotalRx - prevTotalRx) / dt;
      double total_tx_bps = double(currTotalTx - prevTotalTx) / dt;

      data["payload"]["net"]["speed"]["total_rx_bps"] = total_rx_bps;
      data["payload"]["net"]["speed"]["total_tx_bps"] = total_tx_bps;

      // per-interface speeds
      std::unordered_map<std::string, std::pair<unsigned long long, unsigned long long>> prevMap;
      if (fPrevNetStats.contains("interfaces")) {
        for (const auto & ifj : fPrevNetStats["interfaces"]) {
          std::string name = ifj.value("name", std::string());
          unsigned long long rx = static_cast<unsigned long long>(ifj.value("rx", 0ULL));
          unsigned long long tx = static_cast<unsigned long long>(ifj.value("tx", 0ULL));
          prevMap[name] = {rx, tx};
        }
      }

      for (const auto & ifj : currNet.value("interfaces", json::array())) {
        std::string name = ifj.value("name", std::string());
        unsigned long long rx = static_cast<unsigned long long>(ifj.value("rx", 0ULL));
        unsigned long long tx = static_cast<unsigned long long>(ifj.value("tx", 0ULL));
        unsigned long long prevRx = 0, prevTx = 0;
        auto it = prevMap.find(name);
        if (it != prevMap.end()) {
          prevRx = it->second.first;
          prevTx = it->second.second;
        }
        double rx_bps = double(rx - prevRx) / dt;
        double tx_bps = double(tx - prevTx) / dt;
        data["payload"]["net"]["speed"]["interfaces"].push_back({{"name", name}, {"rx_bps", rx_bps}, {"tx_bps", tx_bps}});
      }
    }

    // store snapshot
    fPrevNetStats = currNet;
    fPrevNetTs = now;
    fHavePrevNet = true;
  }
  catch (...) {
    // if stats retrieval fails, continue without them
  }
  for (const auto & pair : fClients) {
    json userData;
    userData["wsId"]     = pair.first;
    userData["username"] = pair.second.GetUsername();
    const auto connectedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        pair.second.GetConnectedAt().time_since_epoch()).count();
    userData["connectedAt"] = connectedAtMs;
    data["payload"]["users"].push_back(userData);
  }
  BroadcastUnsafe(data.dump());

  return kTRUE;
}

} // namespace Ndmspc
