#include "NWsHandler.h"
#include <THttpCallArg.h>
#include <TTimer.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <unordered_map>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NGnHttpServer.h"

namespace Ndmspc {
namespace {

bool IsAuthenticationMessage(const json & message)
{
  return message.is_object() && message.value("event", "") == "authenticate" &&
         message.contains("token") && message["token"].is_string() && !message["token"].get_ref<const std::string &>().empty();
}

bool IsAuthorizationHeader(std::string name)
{
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) { return std::tolower(value); });
  return name == "authorization" || name == "x-ndmspc-user" || name == "x-ndmspc-subject" ||
         name == "x-ndmspc-token-expires" || name == "x-ndmspc-authenticated";
}

} // namespace

NWsHandler::NWsHandler(const char * name, const char * title, std::shared_ptr<IOidcTokenVerifier> verifier,
                       std::chrono::seconds authenticationTimeout)
    : THttpWSHandler(name, title, kFALSE), fOidcVerifier(std::move(verifier)),
      fAuthenticationTimeout(authenticationTimeout), fServerStartedAt(std::chrono::system_clock::now())
{
}

NWsHandler::~NWsHandler() = default;

Bool_t NWsHandler::ProcessWS(THttpCallArg * arg)
{
  if (!arg || arg->GetWSId() == 0) return kTRUE;
  const auto wsId = arg->GetWSId();

  if (arg->IsMethod("WS_CONNECT")) {
    NLogTrace("WS_CONNECT received for path: /%s", arg->GetPathName());
    return kTRUE;
  }

  if (arg->IsMethod("WS_READY")) {
    NLogTrace("WS_READY received. Connection established with ID: %lld", wsId);
    if (fOidcVerifier) {
      std::lock_guard lock(fMutex);
      fPendingClients[wsId] = {std::chrono::steady_clock::now(), false};
    } else {
      ActivateAnonymousClient(wsId);
    }
    return kTRUE;
  }

  if (arg->IsMethod("WS_CLOSE")) {
    RemoveClientAndAnnounce(wsId);
    return kTRUE;
  }

  if (arg->IsMethod("WS_DATA")) {
    std::string receivedStr(static_cast<const char *>(arg->GetPostData()), arg->GetPostDataLength());
    NLogTrace("WS_DATA from ID %lld", wsId);

    json parsed;
    bool parsedJson = false;
    try {
      parsed = json::parse(receivedStr);
      parsedJson = true;
    }
    catch (const json::parse_error &) {
    }

    if (fOidcVerifier) {
      bool pending = false;
      bool active = false;
      bool expired = false;
      {
        std::lock_guard lock(fMutex);
        pending = fPendingClients.contains(wsId);
        const auto client = fClients.find(wsId);
        active = client != fClients.end();
        expired = active && !client->second.IsTokenValidAt(std::chrono::system_clock::now());
      }
      if (expired) {
        SendAuthenticationError(wsId, "token_expired", "Access token expired", true);
        RemoveClientAndAnnounce(wsId);
        CloseWS(wsId);
        return kTRUE;
      }
      if (parsedJson && IsAuthenticationMessage(parsed)) {
        HandleAuthentication(wsId, parsed);
        return kTRUE;
      }
      if (pending || !active) {
        SendAuthenticationError(wsId, pending ? "invalid_authentication_message" : "authentication_required",
                                "Authentication required", false);
        {
          std::lock_guard lock(fMutex);
          fPendingClients.erase(wsId);
        }
        CloseWS(wsId);
        return kTRUE;
      }
    }

    const bool isApiRequest = parsedJson && parsed.is_object() && parsed.contains("path") && parsed["path"].is_string();
    if (isApiRequest) {
      json requestId = parsed.contains("requestId") ? parsed["requestId"] : json(nullptr);
      std::string method = parsed.value("method", "POST");
      std::string path = parsed["path"].get<std::string>();
      std::string query = parsed.value("query", "");
      json payload = parsed.contains("payload") ? parsed["payload"] : json::object();
      json headers = parsed.contains("headers") && parsed["headers"].is_object() ? parsed["headers"] : json::object();

      if (!Ndmspc::gNGnHttpServer) {
        NLogError("Cannot route WS_DATA to HTTP API: gNGnHttpServer is not set");
        return kTRUE;
      }

      auto httpArg = std::make_shared<THttpCallArg>();
      httpArg->SetMethod(method.c_str());
      httpArg->SetPathName("api");
      httpArg->SetFileName(path.c_str());
      if (!query.empty()) httpArg->SetQuery(query.c_str());
      httpArg->SetWSId(wsId);
      httpArg->SetPostData(payload.dump());

      // Carry the authenticated WebSocket identity onto the synthetic HTTP
      // argument. The connection already passed the WS authentication gate, so
      // NGnHttpServer skips the bearer check for requests with a nonzero WS id.
      // The username is set from the server-side client record only, never from
      // the client-supplied headers (Authorization and X-NDMSPC-* are stripped).
      {
        std::lock_guard lock(fMutex);
        const auto client = fClients.find(wsId);
        if (client != fClients.end() && !client->second.GetSubject().empty()) {
          httpArg->SetUserName(client->second.GetUsername().c_str());
        }
      }

      std::string headerBlock;
      for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (!it.value().is_string() || IsAuthorizationHeader(it.key())) continue;
        headerBlock += it.key() + ": " + it.value().get<std::string>() + "\r\n";
      }
      if (!headerBlock.empty()) httpArg->SetRequestHeader(headerBlock.c_str());

      Ndmspc::gNGnHttpServer->ProcessRequest(httpArg);

      std::string content(static_cast<const char *>(httpArg->GetContent()), httpArg->GetContentLength());
      json reply;
      reply["event"] = "ngnt_reply";
      reply["requestId"] = requestId;
      reply["contentType"] = httpArg->GetContentType();
      try {
        reply["payload"] = json::parse(content);
      }
      catch (const json::parse_error &) {
        reply["payload"] = content;
      }
      SendCharStarWS(wsId, reply.dump().c_str());
      return kTRUE;
    }

    std::vector<ULong_t> recipients;
    {
      std::lock_guard lock(fMutex);
      const auto sender = fClients.find(wsId);
      if (sender != fClients.end()) sender->second.IncrementMessageCount();
      for (const auto & [clientId, client] : fClients) {
        if (clientId != wsId && client.IsTokenValidAt(std::chrono::system_clock::now())) recipients.push_back(clientId);
      }
    }
    for (const auto recipient : recipients) SendCharStarWS(recipient, receivedStr.c_str());
    return kTRUE;
  }

  NLogError("Unknown WS method received: %s", arg->GetMethod());
  return kFALSE;
}

