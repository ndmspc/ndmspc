#ifndef Ndmspc_NThreadData_H
#define Ndmspc_NThreadData_H
#include <vector>
#include <thread>
#include <cstddef>
#include <TObject.h>

namespace Ndmspc {
class NThreadData : public TObject {
  public:
  // write getter and setter for data members
  void SetItemCount(long long itemCount) { fItemCount = itemCount; }
  void SetCoordSum(long long coordSum) { fCoordSum = coordSum; }
  void SetThreadId(std::thread::id threadId) { fThreadId = threadId; }
  void SetIdSet(bool idSet) { fIdSet = idSet; }
  void SetAssignedIndex(size_t assignedIndex) { fAssignedIndex = assignedIndex; }

  long long         GetItemCount() const { return fItemCount; }
  long long         GetCoordSum() const { return fCoordSum; }
  std::thread::id   GetThreadId() const { return fThreadId; }
  bool              GetIdSet() const { return fIdSet; }
  size_t            GetAssignedIndex() const { return fAssignedIndex; }
  static std::mutex fSharedMutex;

  // Default constructor
  NThreadData();
  // Virtual destructor
  virtual ~NThreadData();

  // Process method
  void Process(const std::vector<int> & coords);

  // Print method override
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
