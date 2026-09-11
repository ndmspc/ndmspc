#include <gtest/gtest.h>

#include "ndmspc/http/NX509Authenticator.h"
#include "ndmspc/http/NX509Config.h"

#include <httplib.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

std::string TempDir()
{
  const std::string base = "/tmp/ndmspc-x509-test";
  std::system(("rm -rf " + base).c_str());
  std::system(("mkdir -p " + base).c_str());
  return base;
}

bool GenerateCerts(const std::string & dir)
{
  auto run = [&](const std::string & cmd) {
    return std::system((cmd + " </dev/null >/dev/null 2>&1").c_str()) == 0;
  };
  // CA
  if (!run("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out " + dir + "/ca.key")) return false;
  if (!run("openssl req -x509 -new -key " + dir + "/ca.key -sha256 -days 2 -subj \"/CN=NDMSPC Test CA\" -out " + dir + "/ca.pem")) return false;
  // Server cert signed by the CA with SAN for 127.0.0.1
  if (!run("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out " + dir + "/server.key")) return false;
  if (!run("openssl req -new -key " + dir + "/server.key -subj \"/CN=ndmspc-server\" -out " + dir + "/server.csr")) return false;
  { std::ofstream f(dir + "/server.cnf"); f << "subjectAltName=IP:127.0.0.1\n"; }
  if (!run("openssl x509 -req -in " + dir + "/server.csr -CA " + dir + "/ca.pem -CAkey " + dir + "/ca.key -CAcreateserial -days 2 -sha256 -extfile " + dir + "/server.cnf -out " + dir + "/server.pem")) return false;
  // Client cert signed by the CA
  if (!run("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out " + dir + "/client.key")) return false;
  if (!run("openssl req -new -key " + dir + "/client.key -subj \"/CN=alice\" -out " + dir + "/client.csr")) return false;
  { std::ofstream f(dir + "/client.cnf"); f << "basicConstraints=CA:FALSE\nextendedKeyUsage=clientAuth\n"; }
  if (!run("openssl x509 -req -in " + dir + "/client.csr -CA " + dir + "/ca.pem -CAkey " + dir + "/ca.key -CAcreateserial -days 2 -sha256 -extfile " + dir + "/client.cnf -out " + dir + "/client.pem")) return false;
  return true;
}

std::string ReadFile(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool RunShell(const std::string & command)
{
  return std::system((command + " </dev/null >/dev/null 2>&1").c_str()) == 0;
}

bool RehashCaDir(const std::string & dir) { return RunShell("openssl rehash " + dir); }

// Creates a client certificate whose issuer is a CA that is NOT the directory's
// CA, used to prove the CA directory is actually consulted during verification.
bool GenerateUntrustedClientCert(const std::string & dir)
{
  if (!RunShell("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out " + dir + "/otherca.key")) return false;
  if (!RunShell("openssl req -x509 -new -key " + dir + "/otherca.key -sha256 -days 2 -subj \"/CN=Other CA\" -out " + dir + "/otherca.pem")) return false;
  if (!RunShell("openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out " + dir + "/mallory.key")) return false;
  if (!RunShell("openssl req -new -key " + dir + "/mallory.key -subj \"/CN=mallory\" -out " + dir + "/mallory.csr")) return false;
  { std::ofstream f(dir + "/mallory.cnf"); f << "basicConstraints=CA:FALSE\nextendedKeyUsage=clientAuth\n"; }
  if (!RunShell("openssl x509 -req -in " + dir + "/mallory.csr -CA " + dir + "/otherca.pem -CAkey " + dir + "/otherca.key -CAcreateserial -days 2 -sha256 -extfile " + dir + "/mallory.cnf -out " + dir + "/mallory.pem")) return false;
  return true;
}

TEST(X509ConfigTest, ValidateRequiresCertAndKey)
{
  Ndmspc::NX509Config cfg;
  cfg.certFile = "/tmp/nonexistent-cert.pem";
  EXPECT_TRUE(cfg.Enabled()); // a cert path enables X509 mode
  EXPECT_THROW(cfg.Validate(), std::invalid_argument); // key missing
}

TEST(X509ConfigTest, ValidateRejectsMissingFiles)
{
  Ndmspc::NX509Config cfg;
  cfg.certFile = "/does/not/exist.pem";
  cfg.keyFile = "/does/not/exist.key";
  EXPECT_TRUE(cfg.Enabled());
  EXPECT_THROW(cfg.Validate(), std::invalid_argument); // cert/key files don't exist
}

TEST(X509ConfigTest, ValidateAcceptsValidConfiguration)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.caFile = dir + "/ca.pem";
  cfg.identity = "cn";
  EXPECT_NO_THROW(cfg.Validate());
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509ConfigTest, ValidateAcceptsCaPathDirectory)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.caPath = dir; // a directory of hashed CAs; hashing is validated by OpenSSL, not here
  EXPECT_NO_THROW(cfg.Validate());
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509ConfigTest, ValidateRejectsCaPathThatIsNotADirectory)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.caPath = dir + "/ca.pem"; // a regular file, not a directory
  EXPECT_THROW(cfg.Validate(), std::invalid_argument);
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509ConfigTest, ValidateRejectsMissingCaPath)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.caPath = "/does/not/exist";
  EXPECT_THROW(cfg.Validate(), std::invalid_argument);
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509ConfigTest, ValidateRequiresACaWhenCertificatesAreMandatory)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.verifyOptional = false;
  EXPECT_THROW(cfg.Validate(), std::invalid_argument);
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509ConfigTest, ValidateAllowsNoCaWhenVerificationIsOptional)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.verifyOptional = true;
  EXPECT_NO_THROW(cfg.Validate());
  std::system(("rm -rf " + dir).c_str());
}