void NWsHandler::HandleAuthentication(ULong_t wsId, const json & message)
{
  bool initial = false;
  {
    std::lock_guard lock(fMutex);
    if (const auto pending = fPendingClients.find(wsId); pending != fPendingClients.end()) {
      if (pending->second.authenticationInProgress) return;
      pending->second.authenticationInProgress = true;
      initial = true;
    } else if (!fClients.contains(wsId)) {
      return;
    }
  }

  const auto result = fOidcVerifier->Verify(message["token"].get_ref<const std::string &>());
  if (!result) {
    bool oldTokenValid = false;
    {
      std::lock_guard lock(fMutex);
      if (initial) fPendingClients.erase(wsId);
      const auto client = fClients.find(wsId);
      oldTokenValid = client != fClients.end() && client->second.IsTokenValidAt(std::chrono::system_clock::now());
    }
    NLogWarning("WebSocket authentication failed for ID %lld: %s", wsId, result.diagnostic.c_str());
    const bool providerUnavailable = result.error == NOidcErrorCode::ProviderUnavailable;
    SendAuthenticationError(wsId, NOidcErrorCodeName(result.error), "Authentication failed", providerUnavailable);
    if (initial || !oldTokenValid) CloseWS(wsId);
    return;
  }

  bool activated = false;
  bool usernameChanged = false;
  bool identityChanged = false;
  {
    std::lock_guard lock(fMutex);
    if (initial) {
      if (!fPendingClients.erase(wsId)) return;
      fClients.emplace(wsId, NWsClientInfo(wsId, result.identity->subject, result.identity->preferredUsername,
                                          result.identity->expiresAt));
      activated = true;
    } else {
      const auto client = fClients.find(wsId);
      if (client == fClients.end()) return;
      if (client->second.GetSubject() != result.identity->subject) {
        identityChanged = true;
      } else {
        usernameChanged = client->second.GetUsername() != result.identity->preferredUsername;
        client->second.ReplaceIdentity(result.identity->subject, result.identity->preferredUsername,
                                       result.identity->expiresAt);
      }
    }
  }

  if (identityChanged) {
    SendAuthenticationError(wsId, "identity_change_not_allowed", "Identity change requires reconnecting", false);
    return;
  }

  json authenticated;
  authenticated["event"] = "authenticated";
  authenticated["payload"]["username"] = result.identity->preferredUsername;
  authenticated["payload"]["expiresAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
      result.identity->expiresAt.time_since_epoch()).count();
  SendCharStarWS(wsId, authenticated.dump().c_str());
  if (activated) SendWelcomeAndAnnounce(wsId, result.identity->preferredUsername);
  else if (usernameChanged) Broadcast(BuildClientsMessage().dump());
}

