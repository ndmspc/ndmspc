#include "NX509Authenticator.h"

#include <httplib.h>

#include "ndmspc/core/NLogger.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace Ndmspc {

namespace {

// Every httplib connection is served on its own worker thread (thread pool).
// The certificate verification callback runs on the same thread as the request
// handler, so a thread-local is sufficient to carry the verified identity from
// the TLS handshake to the handler that serves the request.
thread_local std::string gPeerIdentity;

// Identity attribute ("cn" or "dn") captured once at configuration time.
std::string gIdentityAttr = "cn";

// Extracts the identity of a verified client certificate. identityAttr == "cn"
// returns the Common Name; otherwise the full subject DN.
std::string ExtractIdentityFromX509(::X509 * cert, const std::string & identityAttr)
{
  if (!cert) return {};
  ::X509_NAME * subject = X509_get_subject_name(cert);
  if (!subject) return {};
  if (identityAttr == "cn") {
    char cn[256] = {0};
    const int len = X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
    if (len <= 0) return {};
    return std::string(cn, static_cast<size_t>(len));
  }
  char * line = X509_NAME_oneline(subject, nullptr, 0);
  if (!line) return {};
  std::string dn(line);
  ::OPENSSL_free(line);
  return dn;
}

// Raw OpenSSL server-side client-certificate verification callback. Overwrites
// the thread-local identity with the subject of each successfully verified
// certificate, so after a successful mTLS handshake gPeerIdentity holds the
// client's identity (CN or DN).
int ClientCertVerifyCallback(int preverify_ok, X509_STORE_CTX * store)
{
  ::X509 * cert = X509_STORE_CTX_get_current_cert(store);
  if (preverify_ok && cert) gPeerIdentity = ExtractIdentityFromX509(cert, gIdentityAttr);
  return preverify_ok;
}

// Rewrites the "username" field of NDMSPC JSON WebSocket messages so the client
// sees its certificate-derived identity.
std::string RewriteUsernameInJson(const std::string & payload, const std::string & username)
{
  try {
    auto json = nlohmann::json::parse(payload);
    if (!json.is_object()) return payload;
    const auto event = json.value("event", "");
    if (event == "welcome" || event == "clients" || event == "heartbeat") {
      if (json.contains("payload") && json["payload"].is_object()) {
        auto & pl = json["payload"];
        if (pl.contains("username")) pl["username"] = username;
        if (pl.contains("users") && pl["users"].is_array()) {
          for (auto & user : pl["users"]) {
            if (user.contains("username")) user["username"] = username;
          }
        }
      }
      return json.dump();
    }
  } catch (...) {
  }
  return payload;
}

} // namespace

struct NX509Authenticator::Impl {
  explicit Impl(NX509Config cfg) : config(std::move(cfg)) {}

  NX509Config config;
  std::string internalBase;
  std::string internalWsBase; ///< internalBase with the ws:// scheme (httplib WS client rejects http://)
  std::unique_ptr<httplib::SSLServer> server;
  std::thread listenThread;
  std::atomic<bool> running{false};
};

NX509Authenticator::NX509Authenticator(NX509Config config) : fImpl(std::make_unique<Impl>(std::move(config))) {}
NX509Authenticator::~NX509Authenticator() { Stop(); }

bool NX509Authenticator::IsRunning() const { return fImpl->running.load(); }

std::string NX509Authenticator::ExtractIdentity(const std::string & certPem, const std::string & identityAttr)
{
  ::BIO * bio = BIO_new_mem_buf(certPem.data(), static_cast<int>(certPem.size()));
  if (!bio) return {};
  ::X509 * cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!cert) return {};
  std::string identity = ExtractIdentityFromX509(cert, identityAttr);
  ::X509_free(cert);
  return identity;
}