// End-to-end: starts the mTLS front-door with a hashed CA directory and drives
// real TLS handshakes against it.
struct X509TestServer {
  std::string dir;
  std::string caDir;
  int port{0};
  Ndmspc::NX509Config config;
  std::unique_ptr<Ndmspc::NX509Authenticator> frontDoor;

  bool Start()
  {
    dir = TempDir();
    if (!GenerateCerts(dir)) return false;
    caDir = dir + "/ca-dir";
    if (!RunShell("mkdir -p " + caDir)) return false;
    if (!RunShell("cp " + dir + "/ca.pem " + caDir + "/")) return false;
    if (!RehashCaDir(caDir)) return false;
    config.certFile = dir + "/server.pem";
    config.keyFile = dir + "/server.key";
    config.caPath = caDir;
    port = 20000 + (::getpid() % 500);
    config.internalPort = 19000 + (::getpid() % 500);
    frontDoor = std::make_unique<Ndmspc::NX509Authenticator>(config);
    // The internal backend is intentionally unreachable: a request that passes
    // TLS verification reaches the forwarding handler and fails with 502.
    return frontDoor->Start("127.0.0.1", port, "http://127.0.0.1:1");
  }

  ~X509TestServer()
  {
    if (frontDoor) frontDoor->Stop();
    if (!dir.empty()) std::system(("rm -rf " + dir).c_str());
  }
};

TEST(X509CaPathTest, VerifiesClientCertificateAgainstHashedCaDirectory)
{
  X509TestServer server;
  ASSERT_TRUE(server.Start());

  httplib::SSLClient client("127.0.0.1", server.port, server.dir + "/client.pem", server.dir + "/client.key");
  client.set_ca_cert_path("", server.caDir);
  client.enable_server_certificate_verification(true);

  auto result = client.Get("/api/state");
  ASSERT_TRUE(result) << "mTLS request with a CA-directory-trusted client certificate failed";
  EXPECT_EQ(result->status, 502);
}

TEST(X509CaPathTest, RejectsClientWithoutCertificate)
{
  X509TestServer server;
  ASSERT_TRUE(server.Start());

  httplib::SSLClient client("127.0.0.1", server.port);
  client.set_ca_cert_path("", server.caDir);
  client.enable_server_certificate_verification(true);

  EXPECT_FALSE(client.Get("/api/state"));
}

TEST(X509CaPathTest, RejectsClientCertificateFromUntrustedCa)
{
  X509TestServer server;
  ASSERT_TRUE(server.Start());
  ASSERT_TRUE(GenerateUntrustedClientCert(server.dir));

  httplib::SSLClient client("127.0.0.1", server.port, server.dir + "/mallory.pem", server.dir + "/mallory.key");
  client.set_ca_cert_path("", server.caDir);
  client.enable_server_certificate_verification(true);

  EXPECT_FALSE(client.Get("/api/state"));
}

TEST(X509ConfigTest, ValidateRejectsUnknownIdentityAttribute)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  Ndmspc::NX509Config cfg;
  cfg.certFile = dir + "/server.pem";
  cfg.keyFile = dir + "/server.key";
  cfg.caFile = dir + "/ca.pem";
  cfg.identity = "emailsan";
  EXPECT_THROW(cfg.Validate(), std::invalid_argument);
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509IdentityTest, ExtractsCommonName)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  const std::string clientPem = ReadFile(dir + "/client.pem");
  EXPECT_EQ(Ndmspc::NX509Authenticator::ExtractIdentity(clientPem, "cn"), "alice");
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509IdentityTest, ExtractsFullDistinguishedName)
{
  const std::string dir = TempDir();
  ASSERT_TRUE(GenerateCerts(dir));
  const std::string clientPem = ReadFile(dir + "/client.pem");
  const std::string dn = Ndmspc::NX509Authenticator::ExtractIdentity(clientPem, "dn");
  EXPECT_NE(dn.find("CN=alice"), std::string::npos);
  std::system(("rm -rf " + dir).c_str());
}

TEST(X509IdentityTest, EmptyPemYieldsEmptyIdentity)
{
  EXPECT_EQ(Ndmspc::NX509Authenticator::ExtractIdentity("", "cn"), "");
  EXPECT_EQ(Ndmspc::NX509Authenticator::ExtractIdentity("not a certificate", "dn"), "");
}

} // namespace