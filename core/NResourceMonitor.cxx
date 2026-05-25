#include <cmath>
#include <limits>
#include <THnSparse.h>
#include <TROOT.h>
#include "NLogger.h"
#include "NUtils.h"
#include "NResourceMonitor.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NResourceMonitor);
/// \endcond

namespace Ndmspc {
NResourceMonitor::NResourceMonitor() : TObject() {}
NResourceMonitor::~NResourceMonitor() {}
void NResourceMonitor::Print(Option_t * /*option*/) const
{
  ///
  /// Print resource usage
  ///
  double userStart = timevalToDouble(fUsageStart.ru_utime);
  double sysStart  = timevalToDouble(fUsageStart.ru_stime);
  double userEnd   = timevalToDouble(fUsageEnd.ru_utime);
  double sysEnd    = timevalToDouble(fUsageEnd.ru_stime);

  double wallStart = std::chrono::duration<double>(fWallStart.time_since_epoch()).count();
  double wallEnd   = std::chrono::duration<double>(fWallEnd.time_since_epoch()).count();

  NLogInfo("Resource usage:");
  NLogInfo(" User time:    %.6f s", userEnd - userStart);
  NLogInfo(" System time:  %.6f s", sysEnd - sysStart);
  NLogInfo(" Wall time:    %.6f s", wallEnd - wallStart);

  // calculate effective CPU usage
  NLogInfo(" CPU usage:    %.2f %%", GetCpuUsage());

  NLogInfo(" Min RSS:      %ld KB", fUsageStart.ru_maxrss);
  NLogInfo(" Max RSS:      %ld KB", fUsageEnd.ru_maxrss);
  NLogInfo(" Diff RSS:     %ld KB", fUsageEnd.ru_maxrss - fUsageStart.ru_maxrss);
  // NLogInfo(" Minor faults: %ld", fUsageEnd.ru_minflt - fUsageStart.ru_minflt);
  // NLogInfo(" Major faults: %ld", fUsageEnd.ru_majflt - fUsageStart.ru_majflt);
}

THnSparse * NResourceMonitor::Initialize(THnSparse * hns, int nWorkers)
{
  ///
  /// Initialize resource monitor with THnSparse object
  ///
  std::vector<TAxis *> axes;
  int                  nThreads = nWorkers > 0 ? nWorkers : ROOT::GetThreadPoolSize();
  if (nThreads <= 0) nThreads = 1;

  NLogTrace("NResourceMonitor::Initialize: Initializing resource monitor for %d workers", nThreads);

  TAxis * workerAxis = new TAxis(nThreads, 0, nThreads);
  workerAxis->SetNameTitle("worker", "Worker");
  // set labels for worker axis
  for (int i = 0; i < nThreads; ++i) {
    workerAxis->SetBinLabel(i + 1, TString::Format("%d", i).Data());
  }
  axes.push_back(workerAxis);
  TAxis * aStat = NUtils::CreateAxisFromLabels("stat", "Stat", fNames);
  axes.push_back(aStat);

  if (fHnSparse) {
    NLogWarning("NResourceMonitor::Initialize: THnSparse is already initialized, overwriting ...");
    SafeDelete(fHnSparse);
  }

  // Build order so that the worker axis comes first, then the original binning
  // axes, and finally the stat axis.
  // New axes are appended after original hns dims in ReshapeSparseAxes, so:
  //   workerAxis index = hns->GetNdimensions()  (first new axis)
  //   statAxis   index = hns->GetNdimensions()+1 (second new axis)
  int              origDims = hns->GetNdimensions();
  std::vector<int> order;
  order.reserve(origDims + 2);
  order.push_back(origDims);            // worker first
  for (int i = 0; i < origDims; ++i) order.push_back(i); // original binning axes
  order.push_back(origDims + 1);        // stat last

  fHnSparse = NUtils::ReshapeSparseAxes(hns, order, axes);
  fHnSparse->SetNameTitle("resource_monitor", "Resource Monitor");

  return fHnSparse;
}

void NResourceMonitor::Fill(Int_t * coords, int threadId)
{
  ///
  /// Fill resource monitor with THnSparse object and coordinates
  ///
  ///
  auto statBinCoords = std::make_unique<Int_t[]>(fHnSparse->GetNdimensions());
  // Axis layout: [worker, binning_axes..., stat]
  // worker is axis 0; original binning axes follow; stat is the last axis.
  // Guard against TCP mode where workerIndex may be larger than the
  // configured worker axis. Clamp to the available worker bins so the
  // sparse fill stays in-range and remains meaningful.
  int workerBins = 1;
  if (fHnSparse && fHnSparse->GetAxis(0)) workerBins = fHnSparse->GetAxis(0)->GetNbins();
  int workerIndexClamped = static_cast<int>(threadId);
  if (workerIndexClamped < 0) workerIndexClamped = 0;
  if (workerIndexClamped >= workerBins) {
    NLogTrace("NResourceMonitor::Fill: workerId %d >= workerBins %d, clamping to last bin",
              workerIndexClamped, workerBins);
    workerIndexClamped = workerBins - 1;
  }
  statBinCoords[0] = workerIndexClamped + 1;
  // Log mapping for debugging: worker index and corresponding storage coords
  int origDims = fHnSparse->GetNdimensions() - 2;
  std::vector<int> coordsVec = NUtils::ArrayToVector(coords, origDims);
  // Compute the actual values we'll fill so we can log them for inspection
  double timeValLocal = GetTimeDiffInSeconds();
  if (!std::isfinite(timeValLocal) || timeValLocal <= 0.0) timeValLocal = 0.001;
  double cpuLocal = GetCpuUsage();
  if (!std::isfinite(cpuLocal) || cpuLocal <= 0.0) cpuLocal = 0.001;
  long diffRssLocal = static_cast<long>(fUsageEnd.ru_maxrss) - static_cast<long>(fUsageStart.ru_maxrss);
  double memLocal = (diffRssLocal <= 0) ? 0.001 : static_cast<double>(diffRssLocal);

  // NLogInfo("NResourceMonitor::Fill: worker=%d mappedWorkerBin=%d coords=%s time=%.6f cpu=%.3f mem=%.3f",
  //          workerIndexClamped, workerIndexClamped + 1, NUtils::GetCoordsString(coordsVec).c_str(),
  //          timeValLocal, cpuLocal, memLocal);
  for (Int_t i = 0; i < fHnSparse->GetNdimensions() - 2; ++i) {
    statBinCoords[i + 1] = coords[i];
  }
  Long64_t statBin;

  constexpr double kTinyError = std::numeric_limits<double>::min();
  // Small non-zero value used to ensure sparse bins remain allocated
  // when measured diffs are zero or non-finite.

  // Time diff (seconds)
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 1;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  fHnSparse->SetBinContent(statBin, timeValLocal);
  fHnSparse->SetBinError(statBin, kTinyError);

  // Set CPU usage — guard against NaN/Inf when wall time is near-zero
  // CPU usage (percentage)
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 2;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  fHnSparse->SetBinContent(statBin, cpuLocal);
  fHnSparse->SetBinError(statBin, kTinyError);

  // Set Memory usage — store absolute max RSS (KB) at end of processing so the
  // bin is always non-zero; ru_maxrss tracks peak RSS and the diff is often 0,
  // which would silently remove the sparse bin.
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 3;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  // Prefer storing the difference in RSS (end - start) so the monitor reflects
  // memory growth during the job rather than the absolute peak RSS which may
  // be large due to unrelated background allocations. Ensure non-negative.
  // When the difference is zero, store a tiny non-zero value so the sparse
  // bin is still allocated and visible in the monitor.
  // Use precomputed memLocal
  fHnSparse->SetBinContent(statBin, memLocal);
  fHnSparse->SetBinError(statBin, kTinyError);
    NLogTrace("NResourceMonitor::Fill: after Set mem binContent=%.6f", fHnSparse->GetBinContent(statBin));
}

void NResourceMonitor::Start()
{
  fWallStart = std::chrono::high_resolution_clock::now();
  // gather start resource usage
  if (getrusage(RUSAGE_SELF, &fUsageStart) == -1) {
    NLogError("NResourceMonitor::Start: getrusage failed at start");
  }
}

void NResourceMonitor::End()
{
  fWallEnd = std::chrono::high_resolution_clock::now();
  // gather resource usage after processing
  if (getrusage(RUSAGE_SELF, &fUsageEnd) == -1) {
    NLogError("NResourceMonitor::End: getrusage failed at end");
  }
}
double NResourceMonitor::GetTimeDiffInSeconds() const
{
  std::chrono::duration<double> diff = fWallEnd - fWallStart;
  return diff.count();
}

double NResourceMonitor::GetCpuUsage() const
{

  double userStart = timevalToDouble(fUsageStart.ru_utime);
  double sysStart  = timevalToDouble(fUsageStart.ru_stime);
  double userEnd   = timevalToDouble(fUsageEnd.ru_utime);
  double sysEnd    = timevalToDouble(fUsageEnd.ru_stime);

  double wallStart = std::chrono::duration<double>(fWallStart.time_since_epoch()).count();
  double wallEnd   = std::chrono::duration<double>(fWallEnd.time_since_epoch()).count();

  double usage = ((userEnd - userStart) + (sysEnd - sysStart)) / (wallEnd - wallStart) * 100.0;
  return usage;
}

} // namespace Ndmspc
