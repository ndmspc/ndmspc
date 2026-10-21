#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE // flock() and friends
#endif
#include <cstddef>
#include <cmath>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#if defined(__linux__)
#include <malloc.h>
#endif
#include <cstdio>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <TSystem.h>
#include <TROOT.h>
#include <TPad.h>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <TF1.h>
#include <TFile.h>
#include <TGrid.h>
#include <TThread.h>
#include <TAxis.h>
#include <THnSparse.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <TString.h>
#include <algorithm>
#include <map>
#include <utility>
#include <fstream>
#include <TClass.h>
#include <TDirectory.h>
#include <TKey.h>
#include <TList.h>
#include <TNamed.h>
#include <TObject.h>
#include <TObjArray.h>
#include <TTree.h>
#include <TBranch.h>
#include <TH1.h>
#include <THnBase.h>
#include <TEfficiency.h>
#include <TStopwatch.h>
#include <TSystemDirectory.h>
#include <TSystemFile.h>
#include "ndmspc/core/NGnTree.h"
#include "ndmspc/core/NLogString.h"
#if defined(__linux__)
#include <fstream>
#elif defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif
#ifdef WITH_PARQUET
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/exception.h>
#endif
#include "ndmspc/ndmspc.h"
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(Ndmspc::NUtils);
/// \endcond

namespace Ndmspc {

// Progress bar throttle: per-callsite last print time
/**
 * @brief Mutex protecting progress throttle state and map.
 *
 * Guards access to `gLastProgressTime` which stores per-callsite timestamps
 * of the last printed progress update.
 */
static std::mutex gProgressThrottleMutex;

/**
 * @brief Map of callsite->last progress print time.
 */
static std::unordered_map<void *, std::chrono::high_resolution_clock::time_point> gLastProgressTime;

/**
 * @brief Minimum seconds between progress updates (configurable via NDMSPC_PROGRESS_THROTTLE_SEC).
 */
static double gProgressThrottleSeconds = []() {
  const char * env = gSystem->Getenv("NDMSPC_PROGRESS_THROTTLE_SEC");
  if (env) {
    try {
      return std::stod(std::string(env));
    }
    catch (...) {
      return 1.0;
    }
  }
  return 1.0; // default 1 second
}();

bool NUtils::EnableMT(Int_t numthreads)
{
  ///
  /// Enable multithreading
  ///
  ///
  bool previouslyEnabled = ROOT::IsImplicitMTEnabled();

  if (ROOT::IsImplicitMTEnabled()) {
    ROOT::DisableImplicitMT();
  }

  // TH1D * h = new TH1D("h", "Test Histogram", 20, -10, 10);
  // h->FillRandom("gaus", 1000);
  // TF1 * f1 = new TF1("f1", "gaus", 0, 10);
  // h->Fit(f1, "N");
  // delete h;

  if (numthreads == -1) {
    // take numeber of cores from env variable
    const char * nThreadsEnv = gSystem->Getenv("ROOT_MAX_THREADS");
    if (nThreadsEnv) {
      try {
        numthreads = std::stoul(nThreadsEnv);
      }
      catch (const std::exception & e) {
        NLogError("Error parsing ROOT_MAX_THREADS: %s !!! Setting it to '1' ...", e.what());
        numthreads = 1;
      }
    }
    else {
      numthreads = 1; // use default
    }
  }

  // Initialise ROOT's thread-safety infrastructure (gROOTMutex, etc.)
  ROOT::EnableThreadSafety();

  // Enable IMT with default number of threads (usually number of CPU cores)
  if (numthreads > 0) {
    ROOT::EnableImplicitMT(numthreads);
  }

  // Check if IMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {
    NLogInfo("ROOT::ImplicitMT is enabled with number of threads: %d", ROOT::GetThreadPoolSize());
  }

  return previouslyEnabled;
}

void NUtils::DisableObjectAutoRegistration()
{
  ///
  /// Disable automatic object registration in ROOT
  ///

#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 40, 0)
  ROOT::Experimental::DisableObjectAutoRegistration();
#else
  TH1::AddDirectory(kFALSE); // Disable ROOT auto directory management
#endif

}

bool NUtils::IsFileSupported(std::string filename)
{
  ///
  /// Check if file is supported
  ///

  if (filename.find("http://") == 0 || filename.find("https://") == 0 || filename.find("root://") == 0 ||
      filename.find("file://") == 0 || filename.find("alien://") == 0) {
    return true;
  }
  TString fn(filename.c_str());
  if (fn.BeginsWith("/") || !fn.Contains("://")) {
    return true;
  }
  NLogError("NUtils::IsFileSupported: File '%s' not found", filename.c_str());
  return false;
}

bool NUtils::AccessPathName(std::string path)
{
  ///
  /// Check if path exists
  ///
  TString pathStr(gSystem->ExpandPathName(path.c_str()));

  if (pathStr.BeginsWith("http://") || pathStr.BeginsWith("https://")) {
    // // TODO: check if URL exists via HTTP request
    // NHttpRequest request;
    // // request.SetUrl(pathStr.Data());
    // // return request.Exists();
    // int http_code = request.head(pathStr.Data());
    // if (http_code == 200) {
    //   return true;
    // }

    // return false;

    NLogWarning("NUtils::AccessPathName: Skipping actual access check for HTTP/HTTPS URL '%s' ...", pathStr.Data());
    return true; // For now, assume all HTTP/HTTPS URLs are accessible. Implement actual check if needed.
  }
  else if (pathStr.BeginsWith("file://") || pathStr.BeginsWith("/") || !pathStr.Contains("://")) {

    return gSystem->AccessPathName(pathStr.Data()) == false;
  }
  else if (pathStr.BeginsWith("root://") || pathStr.BeginsWith("alien://")) {
    // For root and alien protocols, we can try to open the file
    if (!pathStr.EndsWith(".root")) {
      // For raw files, we cannot use TFile
      pathStr += "?filetype=raw";
    }
    NLogDebug("NUtils::AccessPathName: Trying to open file '%s' ...", pathStr.Data());
    TFile * f = NUtils::OpenFile(pathStr.Data());
    if (f && !f->IsZombie()) {
      f->Close();
      return true;
    }
    return false;
  }
  return false;
}

bool NUtils::IsSplitBase(const std::string & path)
{
  ///
  /// Split output is always a local directory: a remote URL can never be one (and an HTTP URL
  /// would otherwise be "assumed accessible" by AccessPathName, a false positive here).
  ///
  if (path.empty() || path.find("://") != std::string::npos) return false;

  return gSystem->AccessPathName((path + "/manifest.json").c_str()) == false;
}

int NUtils::Cp(std::string source, std::string destination, Bool_t progressbar)
{
  ///
  /// Copy file
  ///
  int rc = 0;

  if (source.empty()) {
    NLogError("NUtils::Cp: Source file is empty");
    return -1;
  }
  if (destination.empty()) {
    NLogError("NUtils::Cp: Destination file is empty");
    return -1;
  }

  source      = gSystem->ExpandPathName(source.c_str());
  destination = gSystem->ExpandPathName(destination.c_str());

  if (IsFileSupported(source) == false) {
    NLogError("NUtils::Cp: Source file '%s' is not supported", source.c_str());
    return -1;
  }
  if (IsFileSupported(destination) == false) {
    NLogError("NUtils::Cp: Destination file '%s' is not supported", destination.c_str());
    return -1;
  }

  // For local destinations ensure parent directory exists before copy.
  TString destinationExpanded(gSystem->ExpandPathName(destination.c_str()));
  TString destinationNoQuery(destinationExpanded);
  Ssiz_t  queryPos = destinationNoQuery.First('?');
  if (queryPos != kNPOS) {
    destinationNoQuery.Remove(queryPos);
  }

  const bool isLocalDestination = destinationNoQuery.BeginsWith("file://") || destinationNoQuery.BeginsWith("/") ||
                                  !destinationNoQuery.Contains("://");
  if (isLocalDestination) {
    TString localPath(destinationNoQuery);
    if (localPath.BeginsWith("file://")) {
      localPath.ReplaceAll("file://", "");
    }

    TUrl        destinationUrl(localPath.Data());
    std::string destinationFilePath = destinationUrl.GetFile();
    std::string destinationDir      = gSystem->GetDirName(destinationFilePath.c_str()).Data();

    if (!destinationDir.empty() && destinationDir != "." && !CreateDirectory(destinationDir)) {
      // NLogError("NUtils::Cp: Cannot create destination directory '%s'", destinationDir.c_str());
      // return -1;
    }
  }

  NLogInfo("Copying file from '%s' to '%s' ...", source.c_str(), destination.c_str());
  rc = TFile::Cp(source.c_str(), destination.c_str(), progressbar);
  return rc;
}

TAxis * NUtils::CreateAxisFromLabels(const std::string & name, const std::string & title,
                                     const std::vector<std::string> & labels)
{
  ///
  /// Create label axis
  ///
  int     nBins = labels.size();
  TAxis * a     = new TAxis(nBins, 0, nBins);
  a->SetName(name.c_str());
  a->SetTitle(title.c_str());
  for (int i = 0; i < nBins; i++) {
    NLogTrace("NUtils::CreateAxisFromLabels: Adding label: %s", labels[i].c_str());
    a->SetBinLabel(i + 1, labels[i].c_str());
  }
  return a;
}

TAxis * NUtils::CreateAxisFromLabelsSet(const std::string & name, const std::string & title,
                                        const std::set<std::string> & labels)
{
  ///
  /// Create label axis
  ///
  int     nBins = labels.size();
  TAxis * a     = new TAxis(nBins, 0, nBins);
  a->SetName(name.c_str());
  a->SetTitle(title.c_str());
  int i = 1;
  for (const auto & label : labels) {
    NLogTrace("NUtils::CreateAxisFromLabels: Adding label: %s", label.c_str());
    a->SetBinLabel(i, label.c_str());
    i++;
  }
  return a;
}

THnSparse * NUtils::Convert(TH1 * h1, std::vector<std::string> names, std::vector<std::string> titles)
{
  ///
  /// Convert TH1 to THnSparse
  ///

  if (h1 == nullptr) {
    NLogError("TH1 h1 is null");
    return nullptr;
  }

  NLogInfo("Converting TH1 '%s' to THnSparse ...", h1->GetName());

  int nDims = 1;
  // Int_t    bins[nDims];
  // Double_t xmin[nDims];
  // Double_t xmax[nDims];
  auto bins = std::make_unique<Int_t[]>(nDims);
  auto xmin = std::make_unique<Double_t[]>(nDims);
  auto xmax = std::make_unique<Double_t[]>(nDims);

  TAxis * aIn = h1->GetXaxis();
  bins[0]     = aIn->GetNbins();
  xmin[0]     = aIn->GetXmin();
  xmax[0]     = aIn->GetXmax();

  THnSparse * hns = new THnSparseD(h1->GetName(), h1->GetTitle(), nDims, bins.get(), xmin.get(), xmax.get());

  // loop over all axes
  for (int i = 0; i < nDims; i++) {
    TAxis * a   = hns->GetAxis(i);
    TAxis * aIn = h1->GetXaxis();
    a->SetName(aIn->GetName());
    a->SetTitle(aIn->GetTitle());
    if (aIn->GetXbins()->GetSize() > 0) {
      // Double_t arr[aIn->GetNbins() + 1];
      auto arr = std::make_unique<Double_t[]>(aIn->GetNbins() + 1);
      arr[0]   = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr.get());
    }
  }

  for (int i = 0; i < nDims; i++) {
    if (!names[i].empty()) hns->GetAxis(i)->SetName(names[i].c_str());
    if (!titles[i].empty()) hns->GetAxis(i)->SetTitle(titles[i].c_str());
  }

  // fill the sparse with the content of the TH3
  for (Int_t i = 0; i <= h1->GetNbinsX() + 1; i++) {
    double content = h1->GetBinContent(i);
    Int_t  p[1]    = {i}; // bin indices in TH3
    hns->SetBinContent(p, content);
  }

  hns->SetEntries(h1->GetEntries());
  if (h1->GetSumw2N() > 0) {
    hns->Sumw2();
  }

  return hns;
}

THnSparse * NUtils::Convert(TH2 * h2, std::vector<std::string> names, std::vector<std::string> titles)
{
  ///
  /// Convert TH2 to THnSparse
  ///
  if (h2 == nullptr) {
    NLogError("TH2 h2 is null");
    return nullptr;
  }
  NLogInfo("Converting TH2 '%s' to THnSparse ...", h2->GetName());
  int  nDims = 2;
  auto bins  = std::make_unique<Int_t[]>(nDims);
  auto xmin  = std::make_unique<Double_t[]>(nDims);
  auto xmax  = std::make_unique<Double_t[]>(nDims);

  for (int i = 0; i < nDims; i++) {
    TAxis * aIn = nullptr;
    if (i == 0)
      aIn = h2->GetXaxis();
    else if (i == 1)
      aIn = h2->GetYaxis();
    else {
      NLogError("Invalid axis index %d", i);
      return nullptr;
    }
    bins[i] = aIn->GetNbins();
    xmin[i] = aIn->GetXmin();
    xmax[i] = aIn->GetXmax();
  }

  THnSparse * hns = new THnSparseD(h2->GetName(), h2->GetTitle(), nDims, bins.get(), xmin.get(), xmax.get());

  for (Int_t i = 0; i < nDims; i++) {
    TAxis * a   = hns->GetAxis(i);
    TAxis * aIn = nullptr;
    if (i == 0)
      aIn = h2->GetXaxis();
    else if (i == 1)
      aIn = h2->GetYaxis();
    else {
      NLogError("Invalid axis index %d", i);
      delete hns;
      return nullptr;
    }
    a->SetName(aIn->GetName());
    a->SetTitle(aIn->GetTitle());
    if (aIn->GetXbins()->GetSize() > 0) {
      auto arr = std::make_unique<Double_t[]>(aIn->GetNbins() + 1);
      arr[0]   = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr.get());
    }
  }

  for (Int_t i = 0; i < nDims; i++) {
    if (!names[i].empty()) hns->GetAxis(i)->SetName(names[i].c_str());
    if (!titles[i].empty()) hns->GetAxis(i)->SetTitle(titles[i].c_str());
  }

  // fill the sparse with the content of the TH2
  for (Int_t i = 0; i <= h2->GetNbinsX() + 1; i++) {
    for (Int_t j = 0; j <= h2->GetNbinsY() + 1; j++) {
      double content = h2->GetBinContent(i, j);
      Int_t  p[2]    = {i, j}; // bin indices in TH3
      hns->SetBinContent(p, content);
    }
  }

  hns->SetEntries(h2->GetEntries());
  if (h2->GetSumw2N() > 0) {
    hns->Sumw2();
  }

  return hns;
}

THnSparse * NUtils::Convert(TH3 * h3, std::vector<std::string> names, std::vector<std::string> titles)
{
  ///
  /// Convert TH3 to THnSparse
  ///

  if (h3 == nullptr) {
    NLogError("TH3 h3 is null");
    return nullptr;
  }

  NLogInfo("Converting TH3 '%s' to THnSparse ...", h3->GetName());

  int  nDims = 3;
  auto bins  = std::make_unique<Int_t[]>(nDims);
  auto xmin  = std::make_unique<Double_t[]>(nDims);
  auto xmax  = std::make_unique<Double_t[]>(nDims);

  for (int i = 0; i < nDims; i++) {
    TAxis * aIn = nullptr;
    if (i == 0)
      aIn = h3->GetXaxis();
    else if (i == 1)
      aIn = h3->GetYaxis();
    else if (i == 2)
      aIn = h3->GetZaxis();
    else {
      NLogError("Invalid axis index %d", i);
      return nullptr;
    }
    bins[i] = aIn->GetNbins();
    xmin[i] = aIn->GetXmin();
    xmax[i] = aIn->GetXmax();
  }

  THnSparse * hns = new THnSparseD(h3->GetName(), h3->GetTitle(), nDims, bins.get(), xmin.get(), xmax.get());

  // loop over all axes
  for (int i = 0; i < nDims; i++) {
    TAxis * a   = hns->GetAxis(i);
    TAxis * aIn = nullptr;
    if (i == 0)
      aIn = h3->GetXaxis();
    else if (i == 1)
      aIn = h3->GetYaxis();
    else if (i == 2)
      aIn = h3->GetZaxis();
    else {
      NLogError("Invalid axis index %d", i);
      delete hns;
      return nullptr;
    }
    a->SetName(aIn->GetName());
    a->SetTitle(aIn->GetTitle());
    if (aIn->GetXbins()->GetSize() > 0) {
      auto arr = std::make_unique<Double_t[]>(aIn->GetNbins() + 1);
      arr[0]   = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr.get());
    }
  }

  for (Int_t i = 0; i < nDims; i++) {
    if (!names[i].empty()) hns->GetAxis(i)->SetName(names[i].c_str());
    if (!titles[i].empty()) hns->GetAxis(i)->SetTitle(titles[i].c_str());
  }

  // fill the sparse with the content of the TH3
  for (Int_t i = 0; i <= h3->GetNbinsX() + 1; i++) {
    for (Int_t j = 0; j <= h3->GetNbinsY() + 1; j++) {
      for (Int_t k = 0; k <= h3->GetNbinsZ() + 1; k++) {
        double content = h3->GetBinContent(i, j, k);
        Int_t  p[3]    = {i, j, k}; // bin indices in TH3
        hns->SetBinContent(p, content);
      }
    }
  }

  hns->SetEntries(h3->GetEntries());
  if (h3->GetSumw2N() > 0) {
    hns->Sumw2();
  }

  return hns;
}

THnSparse * NUtils::ReshapeSparseAxes(THnSparse * hns, std::vector<int> order, std::vector<TAxis *> newAxes,
                                      std::vector<int> newPoint, Option_t * option)
{
  ///
  /// Reshape sparse axes
  ///

  TString opt(option);

  if (hns == nullptr) {
    NLogError("NUtils::ReshapeSparseAxes: THnSparse hns is null");
    return nullptr;
  }

  if (order.empty()) {
    NLogTrace("NUtils::ReshapeSparseAxes: Order vector is empty");
    for (long unsigned int i = 0; i < hns->GetNdimensions() + newAxes.size(); i++) {
      NLogTrace("NUtils::ReshapeSparseAxes: Adding axis %d to order", i);
      order.push_back(i);
    }
  }

  if (order.size() != hns->GetNdimensions() + newAxes.size()) {
    NLogError("NUtils::ReshapeSparseAxes: Invalid size %d [order] != %d [hns->GetNdimensions()+newAxes]", order.size(),
              hns->GetNdimensions() + newAxes.size());
    return nullptr;
  }

  if (newPoint.empty()) {
  }
  else {
    if (newAxes.size() != newPoint.size()) {
      NLogError("NUtils::ReshapeSparseAxes: Invalid size %d [newAxes] != %d [newPoint]", newAxes.size(),
                newPoint.size());
      return nullptr;
    }
  }
  // loop over order and check if order contains values from 0 to hns->GetNdimensions() + newAxes.size()
  for (size_t i = 0; i < order.size(); i++) {
    if (order[i] < 0 || order[i] >= hns->GetNdimensions() + (int)newAxes.size()) {
      NLogError("NUtils::ReshapeSparseAxes: Invalid order[%d]=%d. Value is negative or higher then "
                "'hns->GetNdimensions() + newAxes.size()' !!!",
                i, order[i]);
      return nullptr;
    }
  }

  // check if order contains unique values
  for (size_t i = 0; i < order.size(); i++) {
    for (size_t j = i + 1; j < order.size(); j++) {
      if (order[i] == order[j]) {
        NLogError("NUtils::ReshapeSparseAxes: Invalid order[%d]=%d and order[%d]=%d. Value is not unique !!!", i,
                  order[i], j, order[j]);
        return nullptr;
      }
    }
  }

  // print info about original THnSparse
  // NLogDebug("NUtils::ReshapeSparseAxes: Original THnSparse object:");
  // hns->Print();

  NLogTrace("NUtils::ReshapeSparseAxes: Reshaping sparse axes ...");

  int  nDims = hns->GetNdimensions() + newAxes.size();
  auto bins  = std::make_unique<Int_t[]>(nDims);
  auto xmin  = std::make_unique<Double_t[]>(nDims);
  auto xmax  = std::make_unique<Double_t[]>(nDims);
  /// loop over all axes
  int newAxesIndex = 0;
  for (int i = 0; i < nDims; i++) {
    TAxis * a  = nullptr;
    int     id = order[i];
    if (id < hns->GetNdimensions()) {
      a = hns->GetAxis(id);
      NLogTrace("NUtils::ReshapeSparseAxes: [ORIG] Axis [%d]->[%d]: %s %s %d %.2f %.2f", id, i, a->GetName(),
                a->GetTitle(), a->GetNbins(), a->GetXmin(), a->GetXmax());
    }
    else {
      newAxesIndex = id - hns->GetNdimensions();
      a            = newAxes[newAxesIndex];
      NLogTrace("NUtils::ReshapeSparseAxes: [NEW ] Axis [%d]->[%d]: %s %s %d %.2f %.2f", id, i, a->GetName(),
                a->GetTitle(), a->GetNbins(), a->GetXmin(), a->GetXmax());
    }
    bins[i] = a->GetNbins();
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax();
  }

  THnSparse * hnsNew = new THnSparseD(hns->GetName(), hns->GetTitle(), nDims, bins.get(), xmin.get(), xmax.get());

  // loop over all axes
  for (int i = 0; i < hnsNew->GetNdimensions(); i++) {
    TAxis * aIn = nullptr;
    if (order[i] < hns->GetNdimensions()) {
      aIn = hns->GetAxis(order[i]);
    }
    else {
      newAxesIndex = order[i] - hns->GetNdimensions();
      aIn          = newAxes[newAxesIndex];
    }

    TAxis * a = hnsNew->GetAxis(i);
    a->SetName(aIn->GetName());
    a->SetTitle(aIn->GetTitle());
    if (aIn->GetXbins()->GetSize() > 0) {
      auto arr = std::make_unique<Double_t[]>(aIn->GetNbins() + 1);
      arr[0]   = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr.get());
    }

    // copy bin labels
    if (aIn->IsAlphanumeric()) {
      for (int j = 1; j <= aIn->GetNbins(); j++) {
        const char * label = aIn->GetBinLabel(j);
        a->SetBinLabel(j, label);
      }
    }
  }

  if (newPoint.empty()) {
    NLogTrace("NUtils::ReshapeSparseAxes: New point is empty, filling is skipped and doing reset ...");
    // hnsNew->Reset();
    // hnsNew->SetEntries(0);
    return hnsNew;
  }

  if (hns->GetNbins() > 0) {
    // loop over all bins
    NLogTrace("NUtils::ReshapeSparseAxes: Filling all bins ...");
    for (Long64_t i = 0; i < hns->GetNbins(); i++) {
      auto p    = std::make_unique<Int_t[]>(nDims);
      auto pNew = std::make_unique<Int_t[]>(nDims);
      hns->GetBinContent(i, p.get());
      Double_t v = hns->GetBinContent(i);
      // remap p to pNew
      for (int j = 0; j < nDims; j++) {
        int id = order[j];
        if (id < hns->GetNdimensions()) {
          pNew[j] = p[id];
        }
        else {
          newAxesIndex = id - hns->GetNdimensions();
          pNew[j]      = newPoint[newAxesIndex];
        }
      }
      hnsNew->SetBinContent(pNew.get(), v);
    }
    hnsNew->SetEntries(hns->GetEntries());
  }
  // Calsculate sumw2
  if (opt.Contains("E")) {
    NLogTrace("ReshapeSparseAxes: Calculating sumw2 ...");
    hnsNew->Sumw2();
  }
  NLogTrace("ReshapeSparseAxes: Reshaped sparse axes:");
  // print all axes
  for (int i = 0; i < nDims; i++) {
    TAxis * a = hnsNew->GetAxis(i);
    NLogTrace("ReshapeSparseAxes: Axis %d: %s %s %d %.2f %.2f", i, a->GetName(), a->GetTitle(), a->GetNbins(),
              a->GetXmin(), a->GetXmax());
  }
  // hnsNew->Print("all");
  return hnsNew;
}

