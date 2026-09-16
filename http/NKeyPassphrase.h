#ifndef Ndmspc_NKeyPassphrase_H
#define Ndmspc_NKeyPassphrase_H

#include <string>

namespace Ndmspc {

/**
 * @struct NKeyPassphrase
 * @brief Resolves the passphrase of an encrypted PEM private key.
 *
 * Shared by the NDMSPC clients that present a client certificate for mutual TLS.
 * The sources are tried in a fixed order - an explicit value (from a flag or an
 * environment variable), then a base64-encoded passphrase file, then an interactive
 * prompt with terminal echo disabled when the key is encrypted and a terminal is
 * attached. A non-interactive run with an encrypted key and no passphrase source
 * fails with an actionable error instead of blocking on stdin.
 */
struct NKeyPassphrase {
  /**
   * @brief Read a line from the terminal with echo disabled.
   * @param prompt Prompt written to stderr.
   * @return The line without its terminator (empty when the terminal cannot be switched).
   */
  static std::string Read(const std::string & prompt);

  /**
   * @brief Detect whether a PEM private key is passphrase-protected.
   * @param keyFile Path to the private key.
   * @return True for a PKCS#8 or legacy-PEM encrypted key.
   */
  static bool IsEncrypted(const std::string & keyFile);

  /**
   * @brief Decode a base64-encoded passphrase file (the ~/.globus/password.txt convention).
   * @param path Path to the file.
   * @param out The decoded passphrase.
   * @return False (with an error logged) when the file cannot be read.
   */
  static bool ReadFile(const std::string & path, std::string & out);

  /**
   * @brief Resolve the passphrase from the standard sources.
   * @param keyFile Path to the private key (may be empty).
   * @param inlinePass Passphrase given directly (e.g. --key-pass / NDMSPC_KEY_PASS).
   * @param passFile Base64-encoded passphrase file (e.g. --key-pass-file).
   * @param out The resolved passphrase; empty when the key needs none.
   * @return False (with an actionable error logged) when the key is encrypted and no
   *         source is available in a non-interactive session.
   */
  static bool Resolve(const std::string & keyFile, const std::string & inlinePass, const std::string & passFile,
                      std::string & out);
};

} // namespace Ndmspc
#endif