void NWsHandler::ActivateAnonymousClient(ULong_t wsId)
{
  const auto username = "User_" + std::to_string(wsId);
  {
    std::lock_guard lock(fMutex);
    fClients[wsId] = NWsClientInfo(wsId, username);
  }
  SendWelcomeAndAnnounce(wsId, username);
}

void NWsHandler::SendWelcomeAndAnnounce(ULong_t wsId, const std::string & username)
{
  json welcome;
  welcome["event"] = "welcome";
  welcome["payload"]["username"] = username;
  welcome["payload"]["wsId"] = wsId;
  SendCharStarWS(wsId, welcome.dump().c_str());

  for (const auto recipient : ClientIds()) {
    if (recipient != wsId) SendCharStarWS(recipient, (username + " has joined the chat!").c_str());
  }
  Broadcast(BuildClientsMessage().dump());
}

void NWsHandler::SendAuthenticationError(ULong_t wsId, const std::string & code, const std::string & message, bool retryable)
{
  json error;
  error["event"] = "authentication_error";
  error["payload"] = {{"code", code}, {"message", message}, {"retryable", retryable}};
  SendCharStarWS(wsId, error.dump().c_str());
}

void NWsHandler::RemoveClientAndAnnounce(ULong_t wsId)
{
  std::string username;
  {
    std::lock_guard lock(fMutex);
    fPendingClients.erase(wsId);
    const auto client = fClients.find(wsId);
    if (client == fClients.end()) return;
    username = client->second.GetUsername();
    fClients.erase(client);
  }
  json goodbye;
  goodbye["event"] = "goodbye";
  goodbye["payload"] = "Goodbye, " + username + "!";
  Broadcast(goodbye.dump());
  Broadcast(BuildClientsMessage().dump());
}

void NWsHandler::ExpireConnections()
{
  const auto now = std::chrono::system_clock::now();
  const auto steadyNow = std::chrono::steady_clock::now();
  std::vector<ULong_t> expired;
  std::vector<ULong_t> timedOut;
  {
    std::lock_guard lock(fMutex);
    for (const auto & [wsId, client] : fClients) {
      if (!client.IsTokenValidAt(now)) expired.push_back(wsId);
    }
    for (const auto & [wsId, pending] : fPendingClients) {
      if (steadyNow - pending.readyAt >= fAuthenticationTimeout) timedOut.push_back(wsId);
    }
  }
  for (const auto wsId : expired) {
    SendAuthenticationError(wsId, "token_expired", "Access token expired", true);
    RemoveClientAndAnnounce(wsId);
    CloseWS(wsId);
  }
  for (const auto wsId : timedOut) {
    SendAuthenticationError(wsId, "authentication_timeout", "Authentication timed out", true);
    {
      std::lock_guard lock(fMutex);
      fPendingClients.erase(wsId);
    }
    CloseWS(wsId);
  }
}