void NUtils::GetTrueHistogramMinMax(const TH1 * h, double & min_val, double & max_val, bool include_overflow_underflow)
{
  ///
  /// Retrieve the true min and max values of a histogram, ignoring underflow and overflow bins
  ///
  if (!h) {
    max_val = 0.0;
    min_val = 0.0;
    return;
  }

  max_val = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
  min_val = std::numeric_limits<double>::max();  // Initialize with largest possible double

  int first_bin_x = include_overflow_underflow ? 0 : 1;
  int last_bin_x  = include_overflow_underflow ? h->GetNbinsX() + 1 : h->GetNbinsX();

  int first_bin_y = include_overflow_underflow ? 0 : 1;
  int last_bin_y  = include_overflow_underflow ? h->GetNbinsY() + 1 : h->GetNbinsY();

  int first_bin_z = include_overflow_underflow ? 0 : 1;
  int last_bin_z  = include_overflow_underflow ? h->GetNbinsZ() + 1 : h->GetNbinsZ();

  // Determine the dimensionality of the histogram
  if (h->GetDimension() == 1) { // TH1
    for (int i = first_bin_x; i <= last_bin_x; ++i) {
      double content = h->GetBinContent(i);
      if (content > max_val) max_val = content;
      if (content < min_val) min_val = content;
    }
  }
  else if (h->GetDimension() == 2) { // TH2
    for (int i = first_bin_x; i <= last_bin_x; ++i) {
      for (int j = first_bin_y; j <= last_bin_y; ++j) {
        double content = h->GetBinContent(i, j);
        if (content > max_val) max_val = content;
        if (content < min_val) min_val = content;
      }
    }
  }
  else if (h->GetDimension() == 3) { // TH3
    for (int i = first_bin_x; i <= last_bin_x; ++i) {
      for (int j = first_bin_y; j <= last_bin_y; ++j) {
        for (int k = first_bin_z; k <= last_bin_z; ++k) {
          double content = h->GetBinContent(i, j, k);
          if (content > max_val) max_val = content;
          if (content < min_val) min_val = content;
        }
      }
    }
  }
  else {
    NLogWarning("GetTrueHistogramMinMax: Histogram '%s' has unsupported dimension %d. "
                "Using GetMaximum/GetMinimum as fallback.",
                h->GetName(), h->GetDimension());
    // As a fallback, try to get from GetMaximum/GetMinimum if dimension not 1,2,3
    max_val = h->GetMaximum();
    min_val = h->GetMinimum();
  }

  // Handle the case where all bins might be empty or zero
  if (max_val == -std::numeric_limits<double>::max() && min_val == std::numeric_limits<double>::max()) {
    max_val = 0.0; // If no content was found, assume 0
    min_val = 0.0;
  }
}

bool NUtils::CreateDirectory(const std::string & path)
{
  ///
  /// Create a directory (and all parents) for local paths.
  /// Remote paths (containing "://", except "file://") are silently ignored
  /// as the remote protocol is expected to handle directory creation itself.
  ///

  if (path.empty()) return false;

  TString dir(path.c_str());
  bool    isLocalFile = dir.BeginsWith("file://");
  if (isLocalFile) {
    dir.ReplaceAll("file://", "");
  }
  else {
    isLocalFile = !dir.Contains("://");
  }

  if (!isLocalFile) return true; // remote path — nothing to do locally

  std::string pwd = gSystem->pwd();
  if (dir[0] != '/') dir = (pwd + "/" + std::string(dir.Data())).c_str();
  dir.ReplaceAll("?remote=1&", "?");
  dir.ReplaceAll("?remote=1", "");
  dir.ReplaceAll("&remote=1", "");
  TUrl url(dir.Data());

  const std::string localDir = url.GetFile();
  return gSystem->mkdir(localDir.c_str(), kTRUE) == 0;
}

bool NUtils::ParseBoolEnv(const char * value)
{
  if (!value) return false;
  std::string v(value);
  for (char & c : v) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return (v == "1" || v == "true" || v == "yes" || v == "on");
}

// alien:// needs a grid connection before TFile::Open; reuse an existing gGrid.
static bool NUtilsConnectAlien(const std::string & path)
{
  if (!TString(path.c_str()).BeginsWith("alien://")) return true;
  if (gGrid != nullptr) return true;

  if (TGrid::Connect("alien://") == nullptr) {
    NLogError("NUtils: cannot connect to the ALICE grid (alien://) for '%s'", path.c_str());
    return false;
  }
  return true;
}

TFile * NUtils::OpenFile(std::string filename, std::string mode, bool createLocalDir)
{
  ///
  /// Open root file and create directory when needed in local case
  ///

  filename = gSystem->ExpandPathName(filename.c_str());
  if (!NUtilsConnectAlien(filename)) return nullptr;

  if (createLocalDir) {
    if (!mode.compare("RECREATE") || !mode.compare("UPDATE") || !mode.compare("WRITE")) {
      const std::string dir = gSystem->GetDirName(filename.c_str()).Data();
      CreateDirectory(dir);
    }
  }
  return TFile::Open(filename.c_str(), mode.c_str());
}

std::string NUtils::OpenRawFile(std::string filename, Int_t chunkSize)
{
  ///
  /// Opens raw file
  ///

  if (!NUtilsConnectAlien(filename)) return "";

  std::string content;
  TFile *     f = OpenFile(TString::Format("%s?filetype=raw", filename.c_str()).Data());
  if (!f || f->IsZombie()) {
    if (f) {
      f->Close();
    }
    return "";
  }

  const Long64_t fileSize = f->GetSize();
  if (fileSize <= 0) {
    f->Close();
    return "";
  }

  content.reserve(static_cast<size_t>(fileSize));

  if (chunkSize <= 0) {
    NLogWarning("NUtils::OpenRawFile: Invalid chunkSize=%d for '%s', using default 4096", chunkSize, filename.c_str());
    chunkSize = 4096;
  }

  auto buff = std::make_unique<char[]>(chunkSize);

  Long64_t offset = 0;
  while (offset < fileSize) {
    const Int_t toRead = (offset + chunkSize <= fileSize) ? chunkSize : static_cast<Int_t>(fileSize - offset);

    if (f->ReadBuffer(buff.get(), offset, toRead) != 0) {
      NLogError("NUtils::OpenRawFile: ReadBuffer failed at offset=%lld for '%s'", (long long)offset, filename.c_str());
      f->Close();
      return "";
    }

    content.append(buff.get(), toRead);
    offset += toRead;
  }

  f->Close();
  return content;
}
bool NUtils::SaveRawFile(std::string filename, std::string content)
{
  ///
  /// Save raw file
  ///

  TFile * f = OpenFile(TString::Format("%s?filetype=raw", filename.c_str()).Data(), "RECREATE");
  if (!f) {
    NLogError("Error: Problem opening file '%s' in 'rw' mode  ...", filename.c_str());
    return false;
  }
  f->WriteBuffer(content.c_str(), content.size());
  f->Close();
  return true;
}

std::vector<std::string> NUtils::ResolveMacroList(std::string_view list, const std::string & defaultDir)
{
  std::vector<std::string> resolved;
  // The directory a bare name takes: the one the entry before it named, else the caller's macro
  // directory - but only for a name nothing local answers to (see below).
  std::string dir = defaultDir;

  for (const auto & entry : Tokenize(list, ',')) {
    if (entry.empty()) continue;

    // A name that names no directory of its own is the file of that name where the caller stands,
    // when there is one: a macro named on the command line (`ndmspc-run sum.C`) is run from the
    // current directory, and the tool directory is only the fallback for a name there is nothing
    // local for. One that names a directory (a path, or a URL) is used as it is, and becomes the
    // directory the next name takes.
    if (entry.find('/') == std::string::npos) {
      if (NUtils::AccessPathName(entry)) {
        resolved.push_back(entry);
        continue;
      }
      resolved.push_back(dir.empty() ? entry : dir + "/" + entry);
      continue;
    }
    resolved.push_back(entry);
    const char * parent = gSystem->DirName(entry.c_str());
    if (parent != nullptr && *parent != '\0') dir = parent;
  }
  return resolved;
}

TMacro * NUtils::OpenMacro(std::string filename)
{
  ///
  /// Open macro using ROOT plugin system via OpenRawFile
  ///

  // Expand env variables (e.g. ${NDMSPC_HOME}) before forwarding to OpenRawFile.
  TString expanded(filename.c_str());
  gSystem->ExpandPathName(expanded);
  filename = expanded.Data();

  std::string content = OpenRawFile(filename);
  if (content.empty()) {
    NLogError("Error: Problem opening macro '%s' ...", filename.c_str());
    return nullptr;
  }

  NLogInfo("Opening macro '%s' ...", filename.c_str());
  TUrl        url(filename.c_str());
  std::string basefilename = gSystem->BaseName(url.GetFile());
  if (basefilename.empty()) {
    basefilename = gSystem->BaseName(filename.c_str());
  }
  size_t dotPos = basefilename.find_last_of('.');
  if (dotPos != std::string::npos) {
    basefilename.erase(dotPos);
  }
  if (basefilename.empty()) {
    basefilename = "macro";
  }
  TMacro * m = new TMacro();
  m->SetName(basefilename.c_str());
  m->AddLine(content.c_str());
  return m;
}

bool NUtils::LoadJsonFile(json & cfg, std::string filename)
{
  ///
  /// Load JSON file
  ///

  std::string content = OpenRawFile(filename);
  if (content.empty()) {
    NLogError("NUtils::LoadJsonFile: Problem opening JSON file '%s' ...", filename.c_str());
    return false;
  }

  try {
    json myCfg = json::parse(content.c_str());
    cfg.merge_patch(myCfg);
    NLogInfo("NUtils::LoadJsonFile: Successfully parsed JSON file '%s' ...", filename.c_str());
  }
  catch (json::parse_error & e) {
    NLogError("NUtils::LoadJsonFile: JSON parse error in file '%s' at byte %d: %s", filename.c_str(), e.byte, e.what());
    return false;
  }

  return true;
}

bool NUtils::LoadJsonFileRelative(json & cfg, const std::string & filename, const std::string & baseDir)
{
  ///
  /// Load JSON file, resolving a relative path against baseDir
  ///

  if (filename.empty()) return false;

  std::string path = gSystem->ExpandPathName(filename.c_str());
  if (!AccessPathName(path) && !baseDir.empty()) {
    std::string alt = baseDir + "/" + path;
    if (AccessPathName(alt)) path = alt;
  }

  return LoadJsonFile(cfg, path);
}

std::string NUtils::InjectRawJson(json & j, const RawJsonInjections & injections)
{
  const std::string kPlaceholderBase = "##RAW_JSON_INJECT_";

  // Set all placeholders with unique index suffixes
  for (size_t i = 0; i < injections.size(); ++i) {
    const auto & keys = injections[i].first;
    if (keys.empty()) {
      throw std::invalid_argument("Keys array must not be empty at injection index " + std::to_string(i));
    }

    json * current = &j;
    for (size_t k = 0; k < keys.size() - 1; ++k) {
      if (!current->contains(keys[k])) {
        (*current)[keys[k]] = json::object();
      }
      current = &(*current)[keys[k]];
    }
    (*current)[keys.back()] = kPlaceholderBase + std::to_string(i) + "##";
  }

  std::string result = j.dump();

  // Replace each placeholder with the corresponding raw JSON.
  // Replace all occurrences defensively in case the placeholder appears more than once.
  for (size_t i = 0; i < injections.size(); ++i) {
    const std::string quotedPlaceholder = "\"" + kPlaceholderBase + std::to_string(i) + "##\"";

    size_t pos = result.find(quotedPlaceholder);
    if (pos == std::string::npos) {
      throw std::runtime_error("Placeholder not found for key path ending in \"" + injections[i].first.back() + "\"");
    }

    while (pos != std::string::npos) {
      result.replace(pos, quotedPlaceholder.length(), injections[i].second);
      pos = result.find(quotedPlaceholder, pos + injections[i].second.size());
    }
  }

  return result;
}

void NUtils::AddRawJsonInjection(json & j, const std::vector<std::string> & path, const std::string & rawJson,
                                 const std::string & injectionsKey)
{
  if (path.empty()) {
    throw std::invalid_argument("AddRawJsonInjection: path must not be empty");
  }

  // Walk to the parent of the target key and capture any existing object members so
  // they are preserved in the final injected string (merged at string level).
  json * current       = &j;
  bool   pathReachable = true;
  for (size_t k = 0; k + 1 < path.size(); ++k) {
    if (!current->is_object() || !current->contains(path[k])) {
      pathReachable = false;
      break;
    }
    current = &(*current)[path[k]];
  }

  std::string valueToStore = rawJson;
  if (pathReachable && current->is_object() && current->contains(path.back())) {
    const json & existing = (*current)[path.back()];
    if (existing.is_object() && !existing.empty()) {
      // Append existing key/value pairs into the raw JSON object string.
      // Works only when rawJson is itself a JSON object (starts with '{').
      size_t lastBrace = valueToStore.rfind('}');
      if (lastBrace != std::string::npos) {
        std::string extras      = existing.dump();                     // e.g. {"key":"val"}
        std::string extraFields = extras.substr(1, extras.size() - 2); // strip outer { }
        if (!extraFields.empty()) {
          valueToStore = valueToStore.substr(0, lastBrace) + "," + extraFields + "}";
        }
      }
    }
  }

  if (!j.contains(injectionsKey) || !j[injectionsKey].is_array()) {
    j[injectionsKey] = json::array();
  }

  j[injectionsKey].push_back({{"path", path}, {"value", valueToStore}});
}

bool NUtils::CollectRawJsonInjections(const json & j, RawJsonInjections & injections, const std::string & injectionsKey)
{
  injections.clear();
  if (!j.contains(injectionsKey) || !j[injectionsKey].is_array()) {
    return false;
  }

  for (const auto & entry : j[injectionsKey]) {
    if (!entry.contains("path") || !entry["path"].is_array() || !entry.contains("value") ||
        !entry["value"].is_string()) {
      continue;
    }
    injections.emplace_back(entry["path"].get<std::vector<std::string>>(), entry["value"].get<std::string>());
  }

  return !injections.empty();
}

std::string NUtils::MergeRawJsonWithMetadata(const std::string & rawJson, const json & metadata)
{
  ///
  /// Merge raw JSON string with metadata fields
  ///

  try {
    json obj = json::parse(rawJson);

    // Merge metadata fields into the parsed object
    for (const auto & [key, val] : metadata.items()) {
      obj[key] = val;
    }

    return obj.dump();
  }
  catch (const std::exception & e) {
    NLogError("NUtils::MergeRawJsonWithMetadata: Failed to parse raw JSON: %s", e.what());
    return rawJson; // Return original if parsing fails
  }
}

std::vector<std::string> NUtils::Find(std::string path, std::string filename)
{
  ///
  /// Find files in path
  ///

  std::vector<std::string> files;
  TString                  pathStr = gSystem->ExpandPathName(path.c_str());
  if (pathStr.IsNull() || filename.empty()) {
    NLogError("NUtils::Find: Path or filename is empty");
    return files;
  }

  if (pathStr.BeginsWith("root://")) {
    return FindEos(path, filename);
  }
  else {
    return FindLocal(path, filename);
  }

  return files;
}

std::vector<std::string> NUtils::FindLocal(std::string path, std::string filename)
{
  ///
  /// Find local files
  ///

  std::vector<std::string> files;
  if (TString(path.c_str()).Contains("://")) {
    NLogError("NUtils::FindLocal: Path '%s' is not a local path", path.c_str());
    return files;
  }
  if (!NUtils::AccessPathName(path)) {
    NLogError("NUtils::FindLocal: Path '%s' does not exist", path.c_str());
    return files;
  }
  NLogInfo("Doing find %s -name %s", path.c_str(), filename.c_str());
  std::string linesMerge =
      gSystem->GetFromPipe(TString::Format("find %s -name %s", path.c_str(), filename.c_str())).Data();

  std::stringstream check2(linesMerge);
  std::string       line;
  while (std::getline(check2, line)) {
    files.push_back(line);
  }
  return files;
}
std::vector<std::string> NUtils::FindEos(std::string path, std::string filename)
{
  ///
  /// Find eos files
  ///

  std::vector<std::string> files;
  NLogInfo("Doing eos find -f --name %s %s ", filename.c_str(), path.c_str());

  TUrl        url(path.c_str());
  std::string host      = url.GetHost();
  std::string directory = url.GetFile();
  std::string findUrl   = "root://";
  findUrl += host + "//proc/user/";
  findUrl += "?mgm.cmd=find&mgm.find.match=" + filename;
  findUrl += "&mgm.path=" + directory;
  findUrl += "&mgm.format=json&mgm.option=f&filetype=raw";
  NLogInfo("Doing TFile::Open on '%s' ...", findUrl.c_str());

  TFile * f = Ndmspc::NUtils::OpenFile(findUrl.c_str());
  if (!f) return files;

  // Printf("%lld", f->GetSize());

  int buffsize = 4096;
  // FIXME: use smart pointer to avoid large stack allocation (check if working)
  auto buff = std::make_unique<char[]>(buffsize + 1);
  // char buff[buffsize + 1];

  Long64_t    buffread = 0;
  std::string content;
  while (buffread < f->GetSize()) {

    if (buffread + buffsize > f->GetSize()) buffsize = f->GetSize() - buffread;

    // Printf("Buff %lld %d", buffread, buffsize);
    f->ReadBuffer(buff.get(), buffread, buffsize);
    buff[buffsize] = '\0';
    content += buff.get();
    buffread += buffsize;
  }

  f->Close();

  std::string ss  = "mgm.proc.stdout=";
  size_t      pos = ss.size() + 1;
  content         = content.substr(pos);

  // stringstream class check1
  std::stringstream check1(content);

  std::string intermediate;

  // Tokenizing w.r.t. space '&'
  std::vector<std::string> tokens;
  while (getline(check1, intermediate, '&')) {
    tokens.push_back(intermediate);
  }
  std::string linesString = tokens[0];
  for (auto & line : NUtils::Tokenize(linesString, '\n')) {
    files.push_back("root://" + host + "/" + line);
  }
  return files;
}

std::vector<std::string> NUtils::Tokenize(std::string_view input, const char delim)
{
  ///
  /// Tokenize helper function
  ///
  std::vector<std::string> out;
  size_t                   start = 0;
  size_t                   end   = input.find(delim);

  while (end != std::string_view::npos) {
    if (end > start) {
      out.emplace_back(input.substr(start, end - start));
    }
    start = end + 1;
    end   = input.find(delim, start);
  }

  if (start < input.length()) {
    out.emplace_back(input.substr(start));
  }
  return out;
}
std::vector<int> NUtils::TokenizeInt(std::string_view input, const char delim)
{
  ///
  /// Tokenize helper function
  ///

  std::vector<int>         out;
  std::vector<std::string> tokens = Tokenize(input, delim);
  for (auto & t : tokens) {
    if (t.empty()) continue;
    out.push_back(std::stoi(t));
  }

  return out;
}

std::string NUtils::Join(const std::vector<std::string> & values, const char delim)
{
  ///
  /// Join helper function
  ///

  std::string out;
  for (const auto & v : values) {
    if (!out.empty()) out += delim;
    out += v;
  }
  return out;
}
std::string NUtils::Join(const std::vector<int> & values, const char delim)
{
  ///
  /// Join helper function
  ///

  std::string out;
  for (const auto & v : values) {
    if (!out.empty()) out += delim;
    out += std::to_string(v);
  }
  return out;
}

std::vector<std::string> NUtils::Truncate(std::vector<std::string> values, std::string value)
{
  ///
  /// Truncate helper function
  ///

  std::vector<std::string> out;
  for (auto & v : values) {
    v = std::string(v.begin() + value.size(), v.end());
    out.push_back(v);
  }
  return out;
}

std::set<std::string> NUtils::Unique(std::vector<std::string> & paths, int axis, std::string path, char token)
{
  ///
  /// Unique helper function
  ///

  std::set<std::string>    out;
  std::vector<std::string> truncatedPaths = NUtils::Truncate(paths, path);
  for (auto & p : truncatedPaths) {
    std::vector<std::string> tokens = Tokenize(p, token);
    out.insert(tokens[axis]);
  }
  return out;
}

TH1 * NUtils::ProjectTHnSparse(THnSparse * sparse, const std::vector<int> & axes, Option_t * option)
{
  ///
  /// Project THnSparse onto TH1
  ///
  if (sparse == nullptr) {
    NLogError("Error: Sparse is nullptr ...");
    return nullptr;
  }

  TH1 * h = nullptr;
  if (axes.size() == 1) {
    h = sparse->Projection(axes[0], option);
  }
  else if (axes.size() == 2) {
    h = sparse->Projection(axes[1], axes[0], option);
  }
  else if (axes.size() == 3) {
    h = sparse->Projection(axes[0], axes[1], axes[2], option);
  }
  else {
    NLogError("Error: Only projection onto single axis is supported for TH1 ...");
  }

  h->SetName(TString::Format("%s_proj", sparse->GetName()).Data());
  h->SetTitle(TString::Format("%s Projection", sparse->GetTitle()).Data());
  // Detach from gDirectory to prevent TFile from claiming ownership of the histogram.
  // Without this, if gDirectory is an open TFile, the TFile will delete this histogram
  // when closed, causing a double-free when THStack/canvas tries to clean it up later.
  h->SetDirectory(nullptr);

  // Set labels for axis
  for (size_t i = 0; i < axes.size(); i++) {
    TAxis * axisSparse = sparse->GetAxis(axes[i]);
    TAxis * axisHist   = h->GetXaxis();
    if (i == 1) axisHist = h->GetYaxis();
    if (i == 2) axisHist = h->GetZaxis();

    axisHist->SetName(axisSparse->GetName());
    axisHist->SetTitle(axisSparse->GetTitle());
    // ROOT's Projection carries neither name nor title onto the histogram, and an axis declared without
    // a title (a bare `Age` axis, say) would leave the projection's axis unlabelled: fall back on the
    // axis name, so a projection always reads.
    const char * title = axisHist->GetTitle();
    if (title == nullptr || *title == '\0') axisHist->SetTitle(axisSparse->GetName());

    // Copy bin labels if alphanumeric
    if (axisSparse->IsAlphanumeric()) {
      for (int j = 1; j <= axisSparse->GetNbins(); j++) {
        const char * label = axisSparse->GetBinLabel(j);
        axisHist->SetBinLabel(j, label);
      }
    }
  }

  return h;
}

bool NUtils::SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges, bool withOverflow,
                           bool modifyTitle, bool reset)
{
  ///
  /// Set axis ranges
  ///
  ///

  if (sparse == nullptr) {
    NLogError("Error: Sparse is nullptr ...");
    return false;
  }
  if (sparse->GetNdimensions() == 0) return true;

  if (reset) {
    NLogTrace("Setting axis ranges on '%s' THnSparse ...", sparse->GetName());
    /// Reset all axis ranges
    for (int i = 0; i < sparse->GetNdimensions(); i++) {
      if (withOverflow) {
        NLogTrace("Resetting '%s' axis ...", sparse->GetAxis(i)->GetName());
        sparse->GetAxis(i)->SetRange(0, 0);
      }
      else {
        NLogTrace("Resetting '%s' axis [%d,%d] ...", sparse->GetAxis(i)->GetName(), 1, sparse->GetAxis(i)->GetNbins());
        sparse->GetAxis(i)->SetRange(1, sparse->GetAxis(i)->GetNbins());
      }
    }
  }

  if (ranges.empty()) {
    NLogTrace("No axis ranges to set ...");
    return true;
  }

  TAxis * axis  = nullptr;
  TString title = sparse->GetTitle();
  if (modifyTitle) title += " Ranges:";
  for (size_t i = 0; i < ranges.size(); i++) {
    axis = sparse->GetAxis(ranges[i][0]);
    NLogTrace("Setting axis range %s=[%d,%d] ...", axis->GetName(), ranges[i][1], ranges[i][2]);
    if (ranges[i].size() != 3) {
      NLogError("Error: Axis range must have 3 values, but has %zu ...", ranges[i].size());
      return false;
    }
    axis->SetRange(ranges[i][1], ranges[i][2]);
    if (axis->IsAlphanumeric()) {

      title += TString::Format(" %s[%s]", axis->GetName(), axis->GetBinLabel(ranges[i][1]));
    }
    else {
      title += TString::Format(" %s[%0.2f - %0.2f]", axis->GetName(), axis->GetBinLowEdge(ranges[i][1]),
                               axis->GetBinUpEdge(ranges[i][2]));
    }
  }
  if (modifyTitle) sparse->SetTitle(title.Data());
  return true;
}

