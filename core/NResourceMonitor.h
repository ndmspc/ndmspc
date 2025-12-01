#ifndef Ndmspc_NResourceMonitor_H
#define Ndmspc_NResourceMonitor_H
#include <sys/resource.h>
#include <chrono>
#include <TObject.h>
#include <THnSparse.h>

namespace Ndmspc {

/**
 * @class NResourceMonitor
 * @brief Monitors and records resource usage (CPU, memory, wall time) for processes or threads.
 *
 * Provides methods to start and stop resource monitoring, fill resource usage data
 * into a THnSparse histogram, and retrieve CPU and memory usage statistics.
 * Uses system resource tracking and wall clock timing to measure performance.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NResourceMonitor : public TObject {
  public:
  /**
   * @brief Default constructor.
   */
  NResourceMonitor();

  /**
   * @brief Destructor.
   */
  virtual ~NResourceMonitor();

  /**
   * @brief Prints the resource monitor information.
   * @param option Optional print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Initializes the THnSparse histogram for resource data.
   * @param hns Pointer to THnSparse to initialize.
   * @return Pointer to initialized THnSparse.
   */
  THnSparse * Initialize(THnSparse * hns);

  /**
   * @brief Fills resource usage data into the histogram.
   * @param coords Array of coordinates for histogram axes.
   * @param threadId Identifier for the thread.
   */
  void Fill(Int_t * coords, int threadId);

  /**
   * @brief Returns the pointer to the THnSparse histogram.
   */
  THnSparse * GetHnSparse() const { return fHnSparse; }

  /**
   * @brief Returns the starting resource usage structure.
   */
  rusage & GetUsageStart() { return fUsageStart; }

  /**
   * @brief Returns the ending resource usage structure.
   */
  rusage & GetUsageEnd() { return fUsageEnd; }

  /**
   * @brief Calculates and returns the CPU usage between Start and End.
   */
  double GetCpuUsage() const;

  /**
   * @brief Returns the difference in memory usage (in kilobytes) between Start and End.
   */
  long GetMemoryUsageDiff() const { return fUsageEnd.ru_maxrss - fUsageStart.ru_maxrss; }

  /**
   * @brief Records the starting resource usage and wall time.
   */
  void Start();

  /**
   * @brief Records the ending resource usage and wall time.
   */
  void End();

  private:
  THnSparse *                                fHnSparse{nullptr};      ///< THnSparse histogram for resource data
  rusage                                     fUsageStart;             ///< Resource usage at start
  rusage                                     fUsageEnd;               ///< Resource usage at end
  std::chrono::_V2::system_clock::time_point fWallStart;              ///< Wall clock start time
  std::chrono::_V2::system_clock::time_point fWallEnd;                ///< Wall clock end time
  std::vector<std::string>                   fNames = {"cpu", "mem"}; ///< Axis names

  /**
   * @brief Helper function to convert timeval to double seconds.
   * @param tv timeval structure.
   * @return Time in seconds as double.
   */
  double timevalToDouble(const timeval & tv) const
  {
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1000000.0;
  }

  /// \cond CLASSIMP
  ClassDef(NResourceMonitor, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
