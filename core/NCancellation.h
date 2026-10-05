#ifndef NdmspcCoreNCancellation_H
#define NdmspcCoreNCancellation_H

#include <atomic>

namespace Ndmspc {

/**
 * @class NCancellation
 * @brief The process-wide "cancelled" flag the long loops poll.
 *
 * The server serializes tool execution (one action at a time), so one current token is enough: the
 * worker sets the token of the job it is running, a cancel request flips that token, and the loops
 * read {@link IsCancelled} at their boundaries. Nothing here knows about requests or threads — the
 * server owns the token it publishes.
 */
class NCancellation {
  public:
  /**
   * @brief Publish the token of the action now running (nullptr when none is).
   * @param token The running job's flag; the caller keeps it alive while the job runs.
   */
  static void SetCurrent(const std::atomic<bool> * token);
  /// @brief Clear the current token (the action is done).
  static void ClearCurrent();
  /**
   * @brief Whether the running action has been asked to stop.
   * @return True when a token is published and set.
   */
  static bool IsCancelled();

  private:
  /// The flag of the action now running; nullptr when none is.
  static std::atomic<const std::atomic<bool> *> fCurrent;
};

} // namespace Ndmspc
#endif