bool NUtils::SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow,
                           bool modifyTitle, bool reset)
{
  ///
  /// Set axis ranges
  ///
  ///

  if (sparse == nullptr) {
    NLogError("NUtils::SetAxisRanges: Sparse is nullptr ...");
    return false;
  }
  if (sparse->GetNdimensions() == 0) return true;

  NLogTrace("NUtils::SetAxisRanges: Setting axis ranges on '%s' THnSparse ...", sparse->GetName());
  if (reset) {
    /// Reset all axis ranges
    for (int i = 0; i < sparse->GetNdimensions(); i++) {
      if (withOverflow) {
        NLogTrace("NUtils::SetAxisRanges: Resetting '%s' axis ...", sparse->GetAxis(i)->GetName());
        sparse->GetAxis(i)->SetRange(0, 0);
      }
      else {
        NLogTrace("NUtils::SetAxisRanges: Resetting '%s' axis [%d,%d] ...", sparse->GetAxis(i)->GetName(), 1,
                  sparse->GetAxis(i)->GetNbins());
        sparse->GetAxis(i)->SetRange(1, sparse->GetAxis(i)->GetNbins());
      }
    }
  }

  if (ranges.empty()) {
    NLogTrace("NUtils::SetAxisRanges: No axis ranges to set ...");
    return true;
  }
  TAxis * axis  = nullptr;
  TString title = sparse->GetTitle();
  for (const auto & [key, val] : ranges) {
    NLogTrace("NUtils::SetAxisRanges: Setting axis range for axis %d to [%d,%d] ...", key, val[0], val[1]);
    axis = sparse->GetAxis(key);
    if (axis == nullptr) {
      NLogError("NUtils::SetAxisRanges: Axis %d is nullptr ...", key);
      return false;
    }
    NLogTrace("NUtils::SetAxisRanges: Setting axis range %s=[%d,%d] ...", axis->GetName(), val[0], val[1]);
    axis->SetRange(val[0], val[1]);
    if (axis->IsAlphanumeric()) {

      title += TString::Format(" %s[%s]", axis->GetName(), axis->GetBinLabel(val[0]));
    }
    else {
      title += TString::Format(" %s[%0.2f - %0.2f]", axis->GetName(), axis->GetBinLowEdge(val[0]),
                               axis->GetBinUpEdge(val[1]));
    }
  }

  if (modifyTitle) sparse->SetTitle(title.Data());
  NLogTrace("NUtils::SetAxisRanges: New title: %s", sparse->GetTitle());

  return true;
}
bool NUtils::GetAxisRangeInBase(TAxis * a, int rebin, int rebin_start, int bin, int & min, int & max)
{
  ///
  /// Returns axis range in base in min and max variables
  ///
  if (a == nullptr) {
    NLogError("Error: Axis is nullptr ...");
    return false;
  }
  min = -1;
  max = -1;

  NLogTrace("Getting axis range in base for '%s' rebin=%d rebin_start=%d bin=%d...", a->GetName(), rebin, rebin_start,
            bin);

  min = rebin * (bin - 1) + rebin_start;
  max = min + rebin - 1;
  NLogTrace("Axis '%s' min=%d max=%d", a->GetName(), min, max);

  if (min < 1) {
    NLogError("Error: Axis '%s' min=%d is lower then 1 ...", a->GetName(), min);
    min = -1;
    max = -1;
    return false;
  }

  if (max > a->GetNbins()) {
    NLogError("Error: Axis '%s' max=%d is higher then %d ...", a->GetName(), max, a->GetNbins());
    min = -1;
    max = -1;
    return false;
  }

  return true;
}
bool NUtils::GetAxisRangeInBase(TAxis * a, int min, int max, TAxis * base, int & minBase, int & maxBase)
{
  ///
  /// Gets axis range in base
  ///
  ///
  int rebin = base->GetNbins() / a->GetNbins();

  // TODO: Improve handling of rebin_start correctly (depending on axis min and max of first bin)
  int rebin_start = (base->GetNbins() % a->GetNbins()) + 1;
  rebin_start     = rebin != 1 ? rebin_start : 1; // start from 1

  NLogTrace("Getting axis range in base for '%s' min=%d max=%d rebin=%d rebin_start=%d...", a->GetName(), min, max,
            rebin, rebin_start);

  int tmp;
  GetAxisRangeInBase(base, rebin, rebin_start, min, minBase, tmp);
  GetAxisRangeInBase(base, rebin, rebin_start, max, tmp, maxBase);
  NLogTrace("Axis '%s' minBase=%d maxBase=%d", a->GetName(), minBase, maxBase);

  return true;
}

TObjArray * NUtils::AxesFromDirectory(const std::vector<std::string> paths, const std::string & findPath,
                                      const std::string & fileName, const std::vector<std::string> & axesNames,
                                      const std::map<std::string, std::vector<std::string>> & filterAxes)
{
  if (paths.empty()) {
    NLogError("Error: No paths provided ...");
    return nullptr;
  }

  std::map<std::string, std::set<std::string>> axes;
  for (const auto & path : paths) {
    NLogInfo("Found file: %s", path.c_str());
    // remove prefix basePath from path
    TString relativePath = path;
    relativePath.ReplaceAll(findPath.c_str(), "");
    relativePath.ReplaceAll(fileName.c_str(), "");
    // relativePath.ReplaceAll("years", "");
    // relativePath.ReplaceAll("data", "");
    relativePath.ReplaceAll("//", "/");
    // remove leading slash
    relativePath.Remove(0, relativePath.BeginsWith("/") ? 1 : 0);
    // remove trailing slash
    relativePath.Remove(relativePath.EndsWith("/") ? relativePath.Length() - 1 : relativePath.Length(), 1);

    std::vector<std::string> tokens = Ndmspc::NUtils::Tokenize(relativePath.Data(), '/');

    // if (tokens.size() < axesNames.size()) {
    //   tokens.push_back("mb");
    // }
    //
    if (tokens.size() != axesNames.size()) {
      continue;
    }

    for (size_t i = 0; i < tokens.size(); ++i) {
      axes[axesNames[i]].insert(tokens[i]);
    }
  }

  std::map<std::string, std::vector<std::string>> axesVec;
  TObjArray * axesArr = new TObjArray();
  for (const auto & axisName : axesNames) {

    if (filterAxes.contains(axisName) && !filterAxes.at(axisName).empty()) {
      NLogTrace("Filtering axis '%s' with %zu values ...", axisName.c_str(), filterAxes.at(axisName).size());
      axesVec[axisName].clear();
      for (const auto & v : filterAxes.at(axisName)) {
        axesVec[axisName].push_back(v);
      }
    } else {
      axesVec.emplace(axisName, std::vector<std::string>(axes[axisName].begin(), axes[axisName].end()));
    }

    // print axis values
    for (const auto & v : axesVec[axisName]) {
      NLogTrace("Axis '%s' value: %s", axisName.c_str(), v.c_str());
    }

    TAxis * axis = Ndmspc::NUtils::CreateAxisFromLabels(axisName, axisName, axesVec[axisName]); // Convert set to vector
    axesArr->Add(axis);
  }

  return axesArr;
}
std::string NUtils::GetJsonString(json j)
{
  ///
  /// Returns json string if it is valid
  ///

  if (j.is_string()) {
    return j.get<std::string>();
  }
  else if (j.is_number_integer()) {
    return std::to_string(j.get<int>());
  }
  else if (j.is_number_float()) {
    return std::to_string(j.get<double>());
  }
  else if (j.is_boolean()) {
    return j.get<bool>() ? "true" : "false";
  }
  else if (j.is_null()) {
    return "";
  }
  else {
    return "";
  }
}
int NUtils::GetJsonInt(json j)
{
  ///
  /// Returns json int if it is valid
  ///

  if (j.is_number_integer()) {
    return j.get<int>();
  }
  else if (j.is_number_float()) {
    return static_cast<int>(j.get<double>());
  }
  else if (j.is_boolean()) {
    return j.get<bool>() ? 1 : 0;
  }
  else if (j.is_null()) {
    return -1;
  }
  else {
    return -1;
  }
}

double NUtils::GetJsonDouble(json j)
{
  ///
  /// Returns json double if it is valid
  ///

  if (j.is_number_float()) {
    return j.get<double>();
  }
  else if (j.is_number_integer()) {
    return static_cast<double>(j.get<int>());
  }
  else if (j.is_boolean()) {
    return j.get<bool>() ? 1.0 : 0.0;
  }
  else if (j.is_null()) {
    return -1.0;
  }
  else {
    return -1.0;
  }
}

bool NUtils::GetJsonBool(json j)
{
  ///
  /// Returns json bool if it is valid
  ///

  if (j.is_boolean()) {
    return j.get<bool>();
  }
  else if (j.is_number_integer()) {
    return j.get<int>() != 0;
  }
  else if (j.is_number_float()) {
    return j.get<double>() != 0.0;
  }
  else if (j.is_null()) {
    return false;
  }
  else {
    return false;
  }
}

std::vector<std::string> NUtils::GetJsonStringArray(json j)
{
  ///
  /// Returns json string array if it is valid
  ///

  std::vector<std::string> out;
  if (j.is_array()) {
    for (auto & v : j) {
      out.push_back(GetJsonString(v));
    }
  }
  return out;
}

std::vector<int> NUtils::ArrayToVector(Int_t * v1, int size)
{
  ///
  /// Convert array to vector
  ///

  std::vector<int> v2;
  for (int i = 0; i < size; i++) {
    v2.push_back(v1[i]);
  }
  return v2;
}

void NUtils::VectorToArray(std::vector<int> v1, Int_t * v2)
{
  ///
  /// Convert vector to array
  ///

  for (size_t i = 0; i < v1.size(); i++) {
    v2[i] = v1[i];
  }
}
std::string NUtils::GetCoordsString(const std::vector<Long64_t> & coords, int index, int width)
{
  ///
  /// Get coordinates string
  ///
  std::stringstream msg;
  if (index >= 0) msg << "[" << std::setw(3) << std::setfill('0') << index << "] ";
  msg << "[";
  for (size_t i = 0; i < coords.size(); ++i) {
    msg << std::setw(width) << std::setfill(' ') << coords[i] << (i == coords.size() - 1 ? "" : ",");
  }
  msg << "]";
  return msg.str();
}
std::string NUtils::GetCoordsString(const std::vector<int> & coords, int index, int width)
{
  ///
  /// Get coordinates string
  ///
  std::stringstream msg;
  if (index >= 0) msg << "[" << std::setw(3) << std::setfill('0') << index << "] ";
  msg << "[";
  for (size_t i = 0; i < coords.size(); ++i) {
    msg << std::setw(width) << std::setfill(' ') << coords[i] << (i == coords.size() - 1 ? "" : ",");
  }
  msg << "]";
  return msg.str();
}
std::string NUtils::GetCoordsString(const std::vector<size_t> & coords, int index, int width)
{
  ///
  /// Get coordinates string
  ///
  std::stringstream msg;
  if (index >= 0) msg << "[" << std::setw(3) << std::setfill('0') << index << "] ";
  msg << "[";
  for (size_t i = 0; i < coords.size(); ++i) {
    msg << std::setw(width) << std::setfill(' ') << coords[i] << (i == coords.size() - 1 ? "" : ",");
  }
  msg << "]";
  return msg.str();
}
std::string NUtils::GetCoordsString(const std::vector<std::string> & coords, int index, int width)
{
  ///
  /// Get coordinates string
  ///
  std::stringstream msg;
  if (index >= 0) msg << "[" << std::setw(3) << std::setfill('0') << index << "] ";
  msg << "[";
  for (size_t i = 0; i < coords.size(); ++i) {
    msg << std::setw(width) << std::setfill(' ') << coords[i] << (i == coords.size() - 1 ? "" : ",");
  }
  msg << "]";
  return msg.str();
}
void NUtils::PrintPointSafe(const std::vector<int> & coords, int index)
{
  ///
  /// Print point safe
  ///

  NLogInfo("%s", GetCoordsString(coords, index).c_str());
}

std::vector<std::vector<int>> NUtils::Permutations(const std::vector<int> & v)
{
  ///
  /// Return all permutations of a vector
  ///
  std::vector<std::vector<int>> result;
  std::vector<int>              current = v;
  std::sort(current.begin(), current.end());
  do {
    result.push_back(current);
  } while (std::next_permutation(current.begin(), current.end()));

  // print the permutations
  NLogTrace("Permutations of vector: %s", GetCoordsString(v).c_str());
  for (const auto & perm : result) {
    NLogTrace("Permutation: %s", GetCoordsString(perm).c_str());
  }

  return result;
}

std::string NUtils::FormatTime(long long seconds)
{
  long long hours = seconds / 3600;
  seconds %= 3600;
  long long minutes = seconds / 60;
  seconds %= 60;

  std::stringstream ss;
  ss << std::setw(2) << std::setfill('0') << hours << ":" << std::setw(2) << std::setfill('0') << minutes << ":"
     << std::setw(2) << std::setfill('0') << seconds;
  return ss.str();
}

std::string NUtils::FormatBytes(long long bytes, int precision)
{
  const char *     units[]    = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  constexpr size_t kUnitCount = sizeof(units) / sizeof(units[0]);

  const bool negative = (bytes < 0);
  double     value    = negative ? static_cast<double>(-bytes) : static_cast<double>(bytes);
  size_t     unitIdx  = 0;
  while (value >= 1024.0 && unitIdx + 1 < kUnitCount) {
    value /= 1024.0;
    ++unitIdx;
  }

  std::stringstream ss;
  if (negative) ss << "-";
  if (unitIdx == 0) {
    ss << static_cast<long long>(value) << " " << units[unitIdx];
  }
  else {
    if (precision < 0) precision = 0;
    ss << std::fixed << std::setprecision(precision) << value << " " << units[unitIdx];
  }
  return ss.str();
}

void NUtils::ProgressBar(int current, int total, std::string prefix, std::string suffix, int barWidth)
{

  ///
  /// Print progress bar
  ///
  // Respect run-mode logging flag: if run output is disabled, do not print
  if (!Ndmspc::NLogger::GetRunOutput()) return;
  if (total == 0) {
    std::lock_guard<std::recursive_mutex> lock(NLogger::GetLoggerMutex());
    std::cout << "\r";
    if (!prefix.empty()) std::cout << "[" << prefix << "]";
    std::cout << "[";
    for (int i = 0; i < barWidth; ++i) {
      std::cout << "=";
    }
    std::cout << "] 100% (0/0)";
    if (!suffix.empty()) std::cout << " [" << suffix << "]";
    std::cout << std::endl;
    std::cout << std::flush;
    return;
  }

  // Throttle updates per callsite to avoid flooding the terminal.
  {
    bool                        forcePrint = (current == total);
    void *                      caller     = __builtin_return_address(0);
    auto                        now        = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> plock(gProgressThrottleMutex);
    auto                        it = gLastProgressTime.find(caller);
    if (it != gLastProgressTime.end() && !forcePrint) {
      double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - it->second).count();
      if (elapsed < gProgressThrottleSeconds) return; // too soon
    }
    gLastProgressTime[caller] = now;
  }

  // Let's do protection against any log to be written during progress bar
  std::lock_guard<std::recursive_mutex> lock(NLogger::GetLoggerMutex());

  float percentage = static_cast<float>(current) / total;
  int   numChars   = static_cast<int>(percentage * barWidth);

  std::cout << "\r";                                      // Carriage return
  if (!prefix.empty()) std::cout << "[" << prefix << "]"; // Carriage return
  std::cout << "[";                                       // Carriage return

  for (int i = 0; i < numChars; ++i) {
    std::cout << "=";
  }
  for (int i = 0; i < barWidth - numChars; ++i) {
    std::cout << " ";
  }
  std::cout << "] " << static_cast<int>(percentage * 100.0) << "%"
            << " (" << current << "/" << total << ")";
  if (!suffix.empty()) std::cout << " [" << suffix << "]";
  if (current == total) std::cout << std::endl;
  std::cout << std::flush; // Ensure immediate output
}

void NUtils::ProgressBar(int current, int total, std::chrono::high_resolution_clock::time_point startTime,
                         std::string prefix, std::string suffix, int barWidth)
{
  ///
  /// Print progress bar
  ///
  // Respect run-mode logging flag: if run output is disabled, do not print
  if (!Ndmspc::NLogger::GetRunOutput()) return;
  if (total == 0) {
    std::lock_guard<std::recursive_mutex> lock(NLogger::GetLoggerMutex());
    std::cout << "\r[";
    if (!prefix.empty()) std::cout << prefix << "][";
    for (int i = 0; i < barWidth; ++i) {
      std::cout << "=";
    }
    std::cout << "] 100% | ETA: 00:00:00 | elapsed: 00:00:00 | 0/0";
    if (!suffix.empty()) std::cout << " [" << suffix << "]";
    std::cout << std::endl;
    std::cout << std::flush;
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(NLogger::GetLoggerMutex());
  if (current > total) current = total; // Cap current to total for safety

  // Throttle updates per callsite to avoid flooding the terminal.
  {
    bool                        forcePrint = (current == total);
    void *                      caller     = __builtin_return_address(0);
    auto                        now        = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> plock(gProgressThrottleMutex);
    auto                        it = gLastProgressTime.find(caller);
    if (it != gLastProgressTime.end() && !forcePrint) {
      double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - it->second).count();
      if (elapsed < gProgressThrottleSeconds) return; // too soon
    }
    gLastProgressTime[caller] = now;
  }

  float percentage = static_cast<float>(current) / total;
  int   numChars   = static_cast<int>(percentage * barWidth);

  // std::cout << "\r[" << prefix << "] ["; // Carriage return
  std::cout << "\r[";
  if (!prefix.empty()) std::cout << prefix << "]["; // Carriage return
  for (int i = 0; i < numChars; ++i) {
    std::cout << "=";
  }
  for (int i = 0; i < barWidth - numChars; ++i) {
    std::cout << " ";
  }
  std::cout << "] " << std::setw(3) << static_cast<int>(percentage * 100.0) << "%";

  // Calculate elapsed time
  auto currentTime    = std::chrono::high_resolution_clock::now();
  auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();

  // Calculate estimated remaining time (only if we've made some progress)
  long long estimatedRemainingSeconds = 0;
  if (current > 0 && percentage > 0) {
    // Total estimated time = (elapsed time / current progress) * 100%
    long long totalEstimatedSeconds = static_cast<long long>(elapsedSeconds / percentage);
    estimatedRemainingSeconds       = totalEstimatedSeconds - elapsedSeconds;
  }

  std::cout << " (" << current << "/" << total << ") "
            << "Elapsed: " << FormatTime(elapsedSeconds) << " "
            << "ETA: " << FormatTime(estimatedRemainingSeconds);
  if (!suffix.empty()) std::cout << " [" << suffix << "]";
  if (current == total) std::cout << std::endl;
  std::cout << std::flush; // Ensure immediate output
}

TCanvas * NUtils::CreateCanvas(const std::string & name, const std::string & title, int width, int height)
{
  ///
  /// Create canvas
  ///

  TThread::Lock();
  TCanvas * c = new TCanvas("", title.c_str(), width, height);
  gROOT->GetListOfCanvases()->Remove(c);
  c->ResetBit(kMustCleanup);
  c->SetBit(kCanDelete, kFALSE);
  c->SetName(name.c_str());
  TThread::UnLock();
  return c;
}

void NUtils::SafeDeleteObjects(std::vector<TObject *> & objects)
{
  if (objects.empty()) return;

  // With EnableThreadSafety(), every TList has fUsingRWLock=true, so
  // TList::Clear() acquires gCoreMutex and calls GarbageCollect() on each
  // element.  When deleting a TCanvas, the cascade TPad::Close() →
  // fPrimitives->Clear() → GarbageCollect() crashes.
  //
  // Fix: fully disarm every pad's primitive list (disable RW lock, mark
  // non-owning, remove all links with "nodelete"), then delete the objects.
  // Orphaned primitives (histograms, frames, sub-pads) are collected and
  // deleted manually afterwards.

  Bool_t prevMustClean = gROOT->MustClean();
  gROOT->SetMustClean(kFALSE);

  // Collect all pads breadth-first (top-level + nested sub-pads)
  std::vector<TPad *> pads;
  for (auto * obj : objects) {
    if (obj && obj->InheritsFrom(TPad::Class())) pads.push_back(static_cast<TPad *>(obj));
  }
  for (size_t i = 0; i < pads.size(); ++i) {
    TList * prims = pads[i]->GetListOfPrimitives();
    if (!prims || prims->IsEmpty()) continue;
    for (TObjLink * lnk = prims->FirstLink(); lnk; lnk = lnk->Next()) {
      TObject * child = lnk->GetObject();
      if (child && child->InheritsFrom(TPad::Class())) pads.push_back(static_cast<TPad *>(child));
    }
  }

  // Track input objects to avoid double-deleting shared primitives
  std::set<TObject *> inputSet(objects.begin(), objects.end());
  inputSet.erase(nullptr);

  // Disarm all pad primitive lists (deepest first) and collect orphans
  std::set<TObject *> orphans;
  for (auto it = pads.rbegin(); it != pads.rend(); ++it) {
    TList * prims = (*it)->GetListOfPrimitives();
    if (!prims) continue;
    // Collect primitives not in the input vector — they'd leak otherwise
    for (TObjLink * lnk = prims->FirstLink(); lnk; lnk = lnk->Next()) {
      TObject * child = lnk->GetObject();
      if (child && inputSet.find(child) == inputSet.end()) orphans.insert(child);
    }
    // Disarm: no lock, non-owning, remove links only (no GarbageCollect)
    prims->UseRWLock(kFALSE);
    prims->SetOwner(kFALSE);
    prims->Clear("nodelete");
  }

  // Delete all input objects (canvas/pad destructors find empty primitive lists)
  for (auto * obj : objects) {
    if (obj) delete obj;
  }
  objects.clear();

  // Delete orphaned primitives (histograms, frames, sub-pads extracted above)
  for (auto * obj : orphans) {
    delete obj;
  }

  gROOT->SetMustClean(prevMustClean);
}

void NUtils::SafeDeleteTList(TList *& lst)
{
  if (!lst) return;

  // Extract objects from the TList into a vector
  std::vector<TObject *> objects;
  for (TObjLink * lnk = lst->FirstLink(); lnk; lnk = lnk->Next()) {
    TObject * obj = lnk->GetObject();
    if (obj) objects.push_back(obj);
  }

  // Destroy the TList shell without touching objects
  lst->UseRWLock(kFALSE);
  lst->SetOwner(kFALSE);
  lst->Clear("nodelete");
  delete lst;
  lst = nullptr;

  // Delete all collected objects safely
  SafeDeleteObjects(objects);
}

void NUtils::SafeDeleteObject(TObject *& obj)
{
  if (!obj) return;

  if (obj->InheritsFrom(TList::Class())) {
    TList * lst = static_cast<TList *>(obj);
    obj         = nullptr;
    SafeDeleteTList(lst);
  }
  else {
    delete obj;
    obj = nullptr;
  }
}

json NUtils::GetSystemStats()
{
  json       out;
  ProcInfo_t info;
  gSystem->GetProcInfo(&info);

  out["cpu_user"]     = info.fCpuUser;
  out["cpu_sys"]      = info.fCpuSys;
  out["cpu_total"]    = info.fCpuUser + info.fCpuSys;
  out["mem_rss_kb"]   = info.fMemResident;
  out["mem_vsize_kb"] = info.fMemVirtual;

  // What the process may use, which inside a container is the cgroup's quota and not the node's core
  // count (see AvailableCpuCount): reporting the node would make every percentage a client computes
  // from this read a fraction of a machine the process was never given.
  out["cpu_count"] = NUtils::AvailableCpuCount();

  // And its memory ceiling, the same way (0 when nothing limits it): a client showing "RSS / max"
  // needs the pod's limit, and the pod's limit is the cgroup's.
  out["mem_max_kb"] = NUtils::AvailableMemoryKb();

  return out;
}

double NUtils::AvailableCpuCount(const std::string & cgroupRoot)
{
  const auto quoted = [](std::istream & stream, double & value) {
    std::string text;
    if (!(stream >> text) || text == "max") return false;
    try {
      value = std::stod(text);
    }
    catch (const std::exception &) {
      return false;
    }
    return true;
  };

  // cgroup v2: the quota and the period in one file ("150000 100000" is a core and a half).
  {
    std::ifstream max(cgroupRoot + "/cpu.max");
    std::string   quotaText;
    double        quota = 0;
    double        period = 0;
    if (max >> quotaText >> period && quotaText != "max" && period > 0) {
      try {
        quota = std::stod(quotaText);
      }
      catch (const std::exception &) {
        quota = 0;
      }
      if (quota > 0) return quota / period;
    }
  }

  // cgroup v1: the same two numbers in two files, where -1 (or anything not positive) means nothing.
  {
    std::ifstream quotaFile(cgroupRoot + "/cpu/cpu.cfs_quota_us");
    std::ifstream periodFile(cgroupRoot + "/cpu/cpu.cfs_period_us");
    double        quota  = 0;
    double        period = 0;
    if (quoted(quotaFile, quota) && quoted(periodFile, period) && quota > 0 && period > 0) {
      return quota / period;
    }
  }

  // Nothing limits this process: the node's logical CPUs are the honest ceiling.
  const unsigned int host = std::thread::hardware_concurrency();
  return host == 0 ? 1.0 : static_cast<double>(host);
}

long NUtils::AvailableMemoryKb(const std::string & cgroupRoot, const std::string & memInfoPath)
{
  const auto bytes = [](const std::string & text, double & value) {
    if (text.empty() || text == "max") return false;
    try {
      value = std::stod(text);
    }
    catch (const std::exception &) {
      return false;
    }
    // cgroup v1 writes a sentinel just under the largest signed 64-bit value to mean "no limit".
    return value > 0 && value < 1e18;
  };

  // cgroup v2: the ceiling in one file, in bytes, or "max".
  {
    std::ifstream max(cgroupRoot + "/memory.max");
    std::string   text;
    double        limit = 0;
    if (max >> text && bytes(text, limit)) return static_cast<long>(limit / 1024);
  }

  // cgroup v1: the same ceiling in its own file, with the same sentinel for "no limit".
  {
    std::ifstream limitFile(cgroupRoot + "/memory/memory.limit_in_bytes");
    std::string   text;
    double        limit = 0;
    if (limitFile >> text && bytes(text, limit)) return static_cast<long>(limit / 1024);
  }

  // Nothing limits it: the node's memory is what it may reach. A machine that will not say answers 0,
  // which a caller reads as no known maximum rather than as zero bytes.
  std::ifstream meminfo(memInfoPath);
  std::string   line;
  while (std::getline(meminfo, line)) {
    if (line.rfind("MemTotal:", 0) != 0) continue;
    std::istringstream values(line.substr(9));
    long               kb = 0;
    if (values >> kb && kb > 0) return kb;
    break;
  }
  return 0;
}

