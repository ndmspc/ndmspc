#include "NKeyPassphrase.h"

#include <cstdio>
#include <fstream>
#include <iterator>

#include <termios.h>
#include <unistd.h>

#include <TBase64.h>
#include <TString.h>

#include "ndmspc/core/NLogger.h"

namespace Ndmspc {

std::string NKeyPassphrase::Read(const std::string & prompt)
{
  std::fputs(prompt.c_str(), stderr);
  std::fflush(stderr);

  termios original{};
  if (::tcgetattr(STDIN_FILENO, &original) != 0) {
    std::fputs("\n", stderr);
    return {};
  }
  termios hidden = original;
  hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
  ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);

  std::string passphrase;
  char        ch = 0;
  while (std::fread(&ch, 1, 1, stdin) == 1) {
    if (ch == '\n' || ch == '\r') break;
    passphrase.push_back(ch);
  }

  ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
  std::fputs("\n", stderr);
  return passphrase;
}

bool NKeyPassphrase::IsEncrypted(const std::string & keyFile)
{
  std::ifstream in(keyFile, std::ios::binary);
  if (!in.is_open()) return false;
  const std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  return contents.find("ENCRYPTED PRIVATE KEY") != std::string::npos ||
         contents.find("Proc-Type: 4,ENCRYPTED") != std::string::npos ||
         contents.find("DEK-Info:") != std::string::npos;
}

bool NKeyPassphrase::ReadFile(const std::string & path, std::string & out)
{
  std::ifstream file(path);
  if (!file.is_open()) {
    NLogError("Cannot open the key passphrase file: %s", path.c_str());
    return false;
  }
  std::string encoded;
  std::getline(file, encoded);
  out = TBase64::Decode(TString(encoded)).Data();
  if (!out.empty() && out.back() == '\r') out.pop_back();
  return true;
}

bool NKeyPassphrase::Resolve(const std::string & keyFile, const std::string & inlinePass,
                             const std::string & passFile, std::string & out)
{
  if (!inlinePass.empty()) {
    out = inlinePass;
    return true;
  }
  if (!passFile.empty()) return ReadFile(passFile, out);
  if (keyFile.empty() || !IsEncrypted(keyFile)) return true;

  if (::isatty(STDIN_FILENO)) {
    out = Read("Enter passphrase for key '" + keyFile + "': ");
    if (out.empty()) {
      NLogError("No passphrase supplied for the encrypted key '%s'", keyFile.c_str());
      return false;
    }
    return true;
  }

  NLogError("The private key '%s' is encrypted but no passphrase was provided in a "
            "non-interactive session.",
            keyFile.c_str());
  NLogError("Provide one with --key-pass, --key-pass-file <base64 file>, or the "
            "NDMSPC_KEY_PASS environment variable.");
  return false;
}

} // namespace Ndmspc