json NWsHandler::BuildClientsMessage() const
{
  json data;
  data["event"] = "clients";
  data["payload"]["users"] = json::array();
  std::lock_guard lock(fMutex);
  const auto now = std::chrono::system_clock::now();
  int count = 0;
  for (const auto & [wsId, client] : fClients) {
    if (!client.IsTokenValidAt(now)) continue;
    ++count;
    data["payload"]["users"].push_back({
        {"wsId", wsId},
        {"username", client.GetUsername()},
        {"connectedAt", std::chrono::duration_cast<std::chrono::milliseconds>(client.GetConnectedAt().time_since_epoch()).count()}});
  }
  data["payload"]["count"] = count;
  return data;
}

std::vector<ULong_t> NWsHandler::ClientIds() const
{
  std::vector<ULong_t> ids;
  std::lock_guard lock(fMutex);
  ids.reserve(fClients.size());
  const auto now = std::chrono::system_clock::now();
  for (const auto & [wsId, client] : fClients) {
    if (client.IsTokenValidAt(now)) ids.push_back(wsId);
  }
  return ids;
}

size_t NWsHandler::GetClientCount() const
{
  std::lock_guard lock(fMutex);
  const auto now = std::chrono::system_clock::now();
  return std::count_if(fClients.begin(), fClients.end(), [now](const auto & entry) {
    return entry.second.IsTokenValidAt(now);
  });
}

void NWsHandler::Broadcast(const std::string & message)
{
  for (const auto wsId : ClientIds()) SendCharStarWS(wsId, message.c_str());
}

void NWsHandler::BroadcastUnsafe(const std::string & message)
{
  Broadcast(message);
}

Bool_t NWsHandler::HandleTimer(TTimer *)
{
  ExpireConnections();
  json data;
  data["event"] = "heartbeat";
  data["payload"]["count"] = ++fServCnt;
  data["payload"]["clients"] = static_cast<int>(GetClientCount());
  data["payload"]["serverStartedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
      fServerStartedAt.time_since_epoch()).count();
  try {
    data["payload"]["system"] = NUtils::GetSystemStats();
    json currentFile = NUtils::GetTFileIOStats();
    data["payload"]["file"]["counters"] = currentFile;
    if (fHavePrevFile) {
      const auto now = std::chrono::steady_clock::now();
      double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - fPrevFileTs).count() / 1000.0;
      if (seconds <= 0) seconds = 1.0;
      data["payload"]["file"]["speed"]["total_read_bps"] =
          (currentFile.value("totalRead", 0ULL) - fPrevFileStats.value("totalRead", 0ULL)) / seconds;
      data["payload"]["file"]["speed"]["total_write_bps"] =
          (currentFile.value("totalWritten", 0ULL) - fPrevFileStats.value("totalWritten", 0ULL)) / seconds;
    }
    fPrevFileStats = currentFile;
    fPrevFileTs = std::chrono::steady_clock::now();
    fHavePrevFile = true;

    const auto now = std::chrono::steady_clock::now();
    json currentNet = NUtils::GetNetDevStats();
    data["payload"]["net"]["counters"] = currentNet;
    if (fHavePrevNet) {
      double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - fPrevNetTs).count() / 1000.0;
      if (seconds <= 0) seconds = 1.0;
      data["payload"]["net"]["speed"]["total_rx_bps"] =
          (currentNet.value("total_rx", 0ULL) - fPrevNetStats.value("total_rx", 0ULL)) / seconds;
      data["payload"]["net"]["speed"]["total_tx_bps"] =
          (currentNet.value("total_tx", 0ULL) - fPrevNetStats.value("total_tx", 0ULL)) / seconds;
    }
    fPrevNetStats = currentNet;
    fPrevNetTs = now;
    fHavePrevNet = true;
  }
  catch (...) {
  }
  data["payload"]["users"] = BuildClientsMessage()["payload"]["users"];
  Broadcast(data.dump());
  return kTRUE;
}

} // namespace Ndmspc