json NUtils::GetTFileIOStats()
{
  json out;
  out["totalRead"]    = 0LL;
  out["totalWritten"] = 0LL;

  TList * files = (TList *)gROOT->GetListOfFiles();
  if (!files) return out;

  Long64_t totalRead    = 0;
  Long64_t totalWritten = 0;

  TIter     next(files);
  TObject * obj = nullptr;
  while ((obj = next())) {
    TFile * f = dynamic_cast<TFile *>(obj);
    if (!f) continue;
    json fi;
    fi["name"]     = f->GetName() ? f->GetName() : "";
    fi["isZombie"] = (bool)f->IsZombie();
    fi["isOpen"]   = (bool)f->IsOpen();

    // Try to read per-file counters if available in this ROOT build
    Long64_t bytesRead    = 0;
    Long64_t bytesWritten = 0;
    // Many ROOT versions expose GetBytesRead/GetBytesWritten on TFile; attempt to call them.
    // If they are not available, these calls will fail to link — in that case, users
    // can replace this implementation with a platform-specific /proc reader.
#if 1
    // Use C-style cast to call methods if they exist; rely on linker to resolve.
    // If unavailable, these lines may need adjustment for older ROOT versions.
    try {
      bytesRead    = f->GetBytesRead();
      bytesWritten = f->GetBytesWritten();
    }
    catch (...) {
      bytesRead    = 0;
      bytesWritten = 0;
    }
#endif

    fi["bytesRead"]    = bytesRead;
    fi["bytesWritten"] = bytesWritten;

    totalRead += bytesRead;
    totalWritten += bytesWritten;

    out["files"].push_back(fi);
  }

  out["totalRead"]    = totalRead;
  out["totalWritten"] = totalWritten;

  return out;
}

json NUtils::GetNetDevStats()
{
  json out;
  out["total_rx"] = 0ULL;
  out["total_tx"] = 0ULL;

#if defined(__linux__)
  std::ifstream f("/proc/net/dev");
  if (!f.good()) return out;
  std::string line;
  // skip headers
  std::getline(f, line);
  std::getline(f, line);
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string ifname = line.substr(0, colon);
    // trim
    auto ltrim = [](std::string & s) {
      size_t start = s.find_first_not_of(" \t");
      if (start != std::string::npos)
        s = s.substr(start);
      else
        s.clear();
    };
    auto rtrim = [](std::string & s) {
      size_t end = s.find_last_not_of(" \t");
      if (end != std::string::npos)
        s = s.substr(0, end + 1);
      else
        s.clear();
    };
    ltrim(ifname);
    rtrim(ifname);
    std::string                     rest = line.substr(colon + 1);
    std::stringstream               ss(rest);
    std::vector<unsigned long long> vals;
    std::string                     tok;
    while (ss >> tok) {
      try {
        vals.push_back(std::stoull(tok));
      }
      catch (...) {
        vals.push_back(0ULL);
      }
    }
    if (vals.size() >= 9) {
      unsigned long long rx = vals[0];
      unsigned long long tx = vals[8];
      json               iface;
      iface["name"] = ifname;
      iface["rx"]   = rx;
      iface["tx"]   = tx;
      out["interfaces"].push_back(iface);
      out["total_rx"] = static_cast<unsigned long long>(
                            out["total_rx"].is_null() ? 0ULL : out["total_rx"].get<unsigned long long>()) +
                        rx;
      out["total_tx"] = static_cast<unsigned long long>(
                            out["total_tx"].is_null() ? 0ULL : out["total_tx"].get<unsigned long long>()) +
                        tx;
    }
  }

#elif defined(__APPLE__)
  struct ifaddrs * ifap = nullptr;
  if (getifaddrs(&ifap) != 0) return out;
  for (struct ifaddrs * ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_data) continue;
    struct if_data * ifd = (struct if_data *)ifa->ifa_data;
    if (!ifd) continue;
    unsigned long long rx = (unsigned long long)ifd->ifi_ibytes;
    unsigned long long tx = (unsigned long long)ifd->ifi_obytes;
    json               iface;
    iface["name"] = ifa->ifa_name ? ifa->ifa_name : std::string();
    iface["rx"]   = rx;
    iface["tx"]   = tx;
    out["interfaces"].push_back(iface);
    out["total_rx"] =
        static_cast<unsigned long long>(out["total_rx"].is_null() ? 0ULL : out["total_rx"].get<unsigned long long>()) +
        rx;
    out["total_tx"] =
        static_cast<unsigned long long>(out["total_tx"].is_null() ? 0ULL : out["total_tx"].get<unsigned long long>()) +
        tx;
  }
  freeifaddrs(ifap);
#else
  // Unsupported platform: return empty totals
#endif

  return out;
}

// ---------------------------------------------------------------------------
// ROOT files -> NGnTree (used by FileToNGnTree and NGnTreeMerge)
// ---------------------------------------------------------------------------

