#ifndef Ndmspc_NThreadData_H
#define Ndmspc_NThreadData_H
#include <vector>
#include <thread>
#include <cstddef>
#include <mutex>
#include <TObject.h>

namespace Ndmspc {

/**
 * @class NThreadData
 * @brief Thread-local data object for NDMSPC processing.
 *
 * Stores per-thread statistics, thread ID, assigned index, and provides thread-safe access.
 * Supports processing, printing, and management of thread-specific data.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NThreadData : public TObject {
  public:
  /// Setters for data members

  /**
   * @brief Set the number of items processed by the thread.
   * @param itemCount Number of items.
   */
  void SetItemCount(long long itemCount) { fItemCount = itemCount; }

  /**
   * @brief Set the sum of coordinates processed by the thread.
   * @param coordSum Sum of coordinates.
   */
  void SetCoordSum(long long coordSum) { fCoordSum = coordSum; }

  /**
   * @brief Set the thread's unique identifier.
   * @param threadId Thread ID.
   */
  void SetThreadId(std::thread::id threadId) { fThreadId = threadId; }

  /**
   * @brief Set whether the thread ID has been assigned.
   * @param idSet True if ID is set, false otherwise.
   */
  void SetIdSet(bool idSet) { fIdSet = idSet; }

  /**
   * @brief Set the assigned index for the thread.
   * @param assignedIndex Index assigned to the thread.
   */
  void SetAssignedIndex(size_t assignedIndex) { fAssignedIndex = assignedIndex; }

  /// Getters for data members

  /**
   * @brief Get the number of items processed by the thread.
   * @return Number of items.
   */
  long long GetItemCount() const { return fItemCount; }

  /**
   * @brief Get the sum of coordinates processed by the thread.
   * @return Sum of coordinates.
   */
  long long GetCoordSum() const { return fCoordSum; }

  /**
   * @brief Get the thread's unique identifier.
   * @return Thread ID.
   */
  std::thread::id GetThreadId() const { return fThreadId; }

  /**
   * @brief Check if the thread ID has been assigned.
   * @return True if ID is set, false otherwise.
   */
  bool GetIdSet() const { return fIdSet; }

  /**
   * @brief Get the assigned index for the thread.
   * @return Index assigned to the thread.
   */
  size_t GetAssignedIndex() const { return fAssignedIndex; }
  /// Shared mutex for thread-safe operations
  static std::mutex fSharedMutex;

  /**
   * @brief Default constructor.
   */
  NThreadData();

  /**
   * @brief Virtual destructor.
   */
  virtual ~NThreadData();

  /**
   * @brief Process method for thread data.
   * @param coords Vector of coordinates to process.
   */
  void Process(const std::vector<int> & coords);

  /**
   * @brief Print method override.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  private:
  long long       fItemCount = 0;         ///< Number of items processed
  long long       fCoordSum  = 0;         ///< Sum of coordinates
  std::thread::id fThreadId;              ///< Thread ID
  bool            fIdSet         = false; ///< Flag to indicate if the thread ID is set
  size_t          fAssignedIndex = 0;     ///< Index assigned to this object

  /// \cond CLASSIMP
  ClassDef(NThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
