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
  statBinCoords[0] = threadId + 1;
  for (Int_t i = 0; i < fHnSparse->GetNdimensions() - 2; ++i) {
    statBinCoords[i + 1] = coords[i];
  }
  Long64_t statBin;

  constexpr double kTinyError = std::numeric_limits<double>::min();

  statBinCoords[fHnSparse->GetNdimensions() - 1] = 1;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  fHnSparse->SetBinContent(statBin, GetTimeDiffInSeconds());
  fHnSparse->SetBinError(statBin, kTinyError);

  // Set CPU usage — guard against NaN/Inf when wall time is near-zero
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 2;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  {
    double cpu = GetCpuUsage();
    if (!std::isfinite(cpu)) cpu = 0.0;
    fHnSparse->SetBinContent(statBin, cpu);
    fHnSparse->SetBinError(statBin, kTinyError);
  }

  // Set Memory usage — store absolute max RSS (KB) at end of processing so the
  // bin is always non-zero; ru_maxrss tracks peak RSS and the diff is often 0,
  // which would silently remove the sparse bin.
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 3;
  statBin                                        = fHnSparse->GetBin(statBinCoords.get());
  fHnSparse->SetBinContent(statBin, static_cast<double>(fUsageEnd.ru_maxrss));
  fHnSparse->SetBinError(statBin, kTinyError);
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