// Shell-style glob match: '*' matches any run of characters (including none),
// '?' matches exactly one character. Everything else is literal.
static bool NUtilsGlobMatch(const std::string & text, const std::string & pattern)
{
  size_t ti = 0, pi = 0, star = std::string::npos, mark = 0;

  while (ti < text.size()) {
    if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++ti;
      ++pi;
    }
    else if (pi < pattern.size() && pattern[pi] == '*') {
      star = pi++;
      mark = ti;
    }
    else if (star != std::string::npos) {
      pi = star + 1;
      ti = ++mark;
    }
    else {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*') ++pi;
  return pi == pattern.size();
}

// Key at a path, without reading the object it describes. The final segment is looked up as a
// key of its parent directory; only the ancestor segments are resolved as directories.
static TKey * NUtilsKeyAt(TDirectory * dir, const std::string & path)
{
  if (dir == nullptr || path.empty()) return nullptr;

  std::string  parent = path;
  std::string  name   = path;
  const size_t slash  = path.rfind('/');
  if (slash != std::string::npos) {
    parent = path.substr(0, slash);
    name   = path.substr(slash + 1);
  }

  TDirectory * d = dir;
  if (slash != std::string::npos) {
    size_t start = 0;
    while (start < parent.size()) {
      const size_t next = parent.find('/', start);
      const std::string part =
          parent.substr(start, next == std::string::npos ? std::string::npos : next - start);
      if (!part.empty()) {
        d = d->GetDirectory(part.c_str());
        if (d == nullptr) return nullptr;
      }
      if (next == std::string::npos) break;
      start = next + 1;
    }
  }

  return d->GetKey(name.c_str());
}

// Recursively collect keys whose name (or full path) matches any of the patterns.
// A key that resolves to a real subdirectory is walked; anything else is a leaf and is
// recorded as (path, class) without being read. TKey::IsFolder() is deliberately not
// used: it reports true for classes not known to ROOT (e.g. THnSparseT<TArrayF>), which
// would skip those objects entirely.
static void NUtilsCollectMatching(TDirectory * dir, const std::string & prefix,
                                  const std::vector<std::string> & patterns,
                                  std::vector<std::pair<std::string, std::string>> & out)
{
  TList * keys = dir->GetListOfKeys();
  if (!keys) return;

  for (TObject * o : *keys) {
    TKey * key = dynamic_cast<TKey *>(o);
    if (!key) continue;

    std::string name = key->GetName();
    std::string path = prefix.empty() ? name : prefix + "/" + name;

    // Only a real directory key is descended into: asking GetDirectory about a leaf can make
    // ROOT read the object, which defeats the key-only scan.
    if (std::string(key->GetClassName()).rfind("TDirectory", 0) == 0) {
      TDirectory * sub = dir->GetDirectory(name.c_str());
      if (sub != nullptr) {
        NUtilsCollectMatching(sub, path, patterns, out);
        continue;
      }
    }

    for (const auto & pat : patterns) {
      if (NUtilsGlobMatch(name, pat) || NUtilsGlobMatch(path, pat)) {
        out.emplace_back(path, std::string(key->GetClassName()));
        break;
      }
    }
  }
}

// Resolve the config's object patterns into (path, class) rows for an open file.
// A pattern without wildcards is passed through verbatim so a missing name is still
// reported (empty class) rather than silently dropped.
static std::vector<std::pair<std::string, std::string>> NUtilsResolveObjects(
    TFile * f, const std::vector<std::string> & patterns)
{
  std::vector<std::pair<std::string, std::string>> out;
  std::vector<std::string>                         globs, plain;

  for (const auto & p : patterns) {
    if (p.find('*') != std::string::npos || p.find('?') != std::string::npos) globs.push_back(p);
    else plain.push_back(p);
  }

  for (const auto & p : globs) NUtilsCollectMatching(f, "", {p}, out);

  for (const auto & p : plain) {
    TKey * key = NUtilsKeyAt(f, p);
    out.emplace_back(p, key ? std::string(key->GetClassName()) : std::string());
  }

  return out;
}

// Read a plain-text list of file paths: one per line, blank lines and '#' comments
// ignored, surrounding whitespace trimmed.
static std::vector<std::string> NUtilsReadFileList(const std::string & filename)
{
  std::vector<std::string> out;
  std::ifstream            in(filename.c_str());
  if (!in) return out;

  std::string line;
  while (std::getline(in, line)) {
    size_t b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    size_t      e = line.find_last_not_of(" \t\r\n");
    std::string s = line.substr(b, e - b + 1);
    if (s.empty() || s[0] == '#') continue;
    out.push_back(s);
  }
  return out;
}

// Split a comma-separated list, trimming whitespace and dropping empty entries.
static std::vector<std::string> NUtilsSplitCsv(const std::string & csv)
{
  std::vector<std::string> out;
  size_t                   start = 0;

  while (start <= csv.size()) {
    size_t      comma = csv.find(',', start);
    std::string tok   = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

    size_t b = tok.find_first_not_of(" \t");
    if (b != std::string::npos) {
      size_t e = tok.find_last_not_of(" \t");
      out.push_back(tok.substr(b, e - b + 1));
    }

    if (comma == std::string::npos) break;
    start = comma + 1;
  }

  return out;
}

// Expand '~' and environment variables, then resolve against the current working
// directory, falling back on @p macroDir.
static std::string NUtilsResolveInputPath(const std::string & path, const std::string & macroDir)
{
  if (path.empty()) return path;

  std::string p = gSystem->ExpandPathName(path.c_str());
  if (!NUtils::AccessPathName(p) && !macroDir.empty()) {
    std::string alt = macroDir + "/" + p;
    if (NUtils::AccessPathName(alt)) return alt;
  }
  return p;
}

// A local directory (no protocol). Used to tell a split base with a missing manifest apart from
// a plain ROOT file, instead of letting TFile::Open fail on a directory.
static bool NUtilsIsLocalDirectory(const std::string & path)
{
  if (path.empty() || path.find("://") != std::string::npos) return false;

  void * dir = gSystem->OpenDirectory(path.c_str());
  if (dir == nullptr) return false;
  gSystem->FreeDirectory(dir);
  return true;
}

// Expand a single file wildcard ('*', '?') against the filesystem. A pattern naming a
// directory is scanned there; a bare pattern is scanned in the current working directory
// first and then in the macro directory. Matches are sorted for a deterministic order.
static std::vector<std::string> NUtilsExpandGlob(const std::string & pattern, const std::string & macroDir)
{
  std::vector<std::string>                         out;
  const bool                                       hasDir = pattern.find('/') != std::string::npos;
  std::vector<std::pair<std::string, std::string>> probes; // (directory to scan, prefix to emit)

  if (hasDir) probes.emplace_back(gSystem->DirName(pattern.c_str()), gSystem->DirName(pattern.c_str()));
  else {
    probes.emplace_back(".", "");
    if (!macroDir.empty()) probes.emplace_back(macroDir, macroDir);
  }

  std::string base = gSystem->BaseName(pattern.c_str());

  for (const auto & [scanDir, prefix] : probes) {
    TSystemDirectory dir(scanDir.c_str(), scanDir.c_str());
    TList *          entries = dir.GetListOfFiles();
    if (!entries) continue;

    for (TObject * o : *entries) {
      TSystemFile * sf = dynamic_cast<TSystemFile *>(o);
      if (!sf || sf->IsDirectory()) continue;
      if (!NUtilsGlobMatch(sf->GetName(), base)) continue;
      out.push_back(prefix.empty() ? std::string(sf->GetName()) : prefix + "/" + sf->GetName());
    }
    if (!out.empty()) break;
  }

  std::sort(out.begin(), out.end());
  return out;
}

// Expand wildcards in the given entries and drop duplicates, preserving order.
static std::vector<std::string> NUtilsExpandFileList(const std::vector<std::string> & entries,
                                                     const std::string &              macroDir)
{
  std::vector<std::string> out;

  for (const auto & e : entries) {
    std::string              expanded = gSystem->ExpandPathName(e.c_str());
    std::vector<std::string> matches;

    if (expanded.find('*') != std::string::npos || expanded.find('?') != std::string::npos) {
      matches = NUtilsExpandGlob(expanded, macroDir);
      if (matches.empty()) NLogWarning("NFileToNGnTree: no files match '%s'", expanded.c_str());
    }
    else {
      matches.push_back(expanded);
    }

    for (const auto & m : matches) {
      if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    }
  }

  return out;
}

// One output branch: the branch name (object path with '/' -> '.'), the object path it
// came from in the input file, the class used for the branch, and whether the path names
// a directory.
struct NUtilsFileToNGnTreeBranch {
  std::string branch;
  std::string path;
  std::string cls;
  bool        dir{false};
  Long64_t    bytes{0};
};

// ---------------------------------------------------------------------------
// chunkAxes: split one THnSparse into coarse sub-histograms along selected axes
// (all axes are kept, so summing the chunks restores the original exactly).
// ---------------------------------------------------------------------------

// One chunk bin, expressed the same way as the framework's internal bin description:
// {rebin, start, id} over the finest (base) binning of the axis. `min`/`max` are the base bins.
struct NUtilsChunkBin {
  int rebin{1};
  int start{1};
  int id{1};
  int min{0};
  int max{0};
  int labelId{0}; // unique bin id within the chunk definition (1..K, like AddBinning(id, ...))

  std::string label() const
  {
    return std::to_string(rebin) + "_" + std::to_string(start) + "_" + std::to_string(id);
  }
};

struct NUtilsChunkAxis {
  int                         axis{-1};
  int                         nbins{0};
  std::vector<NUtilsChunkBin> bins;
};

// 1-based bin edges (last = nbins+1) -> {rebin, start, id} descriptors. Same conversion as
// NBinning::AddBinningVariable(), but without creating or filling a binning/mapping histogram.
static bool NUtilsBinEdgesToChunkBins(const std::vector<int> & edges, int nbins,
                                      std::vector<NUtilsChunkBin> & out)
{
  out.clear();
  if (edges.size() < 2 || edges.front() != 1 || edges.back() != nbins + 1) return false;

  for (size_t i = 0; i + 1 < edges.size(); i++) {
    const int rebin = edges[i + 1] - edges[i];
    if (rebin < 1) return false;

    NUtilsChunkBin b;
    b.rebin = rebin;
    b.start = (rebin == 1) ? 1 : edges[i] % rebin;
    b.id    = edges[i + 1] / rebin;
    b.min   = rebin * (b.id - 1) + b.start;
    b.max   = b.min + rebin - 1;
    if (b.max > nbins) b.max = nbins;
    if (b.min != edges[i] || b.max != edges[i + 1] - 1) return false; // round-trip must hold
    out.push_back(b);
  }
  return true;
}

// Width groups, same form as a binning definition map entry: {"widths": [[width, count], ...]}.
// A missing count means nbins/width; a short/overshooting last group is clamped to the axis end.
static bool NUtilsBinWidthsToChunkBins(const json & widths, int nbins, std::vector<NUtilsChunkBin> & out)
{
  out.clear();
  if (!widths.is_array() || widths.empty()) return false;

  std::vector<int> edges{1};
  for (const auto & w : widths) {
    if (!w.is_array() || w.empty() || !w[0].is_number_integer()) return false;

    const int width = w[0].get<int>();
    if (width < 1) return false;

    const int count = (w.size() > 1 && w[1].is_number_integer()) ? w[1].get<int>() : nbins / width;
    if (count < 1) return false;

    for (int i = 0; i < count && edges.back() <= nbins; i++) edges.push_back(edges.back() + width);
  }

  if (edges.size() < 2) return false;
  if (edges.back() != nbins + 1) edges.back() = nbins + 1;
  return NUtilsBinEdgesToChunkBins(edges, nbins, out);
}

// Build one axis' chunk bins from a specification (no binning map is created):
//   {"bins": K}                  K equal ranges
//   {"bins": [1, 26, 51, ...]}   explicit 1-based bin edges (last = nbins+1)
//   {"bins": ["25_1_1", ...]}    explicit {rebin, start, id} descriptors
//   {"widths": [[25, 4], ...]}   width groups (as in a binning definition)
//   {"rebin": R, "start": S, "ids": [...]} or {"rebin": R, "start": S, "id": I}
// The bins must tile [1, nbins] exactly, which keeps chunking lossless.
static bool NUtilsResolveChunkAxis(TAxis * axis, const json & e, NUtilsChunkAxis & spec)
{
  spec = NUtilsChunkAxis();
  if (axis == nullptr || !e.is_object()) return false;

  const int nbins = axis->GetNbins();
  if (nbins < 1) return false;
  spec.nbins = nbins;

  auto addBin = [&spec, nbins](int rebin, int start, int id) -> bool {
    if (rebin < 1 || id < 1) return false;

    NUtilsChunkBin b;
    b.rebin = rebin;
    b.start = start;
    b.id    = id;
    b.min   = rebin * (id - 1) + start; // NUtils::GetAxisRangeInBase()
    b.max   = b.min + rebin - 1;
    if (b.min < 1) b.min = 1;
    if (b.max > nbins) b.max = nbins;
    if (b.min > b.max) return false;

    spec.bins.push_back(b);
    return true;
  };

  if (e.contains("groups")) {
    if (!e["groups"].is_array() || e["groups"].empty()) return false;
    for (const auto & g : e["groups"]) {
      if (!g.is_object()) return false;
      const int rebin = g.value("rebin", 0);
      const int start = g.value("start", 1);
      if (g.contains("ids") && g["ids"].is_array()) {
        for (const auto & idValue : g["ids"]) {
          if (!idValue.is_number_integer() || !addBin(rebin, start, idValue.get<int>())) return false;
        }
      }
      else if (g.contains("id")) {
        if (!addBin(rebin, start, g.value("id", 0))) return false;
      }
      else {
        return false;
      }
    }
  }
  else if (e.contains("widths")) {
    if (!NUtilsBinWidthsToChunkBins(e["widths"], nbins, spec.bins)) return false;
  }
  else if (e.contains("bins")) {
    const json & bj = e["bins"];
    if (bj.is_number_integer()) {
      const int k = bj.get<int>();
      if (k < 1 || k > nbins) return false;
      const int rebin = (nbins + k - 1) / k;
      for (int id = 1; id <= k; id++) {
        if (!addBin(rebin, 1, id)) return false;
      }
    }
    else if (bj.is_array() && !bj.empty() && bj[0].is_object()) {
      // Explicit bin groups: [{"rebin":10,"start":1,"ids":[1,2]}, ...]
      for (const auto & g : bj) {
        if (!g.is_object()) return false;
        const int rebin = g.value("rebin", 0);
        const int start = g.value("start", 1);
        if (g.contains("ids") && g["ids"].is_array()) {
          for (const auto & idValue : g["ids"]) {
            if (!idValue.is_number_integer() || !addBin(rebin, start, idValue.get<int>())) return false;
          }
        }
        else if (g.contains("id")) {
          if (!addBin(rebin, start, g.value("id", 0))) return false;
        }
        else {
          return false;
        }
      }
    }
    else if (bj.is_array() && !bj.empty() && bj[0].is_number_integer()) {
      std::vector<int> edges;
      for (const auto & v : bj) {
        if (!v.is_number_integer()) return false;
        edges.push_back(v.get<int>());
      }
      if (!NUtilsBinEdgesToChunkBins(edges, nbins, spec.bins)) return false;
    }
    else if (bj.is_array()) {
      for (const auto & s : bj) {
        if (!s.is_string()) return false;
        int rebin = 0, start = 0, id = 0;
        if (std::sscanf(s.get<std::string>().c_str(), "%d_%d_%d", &rebin, &start, &id) != 3) return false;
        if (!addBin(rebin, start, id)) return false;
      }
    }
    else {
      return false;
    }
  }
  else if (e.contains("rebin")) {
    const int rebin = e.value("rebin", 0);
    const int start = e.value("start", 1);
    if (e.contains("ids") && e["ids"].is_array()) {
      for (const auto & idValue : e["ids"]) {
        if (!idValue.is_number_integer() || !addBin(rebin, start, idValue.get<int>())) return false;
      }
    }
    else if (e.contains("id")) {
      if (!addBin(rebin, start, e.value("id", 0))) return false;
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  // Must tile the axis: sorted by min, no gap and no overlap, first=1, last=nbins.
  std::sort(spec.bins.begin(), spec.bins.end(),
            [](const NUtilsChunkBin & a, const NUtilsChunkBin & b) { return a.min < b.min; });

  // The descriptor's `id` (third element) is only unique per rebin group; the definition bin id
  // that identifies a chunk is its ordinal in the tiled axis (1..K), as in AddBinning(id, ...).
  for (size_t i = 0; i < spec.bins.size(); i++) spec.bins[i].labelId = (int)i + 1;

  int expected = 1;
  for (const auto & b : spec.bins) {
    if (b.min != expected) {
      NLogError("chunkAxes: axis '%s': chunk bin [%d,%d] does not continue the previous one (expected min %d)",
                axis->GetName(), b.min, b.max, expected);
      return false;
    }
    expected = b.max + 1;
  }
  if (expected != nbins + 1) {
    NLogError("chunkAxes: axis '%s': chunk bins stop at %d, the axis has %d bins", axis->GetName(), expected - 1,
              nbins);
    return false;
  }

  return true;
}

// Resolve the configured chunk axes against one THnSparse. `axis` may be an index or an axis name.
static bool NUtilsResolveChunkAxes(THnBase * h, const json & cfg, std::vector<NUtilsChunkAxis> & out)
{
  out.clear();
  if (h == nullptr || !cfg.is_array() || cfg.empty()) return false;

  const int nd = h->GetNdimensions();
  for (const auto & e : cfg) {
    if (!e.is_object()) return false;

    int axis = -1;
    if (e.contains("axis") && e["axis"].is_string()) {
      const std::string name = e["axis"].get<std::string>();
      for (int i = 0; i < nd; i++) {
        if (name == h->GetAxis(i)->GetName()) {
          axis = i;
          break;
        }
      }
    }
    else {
      axis = e.value("axis", -1);
    }
    if (axis < 0 || axis >= nd) return false;

    NUtilsChunkAxis spec;
    if (!NUtilsResolveChunkAxis(h->GetAxis(axis), e, spec)) return false;
    spec.axis = axis;
    out.push_back(std::move(spec));
  }
  return !out.empty();
}

// Chunk rule for one object: the applicable axis spec and an optional per-rule size threshold
// (-1 = use the global chunkMinBytes).
struct NUtilsChunkRule {
  json     axes;
  Long64_t minBytes{-1};
};

// Resolve `chunkAxes` for one object. Two forms are accepted:
//   global list:  [{"axis":0,"bins":4}, ...]                    (applies to every object)
//   rule list:    [{"objects":[".../rotation"], "axes":[...], "chunkMinBytes":0},
//                  {"objects":["*"], "axes":[...]}]             (first match wins, same glob syntax)
static NUtilsChunkRule NUtilsChunkRuleFor(const json & chunkCfg, const std::string & objectPath,
                                          const std::string & branch)
{
  NUtilsChunkRule rule;
  if (!chunkCfg.is_array() || chunkCfg.empty()) return rule;

  // Global form: a plain list of axis specs.
  if (chunkCfg[0].is_object() && chunkCfg[0].contains("axis")) {
    rule.axes = chunkCfg;
    return rule;
  }

  const size_t slash = objectPath.rfind('/');
  const std::string leaf = slash == std::string::npos ? objectPath : objectPath.substr(slash + 1);

  for (const auto & entry : chunkCfg) {
    if (!entry.is_object()) continue;

    const json * axes = nullptr;
    if (entry.contains("axes") && entry["axes"].is_array()) axes = &entry["axes"];
    else if (entry.contains("bins") && entry["bins"].is_array() && !entry["bins"].empty() &&
             entry["bins"][0].is_object())
      axes = &entry["bins"];
    if (axes == nullptr) continue;

    bool matches = !entry.contains("objects");
    if (!matches && entry["objects"].is_array()) {
      for (const auto & p : entry["objects"]) {
        if (!p.is_string()) continue;
        const std::string pattern = p.get<std::string>();
        if (NUtilsGlobMatch(objectPath, pattern) || NUtilsGlobMatch(branch, pattern) ||
            NUtilsGlobMatch(leaf, pattern)) {
          matches = true;
          break;
        }
      }
    }
    if (!matches) continue;

    rule.axes     = *axes;
    rule.minBytes = entry.value("chunkMinBytes", (Long64_t)-1);
    return rule;
  }

  return rule;
}

std::vector<std::string> NUtils::CreateChunkBinning(TAxis * axis, const json & spec)
{
  std::vector<std::string> labels;

  NUtilsChunkAxis resolved;
  if (!NUtilsResolveChunkAxis(axis, spec, resolved)) {
    NLogError("NUtils::CreateChunkBinning: invalid chunk binning for axis '%s'",
              axis != nullptr ? axis->GetName() : "<null>");
    return labels;
  }

  for (const auto & b : resolved.bins) labels.push_back(b.label());
  return labels;
}

// New empty histogram of the same concrete sparse type and axes as `src`.
static THnBase * NUtilsCloneEmptySparse(THnBase * src, const std::string & name, const std::string & title)
{
  auto * hsp = dynamic_cast<THnSparse *>(src);
  if (hsp == nullptr) return nullptr;

  std::vector<TAxis> axes;
  axes.reserve(hsp->GetNdimensions());
  for (Int_t i = 0; i < hsp->GetNdimensions(); i++) {
    axes.emplace_back(*hsp->GetAxis(i));
    axes.back().SetRange(0, 0); // full axis, independent of any current range
  }

  const Int_t chunkSize = hsp->GetChunkSize();
  if (hsp->InheritsFrom(THnSparseD::Class())) return new THnSparseD(name.c_str(), title.c_str(), axes, chunkSize);
  if (hsp->InheritsFrom(THnSparseF::Class())) return new THnSparseF(name.c_str(), title.c_str(), axes, chunkSize);
  if (hsp->InheritsFrom(THnSparseL::Class())) return new THnSparseL(name.c_str(), title.c_str(), axes, chunkSize);
  if (hsp->InheritsFrom(THnSparseI::Class())) return new THnSparseI(name.c_str(), title.c_str(), axes, chunkSize);
  if (hsp->InheritsFrom(THnSparseS::Class())) return new THnSparseS(name.c_str(), title.c_str(), axes, chunkSize);
  if (hsp->InheritsFrom(THnSparseC::Class())) return new THnSparseC(name.c_str(), title.c_str(), axes, chunkSize);
  return nullptr;
}

// Index of the chunk bin containing a base bin of this axis, or -1.
// Underflow/overflow bins (0 and nbins+1) belong to the first/last chunk, so no content is lost.
static int NUtilsChunkBinIndex(const NUtilsChunkAxis & axis, int baseBin)
{
  if (baseBin < 1) baseBin = 1;
  if (baseBin > axis.nbins) baseBin = axis.nbins;

  for (size_t i = 0; i < axis.bins.size(); i++) {
    if (baseBin >= axis.bins[i].min && baseBin <= axis.bins[i].max) return (int)i;
  }
  return -1;
}

// "<id>[_<id>...]": the definition bin id per chunk axis, in spec order. The bin range is defined
// by the axis chunk binning ({rebin, start}) in the chunkAxes spec — a wider chunk means a larger
// rebin; mixed rebin groups still get unique sequential ids.
static std::string NUtilsChunkLabel(const std::vector<const NUtilsChunkBin *> & bins)
{
  std::string label;
  for (size_t i = 0; i < bins.size(); i++) {
    if (i != 0) label += '_';
    label += std::to_string(bins[i]->labelId);
  }
  return label;
}

// "axis0[1,25], axis1[26,50]" for the chunk bins selected by a base-bin coordinate tuple.
static std::string NUtilsChunkRange(const std::vector<NUtilsChunkAxis> & axes, const std::vector<Int_t> & coord)
{
  std::string range;
  for (const auto & a : axes) {
    const int idx = NUtilsChunkBinIndex(a, coord[a.axis]);
    if (idx < 0) return std::string();

    const NUtilsChunkBin & b = a.bins[idx];
    if (!range.empty()) range += ", ";
    range += "axis" + std::to_string(a.axis) + "[" + std::to_string(b.min) + "," + std::to_string(b.max) + "]";
  }
  return range;
}

// Same, from a chunk label ("1_2" = labelId per axis).
static std::string NUtilsChunkRangeFromLabel(const std::vector<NUtilsChunkAxis> & axes, const std::string & label)
{
  std::vector<int> ids;
  std::stringstream ss(label);
  std::string       tok;
  while (std::getline(ss, tok, '_')) {
    try {
      ids.push_back(std::stoi(tok));
    }
    catch (...) {
      return std::string();
    }
  }
  if (ids.size() != axes.size()) return std::string();

  std::string range;
  for (size_t i = 0; i < axes.size(); i++) {
    const NUtilsChunkBin * found = nullptr;
    for (const auto & b : axes[i].bins) {
      if (b.labelId == ids[i]) {
        found = &b;
        break;
      }
    }
    if (found == nullptr) return std::string();

    if (!range.empty()) range += ", ";
    range += "axis" + std::to_string(axes[i].axis) + "[" + std::to_string(found->min) + "," +
             std::to_string(found->max) + "]";
  }
  return range;
}

// Chunk label for one base-bin coordinate tuple; empty when some axis has no matching chunk.
static std::string NUtilsChunkLabelFor(const std::vector<NUtilsChunkAxis> & axes, const std::vector<Int_t> & coord)
{
  std::vector<const NUtilsChunkBin *> selected;
  selected.reserve(axes.size());

  for (const auto & a : axes) {
    const int idx = NUtilsChunkBinIndex(a, coord[a.axis]);
    if (idx < 0) return std::string();
    selected.push_back(&a.bins[idx]);
  }
  return NUtilsChunkLabel(selected);
}

std::vector<std::string> NUtils::FindChunks(THnBase * h, const json & chunkAxes,
                                            const std::vector<std::pair<double, double>> & box)
{
  std::vector<std::string>     labels;
  std::vector<NUtilsChunkAxis> axes;
  if (!NUtilsResolveChunkAxes(h, chunkAxes, axes)) return labels;

  std::vector<std::vector<const NUtilsChunkBin *>> selected;
  selected.reserve(axes.size());

  for (size_t i = 0; i < axes.size(); i++) {
    const NUtilsChunkAxis &             a = axes[i];
    std::vector<const NUtilsChunkBin *> picks;

    if (i < box.size()) {
      const double lo = box[i].first;
      const double hi = box[i].second;
      if (lo > hi) return labels;

      const TAxis * axis = h->GetAxis(a.axis);
      int           b1   = axis->FindBin(lo);
      int           b2   = axis->FindBin(hi);

      // FindBin returns 0 for underflow and nbins+1 for overflow.
      if (b1 < 1) b1 = 1;
      if (b2 > a.nbins) b2 = a.nbins;
      if (b1 > a.nbins || b2 < 1) return labels;

      for (const auto & b : a.bins) {
        if (b.max >= b1 && b.min <= b2) picks.push_back(&b);
      }
      if (picks.empty()) return labels;
    }
    else {
      for (const auto & b : a.bins) picks.push_back(&b);
    }

    selected.push_back(std::move(picks));
  }

  // Mixed-radix Cartesian product of the per-axis chunk bins.
  size_t total = 1;
  for (const auto & r : selected) total *= r.size();

  for (size_t n = 0; n < total; n++) {
    size_t                              rem = n;
    std::vector<const NUtilsChunkBin *> current(selected.size(), nullptr);
    for (size_t i = selected.size(); i-- > 0;) {
      current[i] = selected[i][rem % selected[i].size()];
      rem /= selected[i].size();
    }
    labels.push_back(NUtilsChunkLabel(current));
  }

  NLogDebug("NUtils::FindChunks: %zu chunk axis/axes, %zu box range(s) -> %zu chunk(s)", axes.size(), box.size(),
            labels.size());
  return labels;
}

std::vector<std::string> NUtils::FindChunks(const json & objectEntry, THnBase * h,
                                            const std::vector<std::pair<double, double>> & box)
{
  if (!objectEntry.is_object() || !objectEntry.contains("chunks") || !objectEntry["chunks"].is_object()) {
    return {};
  }
  return FindChunks(h, objectEntry["chunks"].value("axes", json::array()), box);
}

// Branch name keeps the file's directory structure: '/' becomes '.', ROOT's native
// branch-hierarchy separator.
static std::string NUtilsBranchNameFromPath(const std::string & path)
{
  std::string out = path;
  std::replace(out.begin(), out.end(), '/', '.');
  return out;
}

// Register one branch spec, keeping the first path seen for a given branch name.
//
// A directory is carried by a TList of its child names/classes, not by the directory
// object: a TDirectoryFile read from an input file keeps references into that file, so
// writing it into the output tree and reading it back seeks to the wrong places. The TList
// is also what ROOT needs for a dotted child name - the parent branch must exist.
static void NUtilsAddBranchSpec(std::vector<NUtilsFileToNGnTreeBranch> & out, const std::string & path,
                                const std::string & cls, bool dir, Long64_t bytes = 0)
{
  if (!dir && cls.empty()) return;

  std::string branch = NUtilsBranchNameFromPath(path);
  auto        it     = std::find_if(out.begin(), out.end(),
                                    [&](const NUtilsFileToNGnTreeBranch & b) { return b.branch == branch; });
  if (it == out.end()) {
    out.push_back({branch, path, dir ? std::string("TList") : cls, dir, bytes});
  }
  else {
    it->bytes = std::max(it->bytes, bytes);
    if (it->path != path) {
      NLogWarning("NFileToNGnTree: objects '%s' and '%s' both map to branch '%s' - keeping the first",
                  it->path.c_str(), path.c_str(), branch.c_str());
    }
  }
}

// Pre-pass over every input file: the union of objects matched by the patterns, in
// first-seen order. Declaring the union up front keeps every entry filling the same set of
// branches. Every ancestor directory of a matched object is added first: ROOT resolves a
// dotted branch name through its parent branch.
static std::vector<NUtilsFileToNGnTreeBranch> NUtilsCollectBranches(const std::vector<std::string> & files,
                                                                   const std::vector<std::string> & patterns,
                                                                   const std::string &              macroDir)
{
  std::vector<NUtilsFileToNGnTreeBranch> out;

  for (size_t n = 0; n < files.size(); n++) {
    const std::string fn = NUtilsResolveInputPath(files[n], macroDir);
    const std::string at = "[" + std::to_string(n + 1) + "/" + std::to_string(files.size()) + "]";

    NLogInfo("NFileToNGnTree: %s scanning objects in '%s' ...", at.c_str(), fn.c_str());

    TStopwatch watch;
    watch.Start();

    TFile * f = NUtils::OpenFile(fn);
    if (!f || f->IsZombie()) {
      NLogError("NFileToNGnTree: %s cannot open file '%s'", at.c_str(), fn.c_str());
      if (f) delete f;
      continue;
    }

    size_t found = 0;
    for (const auto & [path, cls] : NUtilsResolveObjects(f, patterns)) {
      if (cls.empty()) {
        NLogWarning("NFileToNGnTree: %s object '%s' not found in '%s' - no branch created", at.c_str(), path.c_str(),
                    fn.c_str());
        continue;
      }

      ++found;
      NLogDebug("NFileToNGnTree: %s found '%s' (%s)", at.c_str(), path.c_str(), cls.c_str());

      TKey *      key      = NUtilsKeyAt(f, path);
      std::string keyClass = key != nullptr ? key->GetClassName() : "";
      const bool  isDir    = keyClass.rfind("TDirectory", 0) == 0;

      for (size_t pos = path.find('/'); pos != std::string::npos; pos = path.find('/', pos + 1)) {
        NUtilsAddBranchSpec(out, path.substr(0, pos), "", true);
      }

      NUtilsAddBranchSpec(out, path, cls, isDir, key != nullptr ? key->GetNbytes() : 0);
    }

    f->Close();
    delete f;

    NLogInfo("NFileToNGnTree: %s scanned '%s': %zu object(s) in %.1f s", at.c_str(), fn.c_str(), found,
             watch.RealTime());
  }

  return out;
}

const json NFileToNGnTreeDefaults = json::parse(R"json(
{
  "outFile": "merging_ngnt.root",
  "files": [],
  "objects": ["*"],
  "split": true,
  "splitBase": "",
  "chunkAxes": [],
  "chunkMinBytes": 67108864,
  "reset": false
}
)json");

const json NGnTreeMergeDefaults = json::parse(R"json(
{
  "inputFile": ".ndmspc/merge",
  "outFile": "merged_ngnt.root",
  "inputSplit": false,
  "objects": ["*"],
  "excludeObjects": [],
  "mergeTypes": ["TH1", "TH2", "TH3", "TProfile", "THnSparse", "TEfficiency"],
  "onUnsupported": "keepFirst",
  "force": false,
  "reset": false,
  "combine": true,
  "combineChunks": false,
  "serializeBytes": 67108864,
  "processes": 2,
  "mode": "ipc",
  "port": 5555,
  "stallTimeout": 3600
}
)json");

const json NGnTreeBinningDefaults = json::parse(R"json(
{
  "inputFile": "",
  "objectDirectory": "",
  "objects": ["*"],
  "excludeObjects": [],
  "axes": [],
  "binnings": {},
  "projectionAxis": 0,
  "outFile": "binning_ngnt.root",
  "processes": 2,
  "mode": "ipc",
  "port": 5555,
  "stallTimeout": 3600
}
)json");

// Split-output helpers, defined together with the merge helpers below.
static bool        NUtilsWriteObjectAt(TDirectory * dir, const std::string & path, TObject * obj);
static bool        NUtilsWriteSplitManifest(const std::string & base, const std::vector<std::string> & files,
                                            const std::vector<NUtilsFileToNGnTreeBranch> & branches,
                                            const json & chunkAxes);
static std::string NUtilsInputStamp(const std::string & path);
static bool        NUtilsRemoveTree(const std::string & path);

bool NUtils::FileToNGnTree(const json & cfgIn, const std::string & outFile, const std::string & filesTxt,
                           const std::string & objectsCsv, const std::string & macroDir)
{
  json cfg = cfgIn;

  // Objects declared in the config, plus any extra ones given on the command line.
  std::vector<std::string> objects = cfg.value("objects", std::vector<std::string>{});
  for (const auto & o : NUtilsSplitCsv(objectsCsv)) {
    if (std::find(objects.begin(), objects.end(), o) == objects.end()) {
      objects.push_back(o);
      NLogInfo("NFileToNGnTree: adding object '%s' from the command line", o.c_str());
    }
  }
  cfg["objects"] = objects;

  std::vector<std::string> files = cfg.value("files", std::vector<std::string>{});

  // An explicit text list of files overrides the config's 'files'.
  if (!filesTxt.empty()) {
    std::string              listPath = NUtilsResolveInputPath(filesTxt, macroDir);
    std::vector<std::string> fromTxt  = NUtilsReadFileList(listPath);
    if (fromTxt.empty()) {
      NLogError("NFileToNGnTree: no file paths read from '%s'", listPath.c_str());
      return false;
    }
    files = fromTxt;
    NLogInfo("NFileToNGnTree: using %zu file(s) from '%s' (overrides config 'files')", files.size(), listPath.c_str());
  }

  if (files.empty()) {
    NLogError("NFileToNGnTree: no input files (config 'files' is empty and no text list given)");
    return false;
  }

  // Wildcards in the file entries ('*', '?') are expanded against the filesystem.
  files = NUtilsExpandFileList(files, macroDir);
  if (files.empty()) {
    NLogError("NFileToNGnTree: no input files after expanding wildcards");
    return false;
  }

  std::string out = outFile.empty() ? cfg.value("outFile", std::string("merging_ngnt.root")) : outFile;

  // Split output: one small file per input instead of one aggregate tree.
  const bool  split     = cfg.value("split", false);
  std::string splitBase = cfg.value("splitBase", std::string());
  if (split && splitBase.empty()) splitBase = ".ndmspc/merge";
  if (split) {
    gSystem->mkdir(splitBase.c_str(), kTRUE);
    NLogInfo("NFileToNGnTree: split output -> '%s/<id>/<input file>.root'", splitBase.c_str());
  }

  // Make the macro directory reachable from the (captureless) process function.
  cfg["_macroDir"]   = macroDir;
  cfg["_split"]      = split;
  cfg["_splitBase"]  = splitBase;
  cfg["_fileCount"]  = (int)files.size();
  cfg["_reset"]      = cfg.value("reset", false);
  cfg["_resumeKey"]  = json({{"splitBase", splitBase},
                             {"objects", cfg.value("objects", json::array())},
                             {"chunkAxes", cfg.value("chunkAxes", json::array())},
                             {"chunkMinBytes", cfg.value("chunkMinBytes", (Long64_t)0)}})
                          .dump();

  NLogInfo("NFileToNGnTree: %zu file(s), %zu object name(s)%s", files.size(), objects.size(),
           split ? ", split output" : "");

  // 1D categorical axis: one bin per file, labelled with the file name.
  TObjArray * axes = new TObjArray();
  axes->Add(CreateAxisFromLabels("file", "File", files));

  NGnTree * ngnt = new NGnTree(axes, out);

  std::map<std::string, std::vector<std::vector<int>>> b;
  b["file"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Union of objects across all files; one branch per object is declared up front so that
  // every entry fills the same set of branches.
  DisableObjectAutoRegistration();
  std::vector<NUtilsFileToNGnTreeBranch> branches = NUtilsCollectBranches(files, objects, macroDir);
  if (branches.empty()) NLogWarning("NFileToNGnTree: no objects matched in any input file");

  json branchCfg = json::array();
  for (const auto & br : branches) {
    branchCfg.push_back(
        {{"branch", br.branch}, {"path", br.path}, {"class", br.cls}, {"dir", br.dir}, {"bytes", br.bytes}});
    if (!split && !ngnt->GetStorageTree()->GetBranch(br.branch)) {
      ngnt->GetStorageTree()->AddBranch(br.branch, nullptr, br.cls);
      NLogInfo("NFileToNGnTree: branch '%s' (%s%s)", br.branch.c_str(), br.cls.c_str(), br.dir ? ", directory" : "");
    }
  }
  cfg["_branches"] = branchCfg;
  cfg["_chunkAxes"] = cfg.value("chunkAxes", json::array());

  NGnProcessFuncPtr processFunc = [](NBinningPoint * point, TList * /*output*/, TList * /*outputPoint*/,
                                     int /*threadId*/) {
    if (!point) {
      NLogError("NFileToNGnTree: binning point is nullptr");
      return;
    }

    const json & c        = point->GetCfg();
    std::string  dir      = c.value("_macroDir", std::string());
    std::string  filename = NUtilsResolveInputPath(point->GetBinLabel("file"), dir);

    NLogInfo("NFileToNGnTree: bin=%lld file=%s", point->GetEntryNumber(), filename.c_str());
    if (filename.empty()) return;

    // Split output bookkeeping. A finished input is skipped before its file is opened; --reset
    // (or a changed input/configuration) starts the entry from scratch.
    const bool     split   = c.value("_split", false);
    const Long64_t entryId = point->GetEntryNumber();
    std::string    at;
    std::string    entryDir;
    std::string    outPath;

    if (split) {
      const std::string base      = c.value("_splitBase", std::string());
      const Long64_t    fileCount = c.value("_fileCount", 0);
      at       = "[" + std::to_string(entryId + 1) + "/" + std::to_string(fileCount) + "]";
      entryDir = base + "/" + std::to_string(entryId);
      outPath  = entryDir + "/" + std::string(gSystem->BaseName(filename.c_str()));

      if (!c.value("_reset", false)) {
        json              marker;
        bool              haveMarker = false;
        const std::string markerPath = entryDir + "/.done.json";

        if (NUtils::AccessPathName(markerPath)) {
          std::ifstream in(markerPath);
          if (in) {
            try {
              marker     = json::parse(in);
              haveMarker = true;
            }
            catch (const json::parse_error &) {
              haveMarker = false;
            }
          }
        }

        if (haveMarker && marker.value("input", std::string()) == filename &&
            marker.value("stamp", std::string()) == NUtilsInputStamp(filename) &&
            marker.value("config", std::string()) == c.value("_resumeKey", std::string())) {
          bool complete = !marker.value("outputs", json::array()).empty();
          for (const auto & o : marker.value("outputs", json::array())) {
            if (!NUtils::AccessPathName(o.get<std::string>())) {
              complete = false;
              break;
            }
          }
          if (complete) {
            NLogInfo("NFileToNGnTree: %s '%s' already finished - skipping (use --reset to redo)", at.c_str(),
                     filename.c_str());
            return;
          }
        }
      }

      // Redo: drop this entry's previous output (main file, chunk directories, old marker).
      if (NUtils::AccessPathName(entryDir)) NUtilsRemoveTree(entryDir);
    }

    // Keep this entry's file open until the entry has been filled (the objects live in the
    // file); close the previous entry's file.
    TFile * f = dynamic_cast<TFile *>(point->GetTempObject("file"));
    if (!f || std::string(f->GetName()) != filename) {
      if (f) {
        f->Close();
        delete f;
        point->SetTempObject("file", nullptr);
        f = nullptr;
      }
      TStopwatch openWatch;
      openWatch.Start();

      NLogInfo("NFileToNGnTree: opening '%s' ...", filename.c_str());
      f = NUtils::OpenFile(filename);
      if (!f || f->IsZombie()) {
        NLogError("NFileToNGnTree: cannot open file '%s'", filename.c_str());
        if (f) delete f;
        point->SetTempObject("file", nullptr);
        point->AddLog("cannot open file %s", filename.c_str());
        return;
      }
      point->SetTempObject("file", f);
      NLogDebug("NFileToNGnTree: opened '%s' in %.1f s", filename.c_str(), openWatch.RealTime());
    }

    // Split output: write this input's objects to its own file instead of the storage tree.
    if (split) {
      NLogInfo("NFileToNGnTree: %s '%s' writing to '%s' ...", at.c_str(), filename.c_str(), outPath.c_str());

      TStopwatch watch;
      watch.Start();

      gSystem->mkdir(entryDir.c_str(), kTRUE);

      TFile * entryFile = NUtils::OpenFile(outPath, "RECREATE");
      if (!entryFile || entryFile->IsZombie()) {
        NLogError("NFileToNGnTree: cannot create '%s'", outPath.c_str());
        delete entryFile;
        return;
      }

      const json branches    = c.value("_branches", json::array());
      size_t     leafCount   = 0;
      for (const auto & br : branches) {
        if (!br.value("dir", false)) ++leafCount;
      }

      int    written   = 0;
      int    items     = 0; // objects + chunks written for this input -> merge work items
      size_t leafIndex = 0;
      std::vector<std::string> outputs; // files this entry produced (marker/completeness check)
      for (const auto & br : branches) {
        if (br.value("dir", false)) continue;

        const std::string path = br.at("path").get<std::string>();
        const std::string cls  = br.at("class").get<std::string>();
        ++leafIndex;

        NLogDebug("NFileToNGnTree: %s [%zu/%zu] '%s' (%s)", at.c_str(), leafIndex, leafCount, path.c_str(),
                  cls.c_str());

        TObject * obj = f->Get(path.c_str());
        if (!obj) {
          NLogWarning("NFileToNGnTree: object '%s' not found in '%s'", path.c_str(), filename.c_str());
          continue;
        }

        // Chunk heavy sparse objects along the configured axes (all axes are kept). The chunkAxes
        // spec may be global or per-object rules (first matching rule wins).
        std::vector<NUtilsChunkAxis> chunkAxes;
        auto *                       hnsObj      = dynamic_cast<THnBase *>(obj);
        const std::string            branch      = br.at("branch").get<std::string>();
        const Long64_t               storedBytes = br.value("bytes", (Long64_t)0);

        const NUtilsChunkRule rule = NUtilsChunkRuleFor(c.value("_chunkAxes", json()), path, branch);
        const Long64_t        chunkMinBytes =
            rule.minBytes >= 0 ? rule.minBytes : c.value("chunkMinBytes", (Long64_t)67108864);

        // Only THnSparse objects can be chunked (TH1/2/3, TProfile, TEfficiency, TList stay as-is).
        const bool canChunk = dynamic_cast<THnSparse *>(hnsObj) != nullptr;

        if (!rule.axes.empty()) {
          if (!canChunk) {
            NLogInfo("NFileToNGnTree: '%s' matches a chunk rule but is %s, not THnSparse - not chunked",
                     path.c_str(), obj->ClassName());
          }
          else if (chunkMinBytes != 0 && storedBytes < chunkMinBytes) {
            NLogInfo("NFileToNGnTree: '%s' matches a chunk rule but stored size %lld < %lld - not chunked",
                     path.c_str(), (long long)storedBytes, (long long)chunkMinBytes);
          }
          else if (!NUtilsResolveChunkAxes(hnsObj, rule.axes, chunkAxes)) {
            NLogWarning("NFileToNGnTree: '%s' matches a chunk rule but the axes could not be resolved - not chunked",
                        path.c_str());
            chunkAxes.clear();
          }
          else {
            std::string desc;
            for (const auto & a : chunkAxes) {
              if (!desc.empty()) desc += ", ";
              desc += "axis " + std::to_string(a.axis) + " into " + std::to_string(a.bins.size()) + " chunk(s)";
            }
            NLogDebug("NFileToNGnTree: '%s' will be chunked: %s", path.c_str(), desc.c_str());
          }
        }

        if (!chunkAxes.empty()) {
          std::map<std::string, THnBase *> chunks;
          std::map<std::string, std::string> chunkRanges; // label -> "axis0[1,25], ..."
          std::map<std::string, double>    chunkSumw; // per-chunk sum of contents -> entries
          const Int_t                      nd         = hnsObj->GetNdimensions();
          const bool                       withErrors = hnsObj->GetCalculateErrors();
          std::vector<Int_t>               coord(nd);

          for (Long64_t ibin = 0; ibin < hnsObj->GetNbins(); ++ibin) {
            const Double_t v = hnsObj->GetBinContent(ibin, coord.data());

            const std::string label = NUtilsChunkLabelFor(chunkAxes, coord);
            if (label.empty()) continue; // NUtilsResolveChunkAxes guarantees full coverage
            auto              it    = chunks.find(label);
            if (it == chunks.end()) {
              THnBase * ch = NUtilsCloneEmptySparse(hnsObj, hnsObj->GetName(), hnsObj->GetTitle());
              if (ch == nullptr) {
                NLogError("NFileToNGnTree: cannot create a chunk for '%s' (%s)", path.c_str(), hnsObj->ClassName());
                break;
              }
              it = chunks.emplace(label, ch).first;
              chunkRanges[label] = NUtilsChunkRange(chunkAxes, coord);
            }

            const Long64_t cbin = it->second->GetBin(coord.data(), kTRUE);
            it->second->SetBinContent(cbin, v);
            if (withErrors) it->second->SetBinError2(cbin, hnsObj->GetBinError2(ibin));
            chunkSumw[label] += v;
          }

          // Entries are not split by the histogram itself; the sum of a chunk's contents is the
          // fill count that landed in it, so summing chunks restores the original entry count.
          for (auto & [label, ch] : chunks) ch->SetEntries(chunkSumw[label]);

          for (auto & [label, ch] : chunks) {
            const std::string chunkDir = entryDir + "/" + label;
            const std::string chunkOut = chunkDir + "/" + branch + ".root";
            gSystem->mkdir(chunkDir.c_str(), kTRUE);

            TFile * chunkFile = NUtils::OpenFile(chunkOut, "RECREATE");
            if (!chunkFile || chunkFile->IsZombie()) {
              NLogError("NFileToNGnTree: cannot create chunk '%s'", chunkOut.c_str());
              delete chunkFile;
              delete ch;
              continue;
            }

            NUtilsWriteObjectAt(chunkFile, path, ch);
            chunkFile->cd();
            TNamed provenance(filename.c_str(), "fileToNGnTree-split");
            provenance.Write("provenance", TObject::kOverwrite);
            chunkFile->Close();
            delete chunkFile;
            delete ch;
            outputs.push_back(chunkOut);

            NLogDebug("NFileToNGnTree: %s '%s' chunk %s%s -> '%s'", at.c_str(), path.c_str(), label.c_str(),
                      chunkRanges[label].empty() ? "" : (" (" + chunkRanges[label] + ")").c_str(), chunkOut.c_str());
          }

          if (!chunks.empty()) {
            NLogInfo("NFileToNGnTree: %s '%s' -> %zu chunk(s) of '%s'", at.c_str(), filename.c_str(), chunks.size(),
                     path.c_str());
            items += (int)chunks.size();
            ++written;
            delete obj;
            continue;
          }

          // Nothing could be chunked: keep the object in the per-input file instead of losing it.
          NLogWarning("NFileToNGnTree: chunking '%s' produced no chunks - writing it unchunked", path.c_str());
        }

        if (NUtilsWriteObjectAt(entryFile, path, obj)) {
          ++written;
          ++items;
        }

        // The input file owns the object until it is closed; release it now so an input with
        // many large objects does not accumulate them for the whole entry.
        delete obj;
      }

      entryFile->cd();
      TNamed provenance(filename.c_str(), "fileToNGnTree-split");
      provenance.Write("provenance", TObject::kOverwrite);

      entryFile->Close();
      delete entryFile;

      outputs.push_back(outPath);

      // Written only after every output file of this entry is closed: a rerun can then trust it.
      json marker = {{"input", filename},
                     {"stamp", NUtilsInputStamp(filename)},
                     {"config", c.value("_resumeKey", std::string())},
                     {"outputs", outputs}};
      std::ofstream markerOut((entryDir + "/.done.json").c_str());
      if (markerOut) markerOut << marker.dump(2) << "\n";
      else NLogWarning("NFileToNGnTree: cannot write '%s/.done.json'", entryDir.c_str());

      NLogInfo("NFileToNGnTree: %s '%s' -> '%s' (%d/%zu object(s), %d merge item(s)) in %.1f s", at.c_str(),
               filename.c_str(), outPath.c_str(), written, leafCount, items, watch.RealTime());
      point->AddLog("%s -> %s (%d object(s), %d item(s))", filename.c_str(), outPath.c_str(), written, items);
      return;
    }

    NStorageTree * ts = point->GetTreeStorage();
    if (!ts) {
      NLogError("NFileToNGnTree: no storage tree for '%s'", filename.c_str());
      return;
    }

    // The framework turns this log into the entry's outputPoint, which triggers the fill.
    point->AddLog("%s", filename.c_str());

    // Per-worker placeholders for directory branches. They are rebuilt for every entry and
    // cleared at the start of the next one, i.e. after the previous entry has been filled.
    TList * placeholders = dynamic_cast<TList *>(point->GetTempObject("placeholders"));
    if (!placeholders) {
      placeholders = new TList();
      placeholders->SetName("placeholders");
      placeholders->SetOwner(true);
      point->SetTempObject("placeholders", placeholders);
    }
    else {
      placeholders->Clear();
    }

    for (const auto & br : c.value("_branches", json::array())) {
      std::string branch = br.at("branch").get<std::string>();
      std::string path   = br.at("path").get<std::string>();
      std::string cls    = br.at("class").get<std::string>();

      NTreeBranch * nb = ts->GetBranch(branch);
      if (!nb) {
        ts->AddBranch(branch, nullptr, cls);
        nb = ts->GetBranch(branch);
      }
      if (!nb) continue;

      if (br.value("dir", false)) {
        TList * children = new TList();
        children->SetName(branch.c_str());
        children->SetOwner(true);

        TDirectory * d = f->GetDirectory(path.c_str());
        if (d && d->GetListOfKeys()) {
          for (TObject * o : *d->GetListOfKeys()) {
            TKey * k = dynamic_cast<TKey *>(o);
            if (k) children->Add(new TNamed(k->GetName(), k->GetClassName()));
          }
        }

        placeholders->Add(children);
        nb->SetAddress(children);
        continue;
      }

      TObject * obj = f->Get(path.c_str());
      if (!obj) {
        NLogWarning("NFileToNGnTree: object '%s' not found in '%s' (empty for this file)", path.c_str(),
                    filename.c_str());
        point->AddLog("object %s not found", path.c_str());
        nb->SetAddress(nullptr);
        continue;
      }
      nb->SetAddress(obj);
    }
  };

  NGnBeginFuncPtr beginFunc = [](NBinningPoint * /*point*/, int /*threadId*/) {
    DisableObjectAutoRegistration();
  };

  NGnEndFuncPtr endFunc = [](NBinningPoint * point, int /*threadId*/) {
    TFile * f = dynamic_cast<TFile *>(point->GetTempObject("file"));
    if (f) {
      f->Close();
      delete f;
      point->SetTempObject("file", nullptr);
    }

    TList * placeholders = dynamic_cast<TList *>(point->GetTempObject("placeholders"));
    if (placeholders) {
      delete placeholders;
      point->SetTempObject("placeholders", nullptr);
    }
  };

  bool rc = ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);
  if (rc) {
    NLogInfo("NFileToNGnTree: processing completed successfully");
    ngnt->Close(true);
  }
  else {
    NLogError("NFileToNGnTree: processing failed");
    ngnt->Close(false);
  }

  delete ngnt;

  if (split) NUtilsWriteSplitManifest(splitBase, files, branches, cfg.value("chunkAxes", json::array()));

  return rc;
}

// ---------------------------------------------------------------------------
// NGnTree merge helpers
// ---------------------------------------------------------------------------

// Split a path/name on a separator ('/' for file paths, '.' for dotted object names).
static std::vector<std::string> NUtilsSplitPath(const std::string & path, char sep = '/')
{
  std::vector<std::string> out;
  std::string              cur;

  for (char c : path) {
    if (c == sep) {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    }
    else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// The dotted object name as a TFile path: `a.b.c` -> `a/b/c`.
static std::string NUtilsObjectPath(const std::string & name)
{
  std::string path = name;
  std::replace(path.begin(), path.end(), '.', '/');
  return path;
}

// One requested object: its branch name in the input tree, its class, and its size estimate
// (stored bytes / branch bytes), used to process small objects before large ones. Chunked
// objects are expanded into one entry per chunk.
struct NUtilsMergeObject {
  std::string name;
  std::string cls;
  Long64_t    bytes{0};
  std::string objectPath; // path inside the source files
  json        chunkFiles; // [{id,output}] for this chunk; empty -> the global split file list
  json        chunkAxes;  // chunk axis spec (for the output sidecar)
  std::string chunkRange; // "axis0[1,25], ..." for chunked items (from the manifest)
};

// The objects that exist in the input tree, with their class (branch metadata only).
static std::vector<NUtilsMergeObject> NUtilsCollectMergeObjects(const std::string &              inputFile,
                                                                const std::vector<std::string> & objects)
{
  std::vector<NUtilsMergeObject> found;

  TFile * f = NUtils::OpenFile(inputFile);
  if (!f || f->IsZombie()) {
    NLogError("NGnTreeMerge: cannot open input file '%s'", inputFile.c_str());
    if (f) delete f;
    return found;
  }

  TTree * tree = dynamic_cast<TTree *>(f->Get("ngnt"));
  if (!tree) {
    NLogError("NGnTreeMerge: input file '%s' has no 'ngnt' tree", inputFile.c_str());
    f->Close();
    delete f;
    return found;
  }

  auto add = [&found](const std::string & name, const std::string & cls, Long64_t bytes) {
    for (const auto & f : found) {
      if (f.name == name) return;
    }
    found.push_back({name, cls, bytes, NUtilsObjectPath(name), json::array(), json::array(), ""});
  };

  std::vector<std::string> globs;
  for (const auto & p : objects) {
    if (p.find('*') != std::string::npos || p.find('?') != std::string::npos) globs.push_back(p);
  }

  // Patterns are matched against the input tree's branch names (the dotted object names).
  // Directory placeholders (TList) are not mergeable and are skipped.
  if (!globs.empty()) {
    std::vector<std::string> dottedGlobs;
    for (const auto & g : globs) {
      std::string d = g;
      std::replace(d.begin(), d.end(), '/', '.');
      dottedGlobs.push_back(d);
    }

    for (TObject * o : *tree->GetListOfBranches()) {
      TBranch * b = dynamic_cast<TBranch *>(o);
      if (!b) continue;

      std::string name = b->GetName();
      std::string cls  = b->GetClassName() ? b->GetClassName() : "";
      if (cls == "TList") continue;

      for (size_t g = 0; g < globs.size(); g++) {
        if (NUtilsGlobMatch(name, globs[g]) || NUtilsGlobMatch(name, dottedGlobs[g])) {
          add(name, cls, b->GetTotBytes());
          break;
        }
      }
    }
  }

  for (const auto & name : objects) {
    if (name.find('*') != std::string::npos || name.find('?') != std::string::npos) continue;

    // Accept the source-path notation ("Data/h1") as well as the branch name ("Data.h1").
    std::string dottedName = name;
    std::replace(dottedName.begin(), dottedName.end(), '/', '.');

    TBranch * b = tree->GetBranch(name.c_str());
    if (!b && dottedName != name) b = tree->GetBranch(dottedName.c_str());
    if (!b) {
      NLogWarning("NGnTreeMerge: branch '%s' not found in '%s' - skipped", name.c_str(), inputFile.c_str());
      continue;
    }

    const char * cls = b->GetClassName();
    if (cls && std::string(cls) == "TList") {
      NLogWarning("NGnTreeMerge: '%s' is a directory placeholder (TList) - skipped", name.c_str());
      continue;
    }

    add(b->GetName(), cls ? std::string(cls) : std::string(), b->GetTotBytes());
  }

  if (!globs.empty()) NLogInfo("NGnTreeMerge: %zu object(s) resolved from the requested patterns", found.size());

  f->Close();
  delete f;
  return found;
}

enum class NUtilsMergeStrategy { TH1, THnBase, TEfficiency, TList, Unsupported };

static const char * NUtilsStrategyName(NUtilsMergeStrategy strategy)
{
  switch (strategy) {
  case NUtilsMergeStrategy::TH1: return "TH1";
  case NUtilsMergeStrategy::THnBase: return "THnBase";
  case NUtilsMergeStrategy::TEfficiency: return "TEfficiency";
  case NUtilsMergeStrategy::TList: return "TList";
  default: return "Unsupported";
  }
}

// The strategy for an object class: the first configured type it is or derives from.
static NUtilsMergeStrategy NUtilsMergeStrategyFor(const std::string &              className,
                                                  const std::vector<std::string> & types)
{
  TClass * actual = TClass::GetClass(className.c_str());

  for (const auto & t : types) {
    bool matches = (t == className);
    if (!matches && actual != nullptr) {
      TClass * configured = TClass::GetClass(t.c_str());
      matches             = configured != nullptr && actual->InheritsFrom(configured);
    }
    if (!matches) continue;

    if (actual == nullptr) return NUtilsMergeStrategy::Unsupported;
    if (actual->InheritsFrom(TH1::Class())) return NUtilsMergeStrategy::TH1;
    if (actual->InheritsFrom(THnBase::Class())) return NUtilsMergeStrategy::THnBase;
    if (actual->InheritsFrom(TEfficiency::Class())) return NUtilsMergeStrategy::TEfficiency;
    if (actual->InheritsFrom(TList::Class())) return NUtilsMergeStrategy::TList;
    return NUtilsMergeStrategy::Unsupported;
  }

  return NUtilsMergeStrategy::Unsupported;
}

// Merge the entries of `from` into `into` by name; entries are merged with the same rules.
static void NUtilsMergeListInto(TList * into, TList * from)
{
  if (into == nullptr || from == nullptr) return;

  for (TObject * o : *from) {
    if (o == nullptr) continue;

    TObject * existing = into->FindObject(o->GetName());
    if (existing == nullptr) {
      into->Add(o->Clone());
      continue;
    }

    if (auto * a = dynamic_cast<TH1 *>(existing)) {
      if (auto * b = dynamic_cast<TH1 *>(o)) a->Add(b);
      continue;
    }
    if (auto * a = dynamic_cast<THnBase *>(existing)) {
      if (auto * b = dynamic_cast<THnBase *>(o)) a->Add(b);
      continue;
    }
    if (auto * a = dynamic_cast<TList *>(existing)) {
      if (auto * b = dynamic_cast<TList *>(o)) NUtilsMergeListInto(a, b);
      continue;
    }
  }
}

// `<outFile without ".root">.<object name>.root`
static std::string NUtilsPartialPath(const std::string & outFile, const std::string & objectName)
{
  std::string base = outFile;
  if (base.size() > 5 && base.compare(base.size() - 5, 5, ".root") == 0) base.erase(base.size() - 5);

  std::string label = objectName;
  std::replace(label.begin(), label.end(), '/', '_');
  return base + "." + label + ".root";
}

// The combined result file name: `outFile` with a `.root` extension.
static std::string NUtilsCombinedPath(const std::string & outFile)
{
  std::string final = outFile;
  if (final.size() < 5 || final.compare(final.size() - 5, 5, ".root") != 0) final += ".root";
  return final;
}

// "<size>:<mtime>" of the input file, used to detect a changed input.
static std::string NUtilsInputStamp(const std::string & path)
{
  FileStat_t info;
  if (gSystem->GetPathInfo(path.c_str(), info) != 0) return "unknown";
  return std::to_string((long long)info.fSize) + ":" + std::to_string((long long)info.fMtime);
}

// Remove a directory tree (used when a split entry is redone from scratch).
static bool NUtilsRemoveTree(const std::string & path)
{
  TSystemDirectory dir(path.c_str(), path.c_str());
  TList *          entries = dir.GetListOfFiles();
  if (entries == nullptr) {
    gSystem->Unlink(path.c_str());
    return false;
  }

  for (TObject * o : *entries) {
    auto * sf = dynamic_cast<TSystemFile *>(o);
    if (sf == nullptr) continue;

    const std::string name = sf->GetName();
    if (name == "." || name == "..") continue;

    const std::string child = path + "/" + name;
    if (sf->IsDirectory()) NUtilsRemoveTree(child);
    else gSystem->Unlink(child.c_str());
  }

  ::rmdir(path.c_str());
  return true;
}

// Whether the file holds a key at the given path, reading key metadata only.
static bool NUtilsHasObjectAt(TFile * f, const std::string & objectPath)
{
  if (f == nullptr) return false;

  std::vector<std::string> parts = NUtilsSplitPath(objectPath);
  if (parts.empty()) return false;

  TDirectory * dir = f;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    TDirectory * sub = dir->GetDirectory(parts[i].c_str());
    if (sub == nullptr) return false;
    dir = sub;
  }
  return dir->GetKey(parts.back().c_str()) != nullptr;
}

// Human-readable byte count for progress logs.
static std::string NUtilsFormatBytes(Long64_t bytes)
{
  char buf[64];
  if (bytes < 1024) std::snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
  else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f kB", bytes / 1024.0);
  else std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1024.0 / 1024.0);
  return std::string(buf);
}

// Human-readable bin/count for progress logs.
static std::string NUtilsFormatCount(Long64_t n)
{
  char buf[64];
  if (n < 1000) std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
  else if (n < 1000000) std::snprintf(buf, sizeof(buf), "%.1f k", n / 1000.0);
  else std::snprintf(buf, sizeof(buf), "%.1f M", n / 1000000.0);
  return std::string(buf);
}

// Stored bins of a THnBase or cells of a TH1; 0 for anything else. Used to show how much each
// input file contributes to the merged object.
static Long64_t NUtilsObjectBins(TObject * obj)
{
  if (auto * h = dynamic_cast<THnBase *>(obj)) return h->GetNbins();
  if (auto * h = dynamic_cast<TH1 *>(obj)) return h->GetNcells();
  return 0;
}

// Resident memory of this process, in MB (0 when unavailable).
static double NUtilsRssMB()
{
  ProcInfo_t info;
  if (gSystem->GetProcInfo(&info) != 0) return 0.0;
  return info.fMemResident / 1024.0;
}

// Give freed memory back to the OS (glibc keeps it in its arenas otherwise).
static void NUtilsTrimHeap()
{
#if defined(__linux__)
  malloc_trim(0);
#endif
}

// Cross-process exclusive lock, used to merge one heavy object at a time.
struct NUtilsFileLock {
  int fd{-1};

  ~NUtilsFileLock()
  {
    if (fd >= 0) {
      flock(fd, LOCK_UN);
      close(fd);
    }
  }

  bool acquire(const std::string & path)
  {
    if (path.empty()) return false;
    fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX) != 0) {
      close(fd);
      fd = -1;
      return false;
    }
    return true;
  }
};

// A partial is complete when it holds the object at its restored path and its provenance matches.
static bool NUtilsPartialIsComplete(const std::string & partial, const std::string & objectPath,
                                    const std::string & provenanceTitle)
{
  if (!NUtils::AccessPathName(partial)) return false;

  TFile * f = NUtils::OpenFile(partial, "READ");
  if (!f || f->IsZombie()) {
    delete f;
    return false;
  }

  bool ok = NUtilsHasObjectAt(f, objectPath);
  if (ok) {
    TNamed * p = dynamic_cast<TNamed *>(f->Get("provenance"));
    ok         = p != nullptr && p->GetTitle() != nullptr && provenanceTitle == p->GetTitle();
    delete p;
  }

  f->Close();
  delete f;
  return ok;
}

// Write one merged object into its partial, restoring the source directory layout (the dotted
// name becomes directories) and recording the provenance used by the resume check.
static bool NUtilsWriteObjectToPartial(const std::string & partial, const std::string & objectPath, TObject * obj,
                                       const std::string & provenanceName, const std::string & provenanceTitle)
{
  if (obj == nullptr) return false;

  std::vector<std::string> parts = NUtilsSplitPath(objectPath);
  if (parts.empty()) return false;

  TFile * out = NUtils::OpenFile(partial, "RECREATE");
  if (!out || out->IsZombie()) {
    NLogError("NGnTreeMerge: cannot create '%s'", partial.c_str());
    delete out;
    return false;
  }

  TDirectory * dir = out;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    TDirectory * sub = dir->GetDirectory(parts[i].c_str());
    if (sub == nullptr) sub = dir->mkdir(parts[i].c_str());
    if (sub == nullptr) {
      NLogError("NGnTreeMerge: cannot create directory '%s' in '%s'", parts[i].c_str(), partial.c_str());
      break;
    }
    dir = sub;
  }

  dir->cd();
  obj->Write(parts.back().c_str(), TObject::kOverwrite);

  out->cd();
  TNamed provenance(provenanceName.c_str(), provenanceTitle.c_str());
  provenance.Write("provenance", TObject::kOverwrite);

  out->Close();
  delete out;
  return true;
}

