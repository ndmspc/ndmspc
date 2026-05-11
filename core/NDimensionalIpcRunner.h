#ifndef Ndmspc_NDimensionalIpcRunner_H
#define Ndmspc_NDimensionalIpcRunner_H

#include <string>
#include <vector>
#include <sys/types.h>
#include <Rtypes.h>

namespace Ndmspc {

class NThreadData;

/// @brief Helper for running worker loops and encoding/decoding IPC frames
///
/// `NDimensionalIpcRunner` implements the low-level protocol used by the
/// supervisor and workers to exchange framed messages over ZeroMQ. It also
/// contains helper functions used by both the forked IPC workers and TCP
/// worker mode.
class NDimensionalIpcRunner {
  public:
  /**
   * @brief Send a multi-frame message over the given socket.
   * @param socket ZeroMQ socket pointer.
   * @param frames Vector of frame payloads to send in order.
   * @return True on success, false on failure.
   */
  static bool SendFrames(void * socket, const std::vector<std::string> & frames);

  /**
   * @brief Receive a multi-frame message from the given socket.
   * @param socket ZeroMQ socket pointer.
   * @param outFrames Output vector populated with received frames.
   * @return True on success, false on failure (or timeout).
   */
  static bool ReceiveFrames(void * socket, std::vector<std::string> & outFrames);

  /**
   * @brief Build a canonical worker identity string for a given index.
   * @param workerIndex Worker index (0-based).
   * @return Identity string used by workers (e.g., "wk_000001").
   */
  static std::string BuildWorkerIdentity(size_t workerIndex);

  /**
   * @brief Serialize coordinates into a compact string representation.
   * @param coords Vector of integer coordinates.
   * @return Serialized string representation.
   */
  static std::string SerializeCoords(const std::vector<int> & coords);

  /**
   * @brief Serialize a vector of Long64_t IDs into a string.
   * @param ids Vector of IDs to serialize.
   * @return Serialized string representation.
   */
  static std::string SerializeIds(const std::vector<Long64_t> & ids);

  /**
   * @brief Entry point for a worker process or thread.
   * @param endpoint Endpoint string to connect to (ipc:// or tcp://).
   * @param workerIndex Index assigned to this worker.
   * @param worker Pointer to the worker's `NThreadData` object.
   * @return Exit code (0 on success).
   */
  static int  WorkerLoop(const std::string & endpoint, size_t workerIndex, NThreadData * worker);

  /**
   * @brief Core task loop used by workers to receive tasks and report results.
   * @param dealer ZeroMQ DEALER socket used by the worker.
   * @param workerIndex Worker index.
   * @param worker Pointer to `NThreadData` representing the worker.
   * @return 0 on clean exit, non-zero on error.
   */
  static int  TaskLoop(void * dealer, size_t workerIndex, NThreadData * worker);

  /**
   * @brief Wait for a set of child PIDs to exit up to a timeout.
   * @param pids Vector of PIDs to wait for.
   * @param timeoutMs Timeout in milliseconds, -1 for infinite.
   * @return True if all children exited, false if timeout or error.
   */
  static bool WaitForChildProcesses(const std::vector<pid_t> & pids, int timeoutMs = -1);

  /**
   * @brief Cleanup (reap) any child processes in the provided list.
   * @param pids Vector of PIDs to reap.
   */
  static void CleanupChildProcesses(const std::vector<pid_t> & pids);

  private:
  /**
   * @brief Parse a serialized coordinate string into a vector of ints.
   */
  static std::vector<int> ParseCoords(const std::string & coordsStr);

  /**
   * @brief Parse a serialized IDs string into a vector of Long64_t.
   */
  static std::vector<Long64_t> ParseIds(const std::string & idsStr);
};

} // namespace Ndmspc

#endif
