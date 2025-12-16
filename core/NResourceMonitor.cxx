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
void NResourceMonitor::Print(Option_t * option) const
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

THnSparse * NResourceMonitor::Initialize(THnSparse * hns)
{
  ///
  /// Initialize resource monitor with THnSparse object
  ///
  std::vector<TAxis *> axes;
  int                  nThreads = ROOT::GetThreadPoolSize();
  if (nThreads <= 0) nThreads = 1;

  NLogTrace("NResourceMonitor::Initialize: Initializing resource monitor for %d threads", nThreads);

  TAxis * threadAxis = new TAxis(nThreads, 0, nThreads);
  threadAxis->SetNameTitle("thread", "Thread");
  // set labels for thread axis
  for (int i = 0; i < nThreads; ++i) {
    threadAxis->SetBinLabel(i + 1, TString::Format("%d", i).Data());
  }
  axes.push_back(threadAxis);
  TAxis * aStat = NUtils::CreateAxisFromLabels("stat", "Stat", fNames);
  axes.push_back(aStat);

  if (fHnSparse) {
    NLogWarning("NResourceMonitor::Initialize: THnSparse is already initialized, overwriting ...");
    SafeDelete(fHnSparse);
  }

  fHnSparse = NUtils::ReshapeSparseAxes(hns, {}, axes);
  fHnSparse->SetNameTitle("resource_monitor", "Resource Monitor");

  return fHnSparse;
}

void NResourceMonitor::Fill(Int_t * coords, int threadId)
{
  ///
  /// Fill resource monitor with THnSparse object and coordinates
  ///
  ///
  Int_t statBinCoords[fHnSparse->GetNdimensions()];
  // set statBinCoords from statHnSparse
  for (Int_t i = 0; i < fHnSparse->GetNdimensions() - 2; ++i) {
    statBinCoords[i] = coords[i];
  }
  Long64_t statBin;

  // Set thread id coordinate
  statBinCoords[fHnSparse->GetNdimensions() - 2] = threadId + 1;

  statBinCoords[fHnSparse->GetNdimensions() - 1] = 1;
  statBin                                        = fHnSparse->GetBin(statBinCoords);
  fHnSparse->SetBinContent(statBin, GetTimeDiffInSeconds());

  // Set CPU usage
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 2;
  statBin                                        = fHnSparse->GetBin(statBinCoords);
  fHnSparse->SetBinContent(statBin, GetCpuUsage());

  // Set Memory usage
  statBinCoords[fHnSparse->GetNdimensions() - 1] = 3;
  statBin                                        = fHnSparse->GetBin(statBinCoords);
  fHnSparse->SetBinContent(statBin, GetMemoryUsageDiff());
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