// Write one object at a (source) path inside an already open file/directory.
static bool NUtilsWriteObjectAt(TDirectory * dir, const std::string & path, TObject * obj)
{
  if (dir == nullptr || obj == nullptr) return false;

  std::vector<std::string> parts = NUtilsSplitPath(path);
  if (parts.empty()) return false;

  TDirectory * d = dir;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    TDirectory * sub = d->GetDirectory(parts[i].c_str());
    if (sub == nullptr) sub = d->mkdir(parts[i].c_str());
    if (sub == nullptr) return false;
    d = sub;
  }

  d->cd();
  obj->Write(parts.back().c_str(), TObject::kOverwrite);
  return true;
}

// Manifest of a split FileToNGnTree output, written next to the per-input files.
static bool NUtilsWriteSplitManifest(const std::string & base, const std::vector<std::string> & files,
                                     const std::vector<NUtilsFileToNGnTreeBranch> & branches,
                                     const json & chunkAxes)
{
  json manifest;
  manifest["kind"] = "fileToNGnTree-split";
  manifest["base"] = base;

  // Chunk files live in <base>/<id>/<label>/<branch>.root; collect them per branch and label.
  std::map<std::string, std::map<std::string, json>> chunkFiles; // branch -> label -> [file entries]
  for (size_t id = 0; id < files.size(); id++) {
    const std::string entryDir = base + "/" + std::to_string(id);
    TSystemDirectory  dir(entryDir.c_str(), entryDir.c_str());
    TList *           entries = dir.GetListOfFiles();
    if (entries == nullptr) continue;

    for (TObject * o : *entries) {
      auto * sf = dynamic_cast<TSystemFile *>(o);
      if (sf == nullptr || !sf->IsDirectory()) continue;

      const std::string label = sf->GetName();
      if (label == "." || label == "..") continue;
      if (label.find_first_not_of("0123456789_-") != std::string::npos) continue;

      const std::string chunkDir = entryDir + "/" + label;
      TSystemDirectory  cdir(chunkDir.c_str(), chunkDir.c_str());
      TList *           chunkEntries = cdir.GetListOfFiles();
      if (chunkEntries == nullptr) continue;

      for (TObject * fo : *chunkEntries) {
        auto * ff = dynamic_cast<TSystemFile *>(fo);
        if (ff == nullptr || ff->IsDirectory()) continue;

        const std::string fname = ff->GetName();
        if (fname.size() < 6 || fname.compare(fname.size() - 5, 5, ".root") != 0) continue;

        const std::string branch = fname.substr(0, fname.size() - 5);
        chunkFiles[branch][label].push_back({{"id", (int)id}, {"output", chunkDir + "/" + fname}});
      }
    }
  }

  json objects = json::array();
  for (const auto & br : branches) {
    if (br.dir) continue;

    json object = {{"branch", br.branch}, {"path", br.path}, {"class", br.cls}, {"bytes", br.bytes}};
    const auto bit = chunkFiles.find(br.branch);
    if (bit != chunkFiles.end()) {
      // Record the spec that actually applied to this object (global list or matching rule).
      const NUtilsChunkRule rule = NUtilsChunkRuleFor(chunkAxes, br.path, br.branch);

      // Resolve the axis binning once (open one small chunk file) to record base-bin ranges.
      std::vector<NUtilsChunkAxis> axes;
      bool                         resolved = false;
      if (!rule.axes.empty() && !bit->second.empty()) {
        const std::string firstFile = bit->second.begin()->second[0].value("output", std::string());
        if (!firstFile.empty()) {
          TFile * cf = NUtils::OpenFile(firstFile, "READ");
          if (cf && !cf->IsZombie()) {
            resolved = NUtilsResolveChunkAxes(dynamic_cast<THnBase *>(cf->Get(br.path.c_str())), rule.axes, axes);
          }
          if (cf) {
            cf->Close();
            delete cf;
          }
        }
      }

      json groups = json::array();
      for (const auto & [label, chunkEntries] : bit->second) {
        json group = {{"label", label}, {"files", chunkEntries}};
        if (resolved) {
          const std::string range = NUtilsChunkRangeFromLabel(axes, label);
          if (!range.empty()) group["range"] = range;
        }
        groups.push_back(group);
      }

      object["chunks"] = {{"axes", rule.axes}, {"groups", groups}};
    }
    objects.push_back(object);
  }
  manifest["objects"] = objects;

  json written = json::array();
  for (size_t i = 0; i < files.size(); i++) {
    const std::string output =
        base + "/" + std::to_string(i) + "/" + std::string(gSystem->BaseName(files[i].c_str()));
    if (!NUtils::AccessPathName(output)) continue; // input failed; nothing written for it
    written.push_back({{"id", (int)i},
                       {"input", files[i]},
                       {"stamp", NUtilsInputStamp(files[i])},
                       {"output", output}});
  }
  manifest["files"] = written;

  // Keep manifest.json (and its mtime) stable when nothing changed: the merge resume stamp is
  // "<size>:<mtime>" of this file, so rewriting an identical manifest would invalidate every
  // partial after a no-op re-run of the file step.
  const std::string manifestPath = base + "/manifest.json";
  const std::string content      = manifest.dump(2) + "\n";
  {
    std::ifstream in(manifestPath);
    if (in) {
      std::stringstream existing;
      existing << in.rdbuf();
      if (existing.str() == content) return true;
    }
  }

  const std::string tmpPath = manifestPath + ".tmp";
  std::ofstream     out(tmpPath.c_str());
  if (!out) {
    NLogError("NFileToNGnTree: cannot write '%s'", tmpPath.c_str());
    return false;
  }
  out << content;
  out.close();
  if (gSystem->Rename(tmpPath.c_str(), manifestPath.c_str()) != 0) {
    NLogError("NFileToNGnTree: cannot rename '%s' to '%s'", tmpPath.c_str(), manifestPath.c_str());
    gSystem->Unlink(tmpPath.c_str());
    return false;
  }
  return true;
}