bool NX509Authenticator::Start(const std::string & listenHost, int port, const std::string & internalBase)
{
  auto & impl = *fImpl;
  impl.config.Validate();
  impl.internalBase = internalBase;
  // httplib's WebSocket client only accepts ws:// and wss:// URLs; the internal
  // ROOT engine is reached over plain HTTP, so translate the scheme for the
  // WebSocket bridge (it throws on http:// and would drop the connection).
  impl.internalWsBase = internalBase;
  if (impl.internalWsBase.rfind("https://", 0) == 0) {
    impl.internalWsBase.replace(0, 5, "wss");
  } else if (impl.internalWsBase.rfind("http://", 0) == 0) {
    impl.internalWsBase.replace(0, 4, "ws");
  }

  const char * caFile = impl.config.caFile.empty() ? nullptr : impl.config.caFile.c_str();
  const char * caPath = impl.config.caPath.empty() ? nullptr : impl.config.caPath.c_str();
  auto server = std::make_unique<httplib::SSLServer>(impl.config.certFile.c_str(),
                                                     impl.config.keyFile.c_str(), caFile, caPath);
  if (!server->is_valid() || !server->tls_context()) {
    NLogError("NX509 server: failed to load certificate/key (last SSL error %d)", server->ssl_last_error());
    return false;
  }

  auto ctx = server->tls_context();
  gIdentityAttr = impl.config.identity;
  // Enable client-certificate (mutual TLS) verification with a plain OpenSSL
  // callback. This both enforces the request/verify policy and captures the
  // verified subject as the identity, without httplib's single global verify
  // callback slot (which would be shared with any in-process SSLClient).
  ::SSL_CTX * sslCtx = static_cast<::SSL_CTX *>(ctx);
  const int verifyMode = impl.config.verifyOptional
                             ? SSL_VERIFY_PEER
                             : (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT);
  SSL_CTX_set_verify(sslCtx, verifyMode, ClientCertVerifyCallback);

  auto forwardHttp = [&impl](const httplib::Request & req, httplib::Response & res) {
    const std::string identity = gPeerIdentity;
    if (identity.empty()) {
      res.status = 401;
      res.set_content("{\"error\":{\"code\":\"client_certificate_required\","
                      "\"message\":\"A valid client certificate is required\","
                      "\"retryable\":false}}",
                      "application/json");
      return;
    }
    // Rebase onto the internal /api/ tree.
    static const std::string prefix = "/api/";
    std::string path = req.path;
    if (path.rfind(prefix, 0) != 0 && path != "/api") path = prefix + (path.empty() ? "" : path.substr(1));

    httplib::Client client(impl.internalBase);
    client.set_connection_timeout(30);
    client.set_read_timeout(60);
    httplib::Headers headers = req.headers;
    headers.erase("Authorization");
    headers.emplace("X-NDMSPC-User", identity);
    headers.emplace("X-NDMSPC-Subject", identity);

    httplib::Result result;
    if (req.method == "POST") result = client.Post(path, headers, req.body, "application/json");
    else if (req.method == "DELETE") result = client.Delete(path, headers);
    else if (req.method == "PUT") result = client.Put(path, headers, req.body, "application/json");
    else if (req.method == "PATCH") result = client.Patch(path, headers, req.body, "application/json");
    else result = client.Get(path, headers);

    if (!result) {
      res.status = 502;
      res.set_content("{\"error\":{\"code\":\"provider_unavailable\","
                      "\"message\":\"Backend unavailable\",\"retryable\":true}}",
                      "application/json");
      return;
    }
    res.status = result->status;
    const auto & ct = result->get_header_value("Content-Type");
    res.set_content(result->body, ct.empty() ? "application/json" : ct);
    res.set_header("X-NDMSPC-User", identity);
    res.set_header("X-NDMSPC-Subject", identity);
  };
  std::function<void(const httplib::Request &, httplib::Response &)> httpHandler = forwardHttp;
  server->Get(R"(/api.*)", httpHandler);
  server->Post(R"(/api.*)", httpHandler);
  server->Delete(R"(/api.*)", httpHandler);
  server->Put(R"(/api.*)", httpHandler);
  server->Patch(R"(/api.*)", httpHandler);

  // WebSocket: mTLS already authenticated this connection, so the client must
  // NOT send the OIDC-style "authenticate" frame. Bridge frames to the internal
  // ROOT WebSocket engine and rewrite usernames to the certificate identity.
  server->WebSocket("/ws/root.websocket",
                    [&impl](const httplib::Request &, httplib::ws::WebSocket & clientWs) {
    const std::string identity = gPeerIdentity;
    const std::string user = identity.empty() ? "anonymous" : identity;

    const std::string upstreamUrl = impl.internalWsBase + "/ws/root.websocket";
    httplib::ws::WebSocketClient up(upstreamUrl);
    up.set_websocket_ping_interval(30);
    up.set_connection_timeout(30);
    auto upResult = up.connect();
    if (!upResult) {
      NLogError("NX509 WS bridge: upstream connect to %s failed: %s", upstreamUrl.c_str(),
                httplib::to_string(upResult.error()).c_str());
      clientWs.close(httplib::ws::CloseStatus::InternalError, "backend unavailable");
      return;
    }

    // Upstream -> external client relay.
    std::thread upstream([&]() {
      std::string msg;
      while (up.read(msg) != httplib::ws::ReadResult::Fail) clientWs.send(RewriteUsernameInJson(msg, user));
      clientWs.close(httplib::ws::CloseStatus::Normal, "upstream closed");
    });

    // External client -> upstream relay.
    std::string msg;
    while (clientWs.read(msg) != httplib::ws::ReadResult::Fail) {
      if (!up.send(msg)) break;
    }
    up.close();
    upstream.join();
  });

  // Bind synchronously on the calling thread so a bind failure is reported
  // accurately to Start(). httplib's listen() blocks in the accept loop and
  // only returns when the server stops, so it cannot be used to signal bind
  // success: waiting on it deadlocks the caller before it can drive the ROOT
  // main-thread event loop, which is what processes the internal engine's
  // queued requests. bind_to_port() + listen_after_bind() split the phases.
  impl.server = std::move(server);
  if (!impl.server->bind_to_port(listenHost, port)) {
    impl.running.store(false);
    NLogError("NX509 server: failed to bind/listen on %s:%d", listenHost.c_str(), port);
    return false;
  }
  impl.running.store(true);
  impl.listenThread = std::thread([&impl]() {
    if (!impl.server->listen_after_bind()) {
      impl.running.store(false);
      NLogError("NX509 server: accept loop terminated");
    }
  });
  NLogInfo("NX509 (mutual TLS) front-door listening on %s:%d -> internal %s", listenHost.c_str(), port,
           impl.internalBase.c_str());
  return true;
}

void NX509Authenticator::Stop()
{
  auto & impl = *fImpl;
  if (impl.server) impl.server->stop();
  if (impl.listenThread.joinable()) impl.listenThread.join();
  impl.running.store(false);
}

} // namespace Ndmspc