// Copy every available merged object from its partial into one combined file, preserving the
// source directory layout. The partials are left in place (they are the resumable results).
static int NUtilsCombinePartials(const std::vector<NUtilsMergeObject> & objects, const std::string & outFile)
{
  const std::string final = NUtilsCombinedPath(outFile);

  TFile * out = NUtils::OpenFile(final, "RECREATE");
  if (!out || out->IsZombie()) {
    NLogError("NGnTreeMerge: cannot create combined file '%s'", final.c_str());
    delete out;
    return 0;
  }

  int            copied = 0;
  const Long64_t total  = (Long64_t)objects.size();
  Long64_t       index  = 0;

  for (const auto & object : objects) {
    ++index;
    const std::string partial = NUtilsPartialPath(outFile, object.name);
    if (!NUtils::AccessPathName(partial)) {
      NLogWarning("NGnTreeMerge: combine [%lld/%lld] '%s' - partial '%s' is missing", index, total,
                  object.name.c_str(), partial.c_str());
      continue;
    }

    TFile * in = NUtils::OpenFile(partial, "READ");
    if (!in || in->IsZombie()) {
      NLogWarning("NGnTreeMerge: combine [%lld/%lld] cannot open '%s'", index, total, partial.c_str());
      delete in;
      continue;
    }

    const std::string        objectPath = NUtilsObjectPath(object.name);
    std::vector<std::string> parts      = NUtilsSplitPath(objectPath);
    TObject *                obj        = parts.empty() ? nullptr : in->Get(objectPath.c_str());
    if (obj == nullptr || parts.empty()) {
      NLogWarning("NGnTreeMerge: combine [%lld/%lld] no object at '%s' in '%s'", index, total, objectPath.c_str(),
                  partial.c_str());
      in->Close();
      delete in;
      continue;
    }

    TDirectory * dir = out;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
      TDirectory * sub = dir->GetDirectory(parts[i].c_str());
      if (sub == nullptr) sub = dir->mkdir(parts[i].c_str());
      if (sub == nullptr) break;
      dir = sub;
    }

    dir->cd();
    obj->Write(parts.back().c_str(), TObject::kOverwrite);
    ++copied;
    NLogInfo("NGnTreeMerge: combine [%lld/%lld] '%s'", index, total, object.name.c_str());

    in->Close();
    delete in;
  }

  out->Close();
  delete out;

  if (copied == 0) {
    if (NUtils::AccessPathName(final)) gSystem->Unlink(final.c_str());
    NLogWarning("NGnTreeMerge: nothing combined - '%s' not written", final.c_str());
    return 0;
  }

  NLogInfo("NGnTreeMerge: combined %d object(s) into '%s'", copied, final.c_str());
  return copied;
}

bool NUtils::NGnTreeMerge(const json & cfgIn, const std::string & outFile, const std::string & objectsCsv,
                          bool deletePartials, const std::string & macroDir)
{
  json cfg = cfgIn;

  // Start from scratch: drop the requested objects' partials and the combined file first.
  if (cfg.value("reset", false)) deletePartials = true;

  std::vector<std::string> objects = cfg.value("objects", std::vector<std::string>{});
  for (const auto & o : NUtilsSplitCsv(objectsCsv)) {
    if (std::find(objects.begin(), objects.end(), o) == objects.end()) {
      objects.push_back(o);
      NLogInfo("NGnTreeMerge: adding object '%s' from the command line", o.c_str());
    }
  }

  if (objects.empty()) {
    NLogError("NGnTreeMerge: no objects to merge (config 'objects' is empty)");
    return false;
  }

  NLogInfo("NGnTreeMerge: %zu object(s) requested:", objects.size());
  for (size_t i = 0; i < objects.size(); i++) {
    NLogInfo("NGnTreeMerge:   '%s'", objects[i].c_str());
  }

  std::string inputFile = NUtilsResolveInputPath(cfg.value("inputFile", std::string("merging_ngnt.root")), macroDir);
  std::string outBase   = outFile.empty() ? cfg.value("outFile", std::string("merged_ngnt.root")) : outFile;

  std::vector<std::string> mergeTypes = cfg.value("mergeTypes", std::vector<std::string>{"TH1", "THnBase"});
  const std::string        onUnsupported = cfg.value("onUnsupported", std::string("keepFirst"));
  const bool               force         = cfg.value("force", false);
  const bool               combine       = cfg.value("combine", true);
  const int                processes     = cfg.value("processes", 1);
  const std::string        mode          = cfg.value("mode", std::string("ipc"));
  const int                port          = cfg.value("port", 5555);

  // Split input: `inputFile` is a directory holding the manifest written by FileToNGnTree.
  bool              inputSplit   = cfg.value("inputSplit", false);
  const std::string manifestPath = inputFile + "/manifest.json";
  if (!inputSplit && NUtils::IsSplitBase(inputFile)) {
    inputSplit = true;
    NLogInfo("NGnTreeMerge: input '%s' is a split output (manifest found)", inputFile.c_str());
  }

  if (inputSplit && !NUtils::IsSplitBase(inputFile)) {
    NLogError("NGnTreeMerge: inputSplit is set but '%s' is not a split output (manifest.json missing)",
              inputFile.c_str());
    return false;
  }
  if (!inputSplit && NUtilsIsLocalDirectory(inputFile)) {
    NLogError("NGnTreeMerge: input '%s' is a directory without manifest.json - not a split output; "
              "run the 'file' step first or pass an NGnTree file", inputFile.c_str());
    return false;
  }

  std::vector<NUtilsMergeObject> found;
  json                           splitFiles   = json::array();
  json                           splitObjects = json::object();

  if (inputSplit) {
    json manifest;
    if (!LoadJsonFile(manifest, manifestPath)) {
      NLogError("NGnTreeMerge: cannot read '%s'", manifestPath.c_str());
      return false;
    }

    struct SplitObject {
      std::string branch;
      std::string path;
      std::string cls;
      Long64_t    bytes{0};
      json        chunks;
    };
    std::vector<SplitObject> available;
    for (const auto & o : manifest.value("objects", json::array())) {
      if (!o.is_object()) continue;
      available.push_back({o.value("branch", ""), o.value("path", ""), o.value("class", ""),
                           o.value("bytes", (Long64_t)0), o.value("chunks", json())});
    }

    for (const auto & pattern : objects) {
      const bool glob    = (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos);
      bool       matched = false;

      // Accept both notations: source path ("Data/h1") and dotted branch ("Data.h1").
      std::string dottedPattern = pattern;
      std::replace(dottedPattern.begin(), dottedPattern.end(), '/', '.');

      for (const auto & a : available) {
        const bool hit = glob ? (NUtilsGlobMatch(a.branch, pattern) || NUtilsGlobMatch(a.branch, dottedPattern) ||
                                 NUtilsGlobMatch(a.path, pattern))
                              : (a.branch == pattern || a.branch == dottedPattern || a.path == pattern);
        if (!hit) continue;
        matched = true;

        const bool chunked = a.chunks.is_object() && a.chunks.contains("groups") && !a.chunks["groups"].empty();
        if (chunked) {
          const size_t nGroups = a.chunks["groups"].size();
          for (const auto & g : a.chunks["groups"]) {
            const std::string label    = g.value("label", std::string());
            const std::string itemName = a.branch + ".chunk_" + label;

            bool exists = false;
            for (const auto & f : found) {
              if (f.name == itemName) {
                exists = true;
                break;
              }
            }
            if (exists) continue;

            NUtilsMergeObject item;
            item.name       = itemName;
            item.cls        = a.cls;
            item.bytes      = a.bytes / (Long64_t)std::max<size_t>(1, nGroups);
            item.objectPath = a.path;
            item.chunkFiles = g.value("files", json::array());
            item.chunkAxes  = a.chunks.value("axes", json::array());
            item.chunkRange = g.value("range", std::string());
            found.push_back(item);
          }
        }
        else {
          bool exists = false;
          for (const auto & f : found) {
            if (f.name == a.branch) {
              exists = true;
              break;
            }
          }
          if (!exists) found.push_back({a.branch, a.cls, a.bytes, a.path, json::array(), json::array(), ""});
        }
      }

      if (!matched) {
        NLogWarning("NGnTreeMerge: object '%s' not found in '%s' - skipped", pattern.c_str(), inputFile.c_str());
      }
    }

    splitFiles = manifest.value("files", json::array());
    if (splitFiles.empty()) NLogWarning("NGnTreeMerge: '%s' lists no files", manifestPath.c_str());
  }
  else {
    found = NUtilsCollectMergeObjects(inputFile, objects);
  }

  if (found.empty()) {
    NLogError("NGnTreeMerge: none of the requested objects exists in '%s'", inputFile.c_str());
    return false;
  }

  // Blacklist: drop anything matching an excludeObjects pattern (same glob syntax as objects).
  // Chunked items are matched by their base object too, so excluding the object removes all chunks.
  const std::vector<std::string> excludeObjects = cfg.value("excludeObjects", std::vector<std::string>{});
  if (!excludeObjects.empty()) {
    std::vector<NUtilsMergeObject> kept;
    kept.reserve(found.size());

    for (const auto & item : found) {
      std::string base = item.name;
      const size_t chunkPos = base.find(".chunk_");
      if (chunkPos != std::string::npos) base.erase(chunkPos);

      bool excluded = false;
      for (const auto & pattern : excludeObjects) {
        if (NUtilsGlobMatch(base, pattern) || NUtilsGlobMatch(item.name, pattern) ||
            (!item.objectPath.empty() && NUtilsGlobMatch(item.objectPath, pattern))) {
          excluded = true;
          NLogInfo("NGnTreeMerge: excluding '%s' (matches excludeObjects '%s')", item.name.c_str(),
                   pattern.c_str());
          break;
        }
      }
      if (!excluded) kept.push_back(item);
    }

    found.swap(kept);
    if (found.empty()) {
      NLogError("NGnTreeMerge: all requested objects are excluded by excludeObjects");
      return false;
    }
  }

  // Smallest first: the heavy objects (large THnSparse) run last, so the long tail of the run
  // is predictable and the quick results are available early. Size is the stored/branch size;
  // no object is read to estimate it.
  std::stable_sort(found.begin(), found.end(), [](const NUtilsMergeObject & a, const NUtilsMergeObject & b) {
    if (a.bytes == 0) return false; // unknown size: keep at the end
    if (b.bytes == 0) return true;
    return a.bytes < b.bytes;
  });

  // Per-item source path and chunk file list (chunked objects were expanded into items).
  json chunkFilesByItem = json::object();
  json chunkAxesByItem  = json::object();
  json chunkRangesByItem = json::object();
  for (const auto & item : found) {
    splitObjects[item.name] = item.objectPath.empty() ? NUtilsObjectPath(item.name) : item.objectPath;
    if (!item.chunkFiles.empty()) chunkFilesByItem[item.name] = item.chunkFiles;
    if (!item.chunkAxes.empty()) chunkAxesByItem[item.name] = item.chunkAxes;
    if (!item.chunkRange.empty()) chunkRangesByItem[item.name] = item.chunkRange;
  }

  NLogInfo("NGnTreeMerge: %zu found, %zu missing - to process (smallest first):", found.size(),
           objects.size() > found.size() ? objects.size() - found.size() : 0);

  size_t chunkedCount = 0;
  for (const auto & item : found) {
    if (!item.chunkFiles.empty()) ++chunkedCount;
  }
  if (chunkedCount > 0) {
    NLogInfo("NGnTreeMerge: %zu of them are chunked (one result per chunk)", chunkedCount);
  }
  for (size_t i = 0; i < found.size(); i++) {
    const NUtilsMergeObject & item = found[i];
    const size_t              pos  = item.name.find(".chunk_");
    if (pos != std::string::npos) {
      const std::string range = item.chunkRange.empty() ? "" : " (" + item.chunkRange + ")";
      NLogInfo("NGnTreeMerge:   [%zu/%zu] '%s' chunk %s%s [%s] (%s, %zu file(s))", i + 1, found.size(),
               item.name.substr(0, pos).c_str(), item.name.substr(pos + 7).c_str(), range.c_str(),
               item.cls.c_str(), NUtilsFormatBytes(item.bytes).c_str(), item.chunkFiles.size());
    }
    else {
      NLogInfo("NGnTreeMerge:   [%zu/%zu] '%s' [%s] (%s)", i + 1, found.size(), item.name.c_str(), item.cls.c_str(),
               NUtilsFormatBytes(item.bytes).c_str());
    }
  }

  // Force a fresh run: drop the requested objects' partials and the combined file.
  if (deletePartials) {
    NLogInfo("NGnTreeMerge: deleting existing partials for the requested objects");
    for (const auto & object : found) {
      const std::string partial = NUtilsPartialPath(outBase, object.name);
      const std::string part    = partial + ".part";
      for (const auto & path : {part, partial}) {
        if (NUtils::AccessPathName(path)) {
          NLogInfo("NGnTreeMerge:   removed '%s'", path.c_str());
          gSystem->Unlink(path.c_str());
        }
      }
    }

    const std::string final = NUtilsCombinedPath(outBase);
    if (NUtils::AccessPathName(final)) {
      NLogInfo("NGnTreeMerge:   removed '%s'", final.c_str());
      gSystem->Unlink(final.c_str());
    }
  }

  const std::string inputStamp = inputSplit ? NUtilsInputStamp(manifestPath) : NUtilsInputStamp(inputFile);

  // Execution: one process per worker, ipc by default; overridable in the config and by env.
  if (!gSystem->Getenv("NDMSPC_EXECUTION_MODE")) gSystem->Setenv("NDMSPC_EXECUTION_MODE", mode.c_str());
  if (!gSystem->Getenv("NDMSPC_MAX_PROCESSES")) {
    std::string n = std::to_string(processes);
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", n.c_str());
  }
  if (mode == "tcp" && !gSystem->Getenv("NDMSPC_TCP_PORT")) {
    std::string p = std::to_string(port);
    gSystem->Setenv("NDMSPC_TCP_PORT", p.c_str());
  }
  NLogInfo("NGnTreeMerge: execution mode '%s', %d process(es)", mode.c_str(), processes);

  // Merging is slow and uneven per object: give each worker exactly one bin at a time, so the
  // next bin always goes to whichever worker finishes first.
  gSystem->Setenv("NDMSPC_IPC_BATCH_SIZE", "1");
  gSystem->Setenv("NDMSPC_IPC_MAX_INFLIGHT_PER_WORKER", "1");
  NLogInfo("NGnTreeMerge: one bin per worker (batch size 1, max 1 in flight)");

  if (gSystem->Getenv("NDMSPC_IPC_STALL_TIMEOUT") == nullptr) {
    std::string stall = std::to_string(cfg.value("stallTimeout", 3600));
    gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", stall.c_str());
    NLogInfo("NGnTreeMerge: IPC stall timeout set to %s s", stall.c_str());
  }

  // Everything the bins need, in the config that reaches the worker through point->GetCfg().
  json strategies  = json::object();
  json indices     = json::object();
  json classes     = json::object();
  json objectBytes = json::object();
  for (size_t i = 0; i < found.size(); i++) {
    strategies[found[i].name] = NUtilsStrategyName(NUtilsMergeStrategyFor(found[i].cls, mergeTypes));
    indices[found[i].name]    = (int)(i + 1);
    classes[found[i].name]    = found[i].cls;
    objectBytes[found[i].name] = found[i].bytes;
  }

  cfg["_inputPath"]     = inputFile;
  cfg["_outFile"]       = outBase;
  cfg["_force"]         = force;
  cfg["_onUnsupported"] = onUnsupported;
  cfg["_provenance"]    = inputStamp;
  cfg["_strategies"]    = strategies;
  cfg["_classes"]       = classes;
  cfg["_index"]         = indices;
  cfg["_count"]         = (int)found.size();
  cfg["_inputSplit"]    = inputSplit;
  cfg["_splitFiles"]    = splitFiles;
  cfg["_splitObjects"]  = splitObjects;
  cfg["_chunkFiles"]    = chunkFilesByItem;
  cfg["_chunkAxesByItem"] = chunkAxesByItem;
  cfg["_chunkRanges"]   = chunkRangesByItem;
  cfg["_objectBytes"]   = objectBytes;
  cfg["_serializeBytes"] = cfg.value("serializeBytes", (Long64_t)0);
  cfg["_lockPath"]      = outBase + ".merge.lock";

  // Resume: when every requested object already has a complete partial, skip the merge phase.
  bool     allComplete   = !force;
  Long64_t newestPartial = 0;
  for (const auto & object : found) {
    const std::string partial = NUtilsPartialPath(outBase, object.name);
    const std::string path    = splitObjects.contains(object.name) ? splitObjects[object.name].get<std::string>()
                                                                   : NUtilsObjectPath(object.name);
    if (!NUtilsPartialIsComplete(partial, path, inputStamp + "|" + object.name)) {
      allComplete = false;
      break;
    }
    FileStat_t st;
    if (gSystem->GetPathInfo(partial.c_str(), st) == 0) newestPartial = std::max(newestPartial, (Long64_t)st.fMtime);
  }
  if (allComplete) {
    NLogInfo("NGnTreeMerge: all %zu object(s) already have complete partials - skipping the merge phase",
             found.size());
  }

  // One bin per object; `{{1}}` keeps every base bin.
  std::vector<std::string> names;
  for (const auto & object : found) names.push_back(object.name);

  TObjArray * axes = new TObjArray();
  axes->Add(CreateAxisFromLabels("object", "Object", names));

  // The run's own output tree is not used (bins write their partials directly); keep it in a
  // temporary file and remove it afterwards.
  const std::string runOut = outBase + ".run";
  if (NUtils::AccessPathName(runOut)) gSystem->Unlink(runOut.c_str());

  NGnTree * ngnt = new NGnTree(axes, runOut);

  std::map<std::string, std::vector<std::vector<int>>> binsMap;
  binsMap["object"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", binsMap);

  NGnProcessFuncPtr processFunc = [](NBinningPoint * point, TList * /*output*/, TList * /*outputPoint*/,
                                     int /*threadId*/) {
    if (!point) {
      NLogError("NGnTreeMerge: binning point is nullptr");
      return;
    }

    const json & c    = point->GetCfg();
    std::string  name = point->GetBinLabel("object");
    if (name.empty()) return;

    const json        indices = c.value("_index", json::object());
    const int         index   = indices.value(name, 0);
    const int         count   = c.value("_count", 0);
    const std::string where   = "[" + std::to_string(index) + "/" + std::to_string(count) + "]";

    const std::string outFile = c.value("_outFile", std::string());
    const json        paths   = c.value("_splitObjects", json::object());
    // Chunk items keep the original object path inside their files; plain items use their name.
    const std::string objectPath =
        paths.contains(name) ? paths[name].get<std::string>() : NUtilsObjectPath(name);
    const std::string partial    = NUtilsPartialPath(outFile, name);
    const std::string provTitle  = c.value("_provenance", std::string()) + "|" + name;
    const bool        force      = c.value("_force", false);

    const json        strategies = c.value("_strategies", json::object());
    const std::string strategy   = strategies.value(name, std::string("Unsupported"));
    const json        classes    = c.value("_classes", json::object());
    const std::string objClass   = classes.value(name, std::string("?"));

    const json        chunkRanges = c.value("_chunkRanges", json::object());
    const std::string chunkRange  = chunkRanges.value(name, std::string());

    NLogInfo("NGnTreeMerge: %s merging object '%s'%s [%s] using [%s]", where.c_str(), name.c_str(),
             chunkRange.empty() ? "" : (" (" + chunkRange + ")").c_str(), objClass.c_str(), strategy.c_str());

    if (!force && NUtilsPartialIsComplete(partial, objectPath, provTitle)) {
      NLogInfo("NGnTreeMerge: %s skip '%s' (partial '%s' is complete)", where.c_str(), name.c_str(),
               partial.c_str());
      return;
    }

    if (strategy == "Unsupported") {
      const std::string policy = c.value("_onUnsupported", std::string("keepFirst"));
      NLogWarning("NGnTreeMerge: %s no merge strategy for '%s' in mergeTypes", where.c_str(), name.c_str());
      if (policy == "skip") {
        NLogInfo("NGnTreeMerge: %s skipping '%s' (onUnsupported=skip)", where.c_str(), name.c_str());
        return;
      }
      if (policy == "fail") {
        NLogError("NGnTreeMerge: %s cannot merge '%s' (onUnsupported=fail)", where.c_str(), name.c_str());
        return;
      }
    }

    // Heavy objects are merged one at a time across worker processes, so two multi-GB merged
    // sparses never exist at the same time. `bytes` is the stored-size estimate.
    const json     objectBytes    = c.value("_objectBytes", json::object());
    const Long64_t objBytes       = objectBytes.value(name, (Long64_t)0);
    const Long64_t serializeBytes = c.value("_serializeBytes", (Long64_t)0);
    const bool     heavy          = serializeBytes > 0 && objBytes >= serializeBytes;

    NUtilsFileLock heavyLock;
    if (heavy) {
      NLogInfo("NGnTreeMerge: %s '%s' is heavy (%s stored) - merging it alone", where.c_str(), name.c_str(),
               NUtilsFormatBytes(objBytes).c_str());
      if (!heavyLock.acquire(c.value("_lockPath", std::string()))) {
        NLogWarning("NGnTreeMerge: %s could not take the heavy-object lock - continuing", where.c_str());
      }
    }

    auto mergeOne = [&strategy](TObject * into, TObject * from) -> bool {
      if (strategy == "TH1") {
        auto * a = dynamic_cast<TH1 *>(into);
        auto * b = dynamic_cast<TH1 *>(from);
        if (a && b) {
          a->Add(b);
          return true;
        }
        return false;
      }
      if (strategy == "THnBase") {
        auto * a = dynamic_cast<THnBase *>(into);
        auto * b = dynamic_cast<THnBase *>(from);
        if (a && b) {
          a->Add(b);
          return true;
        }
        return false;
      }
      if (strategy == "TEfficiency") {
        auto * a = dynamic_cast<TEfficiency *>(into);
        auto * b = dynamic_cast<TEfficiency *>(from);
        if (a && b) {
          a->Add(*b);
          return true;
        }
        return false;
      }
      if (strategy == "TList") {
        auto * a = dynamic_cast<TList *>(into);
        auto * b = dynamic_cast<TList *>(from);
        if (a && b) {
          NUtilsMergeListInto(a, b);
          return true;
        }
        return false;
      }
      return false;
    };

    TObject * merged      = nullptr;
    TFile *   mergedFile  = nullptr; // first input file, kept open while its object is the accumulator
    Long64_t  mergedFiles = 0;
    bool      unsupported = false;
    Long64_t  nEntries    = 0;

    TStopwatch watch;
    watch.Start();

    if (c.value("_inputSplit", false)) {
      // Split input: the object is stored, per input file, at its source path in its own file.
      // Chunked objects use the per-item chunk file list from the manifest.
      const json chunkMap = c.value("_chunkFiles", json::object());
      const json files    = chunkMap.contains(name) ? chunkMap[name] : c.value("_splitFiles", json::array());
      const json        paths = c.value("_splitObjects", json::object());
      const std::string aPath = paths.value(name, std::string());

      nEntries   = (Long64_t)files.size();
      Long64_t k = 0;

      for (const auto & f : files) {
        ++k;
        const std::string path = f.value("output", std::string());
        NLogInfo("NGnTreeMerge: %s '%s' [%s] file %lld/%lld: %s (rss %.0f MB)", where.c_str(), name.c_str(),
                 objClass.c_str(), k, nEntries, path.c_str(), NUtilsRssMB());
        if (aPath.empty()) continue;

        TFile * entryFile = NUtils::OpenFile(path, "READ");
        if (!entryFile || entryFile->IsZombie()) {
          NLogWarning("NGnTreeMerge: %s cannot open '%s'", where.c_str(), path.c_str());
          delete entryFile;
          continue;
        }

        TObject *      obj     = entryFile->Get(aPath.c_str());
        const Long64_t srcBins = NUtilsObjectBins(obj);

        TStopwatch addWatch;
        addWatch.Start();

        if (obj != nullptr) {
          if (!merged) {
            // Use the first file's object directly: no clone. The file owns it, so keep that
            // file open until the partial has been written.
            merged     = obj;
            mergedFile = entryFile;
            entryFile  = nullptr;
            ++mergedFiles;
          }
          else if (mergeOne(merged, obj)) {
            ++mergedFiles;
          }
          else {
            unsupported = true;
          }
        }

        addWatch.Stop();
        if (entryFile != nullptr) {
          entryFile->Close();
          delete entryFile;
        }

        NLogDebug("NGnTreeMerge: %s '%s' file %lld/%lld done: %s bins in source, merged now %s bins in %.2f s",
                  where.c_str(), name.c_str(), k, nEntries, NUtilsFormatCount(srcBins).c_str(),
                  NUtilsFormatCount(NUtilsObjectBins(merged)).c_str(), addWatch.RealTime());
      }
    }
    else {
      // Input tree: opened once per worker, closed in the end function.
      TFile * in = dynamic_cast<TFile *>(point->GetTempObject("input"));
      if (!in) {
        in = NUtils::OpenFile(c.value("_inputPath", std::string()));
        if (!in || in->IsZombie()) {
          NLogError("NGnTreeMerge: %s cannot open input file", where.c_str());
          if (in) delete in;
          return;
        }
        point->SetTempObject("input", in);
      }

      TTree * tree = dynamic_cast<TTree *>(in->Get("ngnt"));
      if (!tree) {
        NLogError("NGnTreeMerge: %s input file has no 'ngnt' tree", where.c_str());
        return;
      }

      // Progress: FileToNGnTree stored the source file name in the entry's `_outputPoint` log.
      TList *   entryLog   = nullptr;
      TBranch * entryLogBr = tree->GetBranch("_outputPoint");
      if (entryLogBr != nullptr) tree->SetBranchAddress("_outputPoint", &entryLog);

      // Read only this object's branch. SetBranchStatus("*", 0) is deliberately not used: for
      // complex object branches (e.g. THnSparse) it leaves the object's sub-branches disabled,
      // so the object reads back empty; reading the TBranch directly is exact and just as cheap.
      TBranch * objBranch = tree->GetBranch(name.c_str());
      if (!objBranch) {
        NLogError("NGnTreeMerge: %s branch '%s' disappeared from the input tree", where.c_str(), name.c_str());
        return;
      }

      TObject * obj = nullptr;
      tree->SetBranchAddress(name.c_str(), &obj);

      nEntries = tree->GetEntries();
      for (Long64_t i = 0; i < nEntries; ++i) {
        objBranch->GetEntry(i);

        if (entryLogBr != nullptr) {
          entryLogBr->GetEntry(i);

          std::string source;
          if (entryLog != nullptr) {
            if (auto * log = dynamic_cast<NLogString *>(entryLog->FindObject("log"))) source = log->GetString();
          }
          source = source.substr(0, source.find('\n')); // the file name is the first line
          NLogInfo("NGnTreeMerge: %s '%s' [%s] file %lld/%lld:%s%s (rss %.0f MB)", where.c_str(), name.c_str(),
                   objClass.c_str(), i + 1, nEntries, source.empty() ? "" : " ", source.c_str(), NUtilsRssMB());
        }

        if (!obj) continue;

        if (!merged) {
          merged = obj->Clone();
          ++mergedFiles;
          continue;
        }

        if (mergeOne(merged, obj)) ++mergedFiles;
        else unsupported = true;
      }
    }

    if (!merged) {
      NLogWarning("NGnTreeMerge: %s no data for object '%s'", where.c_str(), name.c_str());
      return;
    }
    if (unsupported) {
      NLogWarning("NGnTreeMerge: %s object '%s' (%s) cannot be merged [%s] - keeping the first entry", where.c_str(),
                  name.c_str(), merged->IsA()->GetName(), strategy.c_str());
    }

    const bool written =
        NUtilsWriteObjectToPartial(partial, objectPath, merged, c.value("_inputPath", std::string()), provTitle);
    if (written) {
      NLogInfo("NGnTreeMerge: %s '%s' merged %lld/%lld file(s) [%s] in %.1f s -> '%s' (rss %.0f MB)", where.c_str(),
               name.c_str(), mergedFiles, nEntries, strategy.c_str(), watch.RealTime(), partial.c_str(),
               NUtilsRssMB());
    }
    else {
      NLogError("NGnTreeMerge: %s could not write '%s'", where.c_str(), partial.c_str());
    }

    // Not kept in an output list: the partial is the result, so the memory is released at once.
    if (mergedFile != nullptr) {
      // The accumulator still belongs to the first input file; closing it frees the object.
      mergedFile->Close();
      delete mergedFile;
      mergedFile = nullptr;
    }
    else {
      delete merged;
    }

    if (heavy) NUtilsTrimHeap();
  };

  NGnBeginFuncPtr beginFunc = [](NBinningPoint * /*point*/, int /*threadId*/) { DisableObjectAutoRegistration(); };

  NGnEndFuncPtr endFunc = [](NBinningPoint * point, int /*threadId*/) {
    TFile * in = dynamic_cast<TFile *>(point->GetTempObject("input"));
    if (in) {
      in->Close();
      delete in;
      point->SetTempObject("input", nullptr);
    }
  };

  bool rc = true;
  if (!allComplete) {
    rc = ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);
    if (rc) {
      NLogInfo("NGnTreeMerge: processing completed successfully");
    }
    else {
      NLogError("NGnTreeMerge: processing failed");
    }
  }

  delete ngnt;
  if (NUtils::AccessPathName(runOut)) gSystem->Unlink(runOut.c_str());

  // The lock file is only a rendezvous point for the workers; remove it once the run is over.
  const std::string lockPath = outBase + ".merge.lock";
  if (NUtils::AccessPathName(lockPath)) gSystem->Unlink(lockPath.c_str());

  int missing = 0;
  const json objectPaths = cfg.value("_splitObjects", json::object());
  for (const auto & object : found) {
    const std::string partial = NUtilsPartialPath(outBase, object.name);
    const std::string path    = objectPaths.contains(object.name) ? objectPaths[object.name].get<std::string>()
                                                                  : NUtilsObjectPath(object.name);
    if (!NUtilsPartialIsComplete(partial, path, inputStamp + "|" + object.name)) {
      ++missing;
    }
  }
  if (missing != 0) NLogError("NGnTreeMerge: %d object(s) have no complete partial", missing);

  // Plain objects go into the combined file. Chunked objects keep one file per chunk (a
  // directory per object), unless combineChunks asks for the chunks to be added back together.
  std::vector<NUtilsMergeObject>                                plain;
  std::map<std::string, std::vector<const NUtilsMergeObject *>> chunked;
  for (const auto & object : found) {
    const std::string marker = ".chunk_";
    const size_t      pos    = object.name.find(marker);
    if (pos == std::string::npos) plain.push_back(object);
    else chunked[object.name.substr(0, pos)].push_back(&object);
  }

  const bool  combineChunks = cfg.value("combineChunks", false);
  std::string outBaseNoRoot = outBase;
  if (outBaseNoRoot.size() > 5 && outBaseNoRoot.compare(outBaseNoRoot.size() - 5, 5, ".root") == 0) {
    outBaseNoRoot.erase(outBaseNoRoot.size() - 5);
  }

  // A resumed run must not repeat the combine stage when every partial is complete and the
  // combined outputs are newer than the partials.
  bool combineUpToDate = false;
  if (allComplete && combine) {
    combineUpToDate = true;

    if (!plain.empty()) {
      const std::string final = NUtilsCombinedPath(outBase);
      FileStat_t        st;
      if (!NUtils::AccessPathName(final) || gSystem->GetPathInfo(final.c_str(), st) != 0 ||
          (Long64_t)st.fMtime < newestPartial) {
        combineUpToDate = false;
      }
    }

    if (combineUpToDate && !chunked.empty()) {
      const std::string sidecar = outBaseNoRoot + ".chunks.json";
      FileStat_t        st;
      if (!NUtils::AccessPathName(sidecar) || gSystem->GetPathInfo(sidecar.c_str(), st) != 0 ||
          (Long64_t)st.fMtime < newestPartial) {
        combineUpToDate = false;
      }

      for (const auto & [objName, items] : chunked) {
        if (!combineUpToDate) break;
        for (const auto * item : items) {
          const std::string label = item->name.substr(item->name.find(".chunk_") + 7);
          const std::string dest  = combineChunks ? (outBaseNoRoot + "." + objName + ".root")
                                                  : (outBaseNoRoot + "." + objName + "/chunk_" + label + ".root");
          if (!NUtils::AccessPathName(dest)) {
            combineUpToDate = false;
            break;
          }
        }
      }
    }
  }

  if (combine && !combineUpToDate) {
    if (!plain.empty()) {
      NLogInfo("NGnTreeMerge: combining partials into the final file");
      NUtilsCombinePartials(plain, outBase);
    }

    if (!chunked.empty()) {
      json sideObjects = json::object();
      for (const auto & [objName, items] : chunked) {
        json        sideChunks = json::array();
        std::string objectPath;
        json        axes;
        TObject *   combined = nullptr;

        for (const auto * item : items) {
          const std::string partial = NUtilsPartialPath(outBase, item->name);
          const std::string label   = item->name.substr(item->name.find(".chunk_") + 7);
          objectPath                = item->objectPath;
          axes                      = item->chunkAxes;

          if (combineChunks) {
            TFile * in = NUtils::OpenFile(partial, "READ");
            if (in && !in->IsZombie()) {
              TObject * o = in->Get(objectPath.c_str());
              if (o != nullptr) {
                if (combined == nullptr) {
                  combined = o->Clone();
                }
                else if (auto * a = dynamic_cast<THnBase *>(combined)) {
                  if (auto * b = dynamic_cast<THnBase *>(o)) a->Add(b);
                }
              }
            }
            if (in) {
              in->Close();
              delete in;
            }
          }
          else {
            if (!NUtils::AccessPathName(partial)) continue;

            const std::string dir = outBaseNoRoot + "." + objName;
            gSystem->mkdir(dir.c_str(), kTRUE);
            const std::string dest = dir + "/chunk_" + label + ".root";
            TFile::Cp(partial.c_str(), dest.c_str(), kFALSE);

            json chunkEntry = {{"label", label}, {"file", dest}};
            if (!item->chunkRange.empty()) chunkEntry["range"] = item->chunkRange;
            sideChunks.push_back(chunkEntry);
          }
        }

        if (combineChunks && combined != nullptr) {
          const std::string dest = outBaseNoRoot + "." + objName + ".root";
          if (NUtilsWriteObjectToPartial(dest, objectPath, combined, inputFile, inputStamp + "|" + objName)) {
            sideChunks.push_back({{"label", "combined"}, {"file", dest}});
          }
          delete combined;
        }

        sideObjects[objName] = {{"path", objectPath}, {"axes", axes}, {"chunks", sideChunks}};
      }

      json map;
      map["objects"] = sideObjects;
      std::ofstream side((outBaseNoRoot + ".chunks.json").c_str());
      if (side) {
        side << map.dump(2) << "\n";
        NLogInfo("NGnTreeMerge: chunk mapping written to '%s.chunks.json'", outBaseNoRoot.c_str());
      }
    }
  }
  else if (combine) {
    NLogInfo("NGnTreeMerge: all objects are complete and the combined output is up to date - skipping combine");
  }
  else {
    NLogInfo("NGnTreeMerge: 'combine' is off - partials only, no combined file");
  }

  return rc;
}

// ---------------------------------------------------------------------------
// NGnTreeBinning: gather selected THnSparse objects, apply several binnings and project each
// object to one observable axis per bin (generalised NAliRsnStep1.C).
// ---------------------------------------------------------------------------

static std::string NUtilsLeafName(const std::string & path)
{
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool NUtilsBinningExcluded(const json & exclude, const std::string & name, const std::string & path)
{
  std::string dotted = path;
  std::replace(dotted.begin(), dotted.end(), '/', '.');

  for (const auto & e : exclude) {
    if (!e.is_string()) continue;
    const std::string pattern = e.get<std::string>();
    if (NUtilsGlobMatch(name, pattern) || NUtilsGlobMatch(path, pattern) || NUtilsGlobMatch(dotted, pattern)) {
      NLogInfo("NGnTreeBinning: excluding '%s' (matches excludeObjects '%s')", path.c_str(), pattern.c_str());
      return true;
    }
  }
  return false;
}

static bool NUtilsBinningMatches(const std::vector<std::string> & patterns, const std::string & name,
                                 const std::string & path)
{
  if (patterns.empty()) return true;

  std::string dotted = path;
  std::replace(dotted.begin(), dotted.end(), '/', '.');

  for (const auto & pattern : patterns) {
    if (NUtilsGlobMatch(name, pattern) || NUtilsGlobMatch(path, pattern) || NUtilsGlobMatch(dotted, pattern)) {
      return true;
    }
  }
  return false;
}

bool NUtils::NGnTreeBinning(const json & cfgIn, const std::string & macroDir)
{
  json cfg = cfgIn;

  const std::string input = NUtilsResolveInputPath(cfg.value("inputFile", std::string()), macroDir);
  if (input.empty()) {
    NLogError("NGnTreeBinning: no input file/directory (config 'inputFile')");
    return false;
  }

  const std::string        outFile         = cfg.value("outFile", std::string("binning_ngnt.root"));
  const std::string        objectDirectory = cfg.value("objectDirectory", std::string());
  std::vector<std::string> patterns        = cfg.value("objects", std::vector<std::string>{"*"});
  const json               exclude         = cfg.value("excludeObjects", json::array());

  struct ProjectObject {
    std::string              name;
    std::string              path;
    std::string              cls;
    std::vector<std::string> files;
  };

  std::vector<ProjectObject> objects;

  const std::string manifestPath = input + "/manifest.json";
  const bool        inputSplit   = NUtils::IsSplitBase(input);

  if (!inputSplit && NUtilsIsLocalDirectory(input)) {
    NLogError("NGnTreeBinning: input '%s' is a directory without manifest.json - not a split output; "
              "run the 'file' step first or pass a ROOT file", input.c_str());
    return false;
  }

  if (inputSplit) {
    json manifest;
    if (!LoadJsonFile(manifest, manifestPath)) {
      NLogError("NGnTreeBinning: cannot read '%s'", manifestPath.c_str());
      return false;
    }

    const json globalFiles = manifest.value("files", json::array());

    for (const auto & o : manifest.value("objects", json::array())) {
      if (!o.is_object()) continue;

      const std::string path = o.value("path", std::string());
      const std::string name = NUtilsLeafName(path.empty() ? o.value("branch", std::string()) : path);

      if (!NUtilsBinningMatches(patterns, name, path)) continue;
      if (NUtilsBinningExcluded(exclude, name, path)) continue;

      // Only sparse histograms are projected.
      const std::string cls = o.value("class", std::string());
      if (cls.find("THnSparse") == std::string::npos) continue;

      ProjectObject po{name, path, cls, {}};

      const json chunks = o.value("chunks", json());
      if (chunks.is_object() && chunks.contains("groups") && !chunks["groups"].empty()) {
        for (const auto & g : chunks["groups"]) {
          for (const auto & f : g.value("files", json::array())) {
            po.files.push_back(f.value("output", std::string()));
          }
        }
      }
      else {
        for (const auto & f : globalFiles) po.files.push_back(f.value("output", std::string()));
      }

      po.files.erase(std::remove(po.files.begin(), po.files.end(), std::string()), po.files.end());
      if (po.files.empty()) {
        NLogWarning("NGnTreeBinning: no files for '%s' - skipped", name.c_str());
        continue;
      }
      objects.push_back(po);
    }
  }
  else {
    TFile * f = NUtils::OpenFile(input);
    if (!f || f->IsZombie()) {
      NLogError("NGnTreeBinning: cannot open '%s'", input.c_str());
      if (f) delete f;
      return false;
    }

    TDirectory * dir = f;
    if (!objectDirectory.empty()) {
      dir = f->GetDirectory(objectDirectory.c_str());
      if (dir == nullptr) {
        NLogError("NGnTreeBinning: directory '%s' not found in '%s'", objectDirectory.c_str(), input.c_str());
        f->Close();
        delete f;
        return false;
      }
    }

    std::vector<std::pair<std::string, std::string>> found;
    NUtilsCollectMatching(dir, "", patterns.empty() ? std::vector<std::string>{"*"} : patterns, found);

    for (const auto & [rel, cls] : found) {
      if (cls.find("THnSparse") == std::string::npos) continue;

      const std::string path = objectDirectory.empty() ? rel : objectDirectory + "/" + rel;
      const std::string name = NUtilsLeafName(rel);
      if (NUtilsBinningExcluded(exclude, name, path)) continue;
      objects.push_back({name, path, cls, {input}});
    }

    f->Close();
    delete f;
  }

  // Parameter names must be unique.
  {
    std::set<std::string>      seen;
    std::vector<ProjectObject> unique;
    unique.reserve(objects.size());
    for (auto & o : objects) {
      if (!seen.insert(o.name).second) {
        NLogWarning("NGnTreeBinning: duplicate object name '%s' - skipped", o.name.c_str());
        continue;
      }
      unique.push_back(std::move(o));
    }
    objects.swap(unique);
  }

  if (objects.empty()) {
    NLogError("NGnTreeBinning: no objects selected in '%s'", input.c_str());
    return false;
  }

  // --- axes from the first object -------------------------------------------
  TFile * fa = NUtils::OpenFile(objects.front().files.front());
  if (!fa || fa->IsZombie()) {
    NLogError("NGnTreeBinning: cannot open '%s'", objects.front().files.front().c_str());
    if (fa) delete fa;
    return false;
  }

  THnSparse * hns0 = dynamic_cast<THnSparse *>(fa->Get(objects.front().path.c_str()));
  if (hns0 == nullptr) {
    NLogError("NGnTreeBinning: '%s' is not a THnSparse in '%s'", objects.front().path.c_str(),
              objects.front().files.front().c_str());
    fa->Close();
    delete fa;
    return false;
  }

  TObjArray *      axes = new TObjArray();
  std::vector<int> axisMap;

  for (const auto & a : cfg.value("axes", json::array())) {
    if (!a.is_object()) continue;

    const std::string name = a.value("name", std::string());
    int               src  = -1;

    if (a.contains("axis") && a["axis"].is_string()) {
      const std::string srcName = a["axis"].get<std::string>();
      for (int i = 0; i < hns0->GetNdimensions(); i++) {
        if (srcName == hns0->GetAxis(i)->GetName()) {
          src = i;
          break;
        }
      }
    }
    else {
      src = a.value("axis", -1);
    }

    if (src < 0 || src >= hns0->GetNdimensions()) {
      NLogError("NGnTreeBinning: source axis '%s' not found in '%s'",
                a.contains("axis") ? a["axis"].dump().c_str() : "?", objects.front().path.c_str());
      fa->Close();
      delete fa;
      return false;
    }

    TAxis * ax = (TAxis *)hns0->GetAxis(src)->Clone(name.empty() ? hns0->GetAxis(src)->GetName() : name.c_str());
    axes->Add(ax);
    axisMap.push_back(src);
  }

  if (axes->GetEntries() == 0) {
    for (int i = 0; i < hns0->GetNdimensions(); i++) {
      axes->Add(hns0->GetAxis(i)->Clone());
      axisMap.push_back(i);
    }
  }

  int projAxis = cfg.value("projectionAxis", 0);
  if (cfg.contains("projectionName") && cfg["projectionName"].is_string()) {
    const std::string pn = cfg["projectionName"].get<std::string>();
    for (int i = 0; i < hns0->GetNdimensions(); i++) {
      if (pn == hns0->GetAxis(i)->GetName()) {
        projAxis = i;
        break;
      }
    }
  }
  if (projAxis < 0 || projAxis >= hns0->GetNdimensions()) {
    NLogError("NGnTreeBinning: projection axis %d is out of range (0..%d)", projAxis, hns0->GetNdimensions() - 1);
    fa->Close();
    delete fa;
    return false;
  }

  fa->Close();
  delete fa;

  // --- NGnTree, binnings, parameters ---------------------------------------
  NGnTree * ngnt = new NGnTree(axes, outFile);
  if (!ngnt || ngnt->IsZombie()) {
    NLogError("NGnTreeBinning: cannot create '%s'", outFile.c_str());
    delete ngnt;
    return false;
  }

  const json binnings = cfg.value("binnings", json::object());
  int        defCount = 0;

  for (auto it = binnings.begin(); it != binnings.end(); ++it) {
    std::map<std::string, std::vector<std::vector<int>>> b;
    for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
      std::vector<std::vector<int>> widths;
      for (const auto & w : jt.value()) {
        std::vector<int> v;
        for (const auto & x : w) v.push_back(x.get<int>());
        widths.push_back(v);
      }
      b[jt.key()] = widths;
    }
    ngnt->GetBinning()->AddBinningDefinition(it.key(), b);
    ++defCount;
  }

  if (defCount == 0) {
    std::map<std::string, std::vector<std::vector<int>>> b;
    for (int i = 0; i < axes->GetEntries(); i++) b[((TAxis *)axes->At(i))->GetName()] = {{1}};
    ngnt->GetBinning()->AddBinningDefinition("default", b);
  }

  std::vector<std::string> names;
  names.reserve(objects.size());
  for (const auto & o : objects) names.push_back(o.name);
  ngnt->InitParameters(names);

  json objectsCfg = json::array();
  for (const auto & o : objects) {
    objectsCfg.push_back({{"name", o.name}, {"path", o.path}, {"files", o.files}});
  }

  cfg["_binningObjects"] = objectsCfg;
  cfg["_axisMap"]        = axisMap;
  cfg["_projectionAxis"] = projAxis;

  NLogInfo("NGnTreeBinning: input '%s'%s, %zu object(s), %zu axis/axes, projection axis %d", input.c_str(),
           inputSplit ? " (split)" : "", objects.size(), axisMap.size(), projAxis);
  for (const auto & o : objects) {
    NLogInfo("NGnTreeBinning:   '%s' <- %zu file(s) [%s]", o.name.c_str(), o.files.size(), o.path.c_str());
  }

  // Execution mode (same conventions as the merge step).
  const int         processes = cfg.value("processes", 1);
  const std::string mode      = cfg.value("mode", std::string("ipc"));
  const int         port      = cfg.value("port", 5555);
  if (!gSystem->Getenv("NDMSPC_EXECUTION_MODE")) gSystem->Setenv("NDMSPC_EXECUTION_MODE", mode.c_str());
  if (!gSystem->Getenv("NDMSPC_MAX_PROCESSES")) {
    std::string n = std::to_string(processes);
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", n.c_str());
  }
  if (mode == "tcp" && !gSystem->Getenv("NDMSPC_TCP_PORT")) {
    std::string p = std::to_string(port);
    gSystem->Setenv("NDMSPC_TCP_PORT", p.c_str());
  }
  if (gSystem->Getenv("NDMSPC_IPC_STALL_TIMEOUT") == nullptr) {
    std::string stall = std::to_string(cfg.value("stallTimeout", 3600));
    gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", stall.c_str());
  }

  NGnProcessFuncPtr processFunc = [](NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                     int /*threadId*/) {
    if (!point || !outputPoint) {
      NLogError("NGnTreeBinning: missing binning point/output");
      return;
    }

    const json & c = point->GetCfg();

    // NGnTree axis ranges -> original THnSparse axis indices.
    std::map<int, std::vector<int>> ranges;
    const json                      axisMap = c.value("_axisMap", json::array());
    for (const auto & [i, r] : point->GetBaseAxisRanges()) {
      if (i >= 0 && i < (int)axisMap.size()) ranges[axisMap[i].get<int>()] = r;
    }

    const int projAxis = c.value("_projectionAxis", 0);

    for (const auto & o : c.value("_binningObjects", json::array())) {
      const std::string name = o.value("name", std::string());
      const std::string path = o.value("path", std::string());

      auto * hns = dynamic_cast<THnSparse *>(point->GetTempObject(name));
      if (hns == nullptr) {
        for (const auto & fv : o.value("files", json::array())) {
          const std::string fileName = fv.get<std::string>();

          TFile * f = NUtils::OpenFile(fileName);
          if (!f || f->IsZombie()) {
            NLogWarning("NGnTreeBinning: cannot open '%s'", fileName.c_str());
            if (f) delete f;
            continue;
          }

          auto * src = dynamic_cast<THnSparse *>(f->Get(path.c_str()));
          if (src != nullptr) {
            if (hns == nullptr) hns = (THnSparse *)src->Clone();
            else hns->Add(src);
          }

          f->Close();
          delete f;
        }

        if (hns == nullptr) {
          NLogWarning("NGnTreeBinning: no '%s' in the input files - skipped", path.c_str());
          continue;
        }
        point->SetTempObject(name, hns);
      }

      NUtils::SetAxisRanges(hns, ranges, false, false, /*reset=*/true);

      TH1 * proj = hns->Projection(projAxis);
      if (proj == nullptr) {
        NLogWarning("NGnTreeBinning: projection of '%s' failed", name.c_str());
        continue;
      }

      proj->SetName(name.c_str());
      proj->SetTitle(TString::Format("%s %s", name.c_str(), point->GetString().c_str()).Data());
      outputPoint->Add(proj);

      NLogDebug("NGnTreeBinning: bin %s '%s' -> %g entries", point->GetString().c_str(), name.c_str(),
                proj->GetEntries());

      if (auto * params = point->GetParameters()) {
        params->SetParameter(name.c_str(), proj->GetEntries(), std::sqrt(proj->GetEntries()));
      }
    }
  };

  NGnBeginFuncPtr beginFunc = [](NBinningPoint * /*point*/, int /*threadId*/) { DisableObjectAutoRegistration(); };

  NGnEndFuncPtr endFunc = [](NBinningPoint * point, int /*threadId*/) {
    for (const auto & o : point->GetCfg().value("_binningObjects", json::array())) {
      const std::string name = o.value("name", std::string());
      auto *            hns  = dynamic_cast<THnSparse *>(point->GetTempObject(name));
      if (hns != nullptr) {
        delete hns;
        point->SetTempObject(name, nullptr);
      }
    }
  };

  bool rc = ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);
  if (rc) NLogInfo("NGnTreeBinning: processing completed successfully -> '%s'", outFile.c_str());
  else NLogError("NGnTreeBinning: processing failed");

  delete ngnt;
  return rc;
}

} // namespace Ndmspc
