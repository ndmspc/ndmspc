#include <iostream>
#include <TSystem.h>
#include <TROOT.h>
#include <vector>
#include <string>
#include <sstream>
#include <TF1.h>
#include <TAxis.h>
#include <THnSparse.h>
#include <TString.h>
#include "NLogger.h"
#include "NHttpRequest.h"
#include "ndmspc.h"
#ifdef WITH_PARQUET
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/exception.h>
#endif
#include "NUtils.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(Ndmspc::NUtils);
/// \endcond

namespace Ndmspc {

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

  // Enable IMT with default number of threads (usually number of CPU cores)
  if (numthreads > 1) {
    ROOT::EnableImplicitMT(numthreads);
  }

  // Check if IMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {
    NLogInfo("ROOT::ImplicitMT is enabled with number of threads: %d", ROOT::GetThreadPoolSize());
  }

  return previouslyEnabled;
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
    // TODO: check if URL exists via HTTP request
    NHttpRequest request;
    // request.SetUrl(pathStr.Data());
    // return request.Exists();
    int http_code = request.head(pathStr.Data());
    if (http_code == 200) {
      return true;
    }

    return false;
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
    TFile * f = TFile::Open(pathStr.Data());
    if (f && !f->IsZombie()) {
      f->Close();
      return true;
    }
    return false;
  }
  return false;
}

int NUtils::Cp(std::string source, std::string destination)
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

  if (IsFileSupported(source) == false) {
    NLogError("NUtils::Cp: Source file '%s' is not supported", source.c_str());
    return -1;
  }
  if (IsFileSupported(destination) == false) {
    NLogError("NUtils::Cp: Destination file '%s' is not supported", destination.c_str());
    return -1;
  }

  NLogInfo("Copying file from '%s' to '%s' ...", source.c_str(), destination.c_str());
  // rc = TFile::Cp(source, destination);
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
    NLogDebug("NUtils::CreateAxisFromLabels: Adding label: %s", labels[i].c_str());
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
    NLogDebug("NUtils::CreateAxisFromLabels: Adding label: %s", label.c_str());
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

  int      nDims = 1;
  Int_t    bins[nDims];
  Double_t xmin[nDims];
  Double_t xmax[nDims];

  TAxis * aIn = h1->GetXaxis();
  bins[0]     = aIn->GetNbins();
  xmin[0]     = aIn->GetXmin();
  xmax[0]     = aIn->GetXmax();

  THnSparse * hns = new THnSparseD(h1->GetName(), h1->GetTitle(), nDims, bins, xmin, xmax);

  // loop over all axes
  for (int i = 0; i < nDims; i++) {
    TAxis * a   = hns->GetAxis(i);
    TAxis * aIn = h1->GetXaxis();
    a->SetName(aIn->GetName());
    a->SetTitle(aIn->GetTitle());
    if (aIn->GetXbins()->GetSize() > 0) {
      Double_t arr[aIn->GetNbins() + 1];
      arr[0] = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr);
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
  int      nDims = 2;
  Int_t    bins[nDims];
  Double_t xmin[nDims];
  Double_t xmax[nDims];

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

  THnSparse * hns = new THnSparseD(h2->GetName(), h2->GetTitle(), nDims, bins, xmin, xmax);

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
      Double_t arr[aIn->GetNbins() + 1];
      arr[0] = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr);
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

  int      nDims = 3;
  Int_t    bins[nDims];
  Double_t xmin[nDims];
  Double_t xmax[nDims];

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

  THnSparse * hns = new THnSparseD(h3->GetName(), h3->GetTitle(), nDims, bins, xmin, xmax);

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
      Double_t arr[aIn->GetNbins() + 1];
      arr[0] = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr);
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

  int      nDims = hns->GetNdimensions() + newAxes.size();
  Int_t    bins[nDims];
  Double_t xmin[nDims];
  Double_t xmax[nDims];
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

  THnSparse * hnsNew = new THnSparseD(hns->GetName(), hns->GetTitle(), nDims, bins, xmin, xmax);

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
      Double_t arr[aIn->GetNbins() + 1];
      arr[0] = aIn->GetBinLowEdge(1);
      for (int iBin = 1; iBin <= aIn->GetNbins(); iBin++) {
        arr[iBin] = aIn->GetBinUpEdge(iBin);
      }
      a->Set(a->GetNbins(), arr);
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
      Int_t p[nDims];
      Int_t pNew[nDims];
      hns->GetBinContent(i, p);
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
      hnsNew->SetBinContent(pNew, v);
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

TFile * NUtils::OpenFile(std::string filename, std::string mode, bool createLocalDir)
{
  ///
  /// Open root file and create directory when needed in local case
  ///

  filename = gSystem->ExpandPathName(filename.c_str());
  if (createLocalDir) {
    // Printf("%s", filename.c_str());
    if (!mode.compare("RECREATE") || !mode.compare("UPDATE") || !mode.compare("WRITE")) {

      TString filenameT(filename.c_str());
      bool    isLocalFile = filenameT.BeginsWith("file://");
      if (isLocalFile) {
        // Remove file:// prefix
        filenameT.ReplaceAll("file://", "");
      }
      else {
        isLocalFile = !filenameT.Contains("://");
      }

      if (isLocalFile) {

        std::string pwd = gSystem->pwd();
        if (filenameT[0] != '/') filenameT = pwd + "/" + filenameT;
        filenameT.ReplaceAll("?remote=1&", "?");
        filenameT.ReplaceAll("?remote=1", "");
        filenameT.ReplaceAll("&remote=1", "");
        TUrl url(filenameT.Data());

        std::string filenameLocal = gSystem->GetDirName(url.GetFile()).Data();
        // Printf("Ndmspc::NUtils::OpenRootFile: Creating directory '%s' ...", filenameLocal.c_str());
        gSystem->mkdir(filenameLocal.c_str(), kTRUE);
      }
    }
  }
  return TFile::Open(filename.c_str(), mode.c_str());
}

std::string NUtils::OpenRawFile(std::string filename)
{
  ///
  /// Opens raw file
  ///

  std::string content;
  TFile *     f = OpenFile(TString::Format("%s?filetype=raw", filename.c_str()).Data());
  if (!f) return "";

  // Printf("%lld", f->GetSize());

  int  buffsize = 4096;
  char buff[buffsize + 1];

  Long64_t buffread = 0;
  while (buffread < f->GetSize()) {
    if (buffread + buffsize > f->GetSize()) buffsize = f->GetSize() - buffread;

    // Printf("Buff %lld %d", buffread, buffsize);
    f->ReadBuffer(buff, buffread, buffsize);
    buff[buffsize] = '\0';
    content += buff;
    buffread += buffsize;
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

TMacro * NUtils::OpenMacro(std::string filename)
{
  ///
  /// Open macro
  ///

  std::string content = OpenRawFile(filename);
  if (content.empty()) {

    Printf("Error: Problem opening macro '%s' ...", filename.c_str());
    return nullptr;
  }
  Printf("Using macro '%s' ...", filename.c_str());
  TUrl        url(filename.c_str());
  std::string basefilename = gSystem->BaseName(url.GetFile());
  basefilename.pop_back();
  basefilename.pop_back();
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
  if (gSystem->AccessPathName(path.c_str())) {
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

  int  buffsize = 4096;
  char buff[buffsize + 1];

  Long64_t    buffread = 0;
  std::string content;
  while (buffread < f->GetSize()) {

    if (buffread + buffsize > f->GetSize()) buffsize = f->GetSize() - buffread;

    // Printf("Buff %lld %d", buffread, buffsize);
    f->ReadBuffer(buff, buffread, buffsize);
    buff[buffsize] = '\0';
    content += buff;
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
  return h;
}

bool NUtils::SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges, bool withOverflow,
                           bool modifyTitle)
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
                           bool modifyTitle)
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

void NUtils::ProgressBar(int current, int total, int barWidth, std::string prefix, std::string suffix)
{

  ///
  /// Print progress bar
  ///
  if (total == 0) return; // Avoid division by zero

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
  std::cout << std::flush; // Ensure immediate output
}

void NUtils::ProgressBar(int current, int total, std::chrono::high_resolution_clock::time_point startTime, int barWidth,
                         std::string prefix, std::string suffix)
{
  ///
  /// Print progress bar
  ///
  if (total == 0) return;               // Avoid division by zero
  if (current > total) current = total; // Cap current to total for safety

  float percentage = static_cast<float>(current) / total;
  int   numChars   = static_cast<int>(percentage * barWidth);

  // std::cout << "\r[" << prefix << "] ["; // Carriage return
  std::cout << "\r[";
  if (!prefix.empty()) std::cout << prefix << "] ["; // Carriage return
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
  std::cout << std::flush; // Ensure immediate output
}
THnSparse * NUtils::CreateSparseFromParquetTaxi(const std::string & filename, THnSparse * hns, Int_t nMaxRows)
{
  ///
  /// Create THnSparse from Parquet file
  ///
  // Open the Parquet file
#ifdef WITH_PARQUET

  if (hns == nullptr) {
    NLogError("NUtils::CreateSparseFromParquetTaxi: THnSparse 'hns' is nullptr ...");
    return nullptr;
  }

  std::shared_ptr<arrow::io::ReadableFile>                infile;
  arrow::Result<std::shared_ptr<arrow::io::ReadableFile>> infile_result = arrow::io::ReadableFile::Open(filename);
  if (!infile_result.ok()) {
    NLogError("NUtils::CreateSparseFromParquetTaxi: Error opening file %s: %s", filename.c_str(),
              infile_result.status().ToString().c_str());
    return nullptr;
  }
  infile = infile_result.ValueUnsafe();

  // Create a Parquet reader using the modern arrow::Result API
  std::unique_ptr<parquet::arrow::FileReader> reader;

  // The new approach using arrow::Result:
  arrow::Result<std::unique_ptr<parquet::arrow::FileReader>> reader_result =
      parquet::arrow::OpenFile(infile, arrow::default_memory_pool()); // No third parameter!
  if (!reader_result.ok()) {
    NLogError("NUtils::CreateSparseFromParquetTaxi: Error opening Parquet file reader for file %s: %s",
              filename.c_str(), reader_result.status().ToString().c_str());
    arrow::Status status = infile->Close(); // Attempt to close
    return nullptr;
  }
  reader = std::move(reader_result).ValueUnsafe(); // Transfer ownership from Result to unique_ptr

  // Get file metadata (optional)
  // Note: parquet_reader() returns a const ptr, and metadata() returns a shared_ptr
  std::shared_ptr<parquet::FileMetaData> file_metadata = reader->parquet_reader()->metadata();
  NLogTrace("Parquet file '%s' opened successfully.", filename.c_str());
  NLogTrace("Parquet file version: %d", file_metadata->version());
  NLogTrace("Parquet created by: %s", file_metadata->created_by().c_str());
  NLogTrace("Parquet number of columns: %d", file_metadata->num_columns());
  NLogTrace("Parquet number of rows: %lld", file_metadata->num_rows());
  NLogTrace("Parquet number of row groups: %d", file_metadata->num_row_groups());

  // Read the entire file as a Table
  // std::shared_ptr<arrow::Table> table;
  // arrow::Status                 status = reader->ReadTable(&table); // ReadTable still returns Status
  std::shared_ptr<arrow::RecordBatchReader> batch_reader;
  arrow::Status                             status = reader->GetRecordBatchReader(&batch_reader);
  if (!status.ok()) {
    NLogError("NUtils::CreateSparseFromParquetTaxi: Error reading table from Parquet file %s: %s", filename.c_str(),
              status.ToString().c_str());
    status = infile->Close();
    return nullptr;
  }

  // It's good practice to close the input file stream when done
  status = infile->Close();
  if (!status.ok()) {
    NLogWarning("NUtils::CreateSparseFromParquetTaxi: Error closing input file %s: %s", filename.c_str(),
                status.ToString().c_str());
    // This is a warning, we still want to return the table.
  }

  // Print schema of the table
  NLogTrace("Parquet Table Schema:\n%s", batch_reader->schema()->ToString().c_str());

  const Int_t              nDims = hns->GetNdimensions();
  std::vector<std::string> column_names;
  for (int i = 0; i < nDims; ++i) {
    column_names.push_back(hns->GetAxis(i)->GetName());
  }
  // std::cout << "\nData (first 5 rows):\n";

  // int max_rows                                           = table->num_rows();
  int max_rows   = 1e8;
  max_rows       = nMaxRows > 0 ? std::min(max_rows, nMaxRows) : max_rows;
  int print_rows = std::min(max_rows, 5);
  // auto                                table_batch_reader = std::make_shared<arrow::TableBatchReader>(*table);
  auto                                table_batch_reader = batch_reader;
  std::shared_ptr<arrow::RecordBatch> batch;
  Double_t                            point[nDims];

  if (print_rows > 0) {
    NLogTrace("Printing first %d rows of Parquet file '%s' ...", print_rows, filename.c_str());
    // NLogInfo("Columns: %s", NUtils::Join(column_names, '\t').c_str());
  }

  int batch_count = 0;
  while (table_batch_reader->ReadNext(&batch).ok() && batch) {
    batch_count++;
    NLogTrace("Processing batch with %d rows and %d columns ...", batch->num_rows(), batch->num_columns());
    for (int i = 0; i < batch->num_rows(); ++i) {
      if (i >= max_rows) break; // Limit to first 5 rows for display

      bool isValid = true;
      int  idx     = 0;
      for (int j = 0; j < batch->num_columns(); ++j) {
        if (std::find(column_names.begin(), column_names.end(), batch->column_name(j)) == column_names.end())
          continue; // Skip columns not in our list
        // NLogDebug("[%d %s]Processing row %d, column '%s' ...", idx, hns->GetAxis(idx)->GetName(), i,
        //                batch->column_name(j).c_str());
        // std::cout << batch->column_name(j) << "\t";
        const auto &                                  array         = batch->column(j);
        arrow::Result<std::shared_ptr<arrow::Scalar>> scalar_result = array->GetScalar(i);
        if (scalar_result.ok()) {
          // if (i * batch_count < print_rows) std::cout << scalar_result.ValueUnsafe()->ToString() << "\t";
          if (scalar_result.ValueUnsafe()->is_valid) {
            TAxis * axis = hns->GetAxis(idx);
            if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::STRING ||
                scalar_result.ValueUnsafe()->type->id() == arrow::Type::LARGE_STRING) {
              // Arrow StringScalar's value is an arrow::util::string_view or arrow::util::string_view
              // It's best to convert it to std::string for general use.
              std::string value = scalar_result.ValueUnsafe()->ToString();
              // TODO: check if not shifted by one
              // NLogInfo("NUtils::CreateSparseFromParquetTaxi: Mapping string value '%s' to axis '%s' ...",
              //               value.c_str(), axis->GetName());
              point[idx] = axis->GetBinCenter(axis->FindBin(value.c_str()));
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::INT32) {
              auto int_scalar = std::static_pointer_cast<arrow::Int32Scalar>(scalar_result.ValueUnsafe());

              point[idx] = static_cast<Double_t>(int_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::INT64) {
              auto int64_scalar = std::static_pointer_cast<arrow::Int64Scalar>(scalar_result.ValueUnsafe());
              point[idx]        = static_cast<Double_t>(int64_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::UINT32) {
              auto uint32_scalar = std::static_pointer_cast<arrow::UInt32Scalar>(scalar_result.ValueUnsafe());
              point[idx]         = static_cast<Double_t>(uint32_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::FLOAT) {
              auto float_scalar = std::static_pointer_cast<arrow::FloatScalar>(scalar_result.ValueUnsafe());
              point[idx]        = static_cast<Double_t>(float_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::DOUBLE) {
              auto double_scalar = std::static_pointer_cast<arrow::DoubleScalar>(scalar_result.ValueUnsafe());
              point[idx]         = double_scalar->value;
            }
            else {
              NLogError("NUtils::CreateSparseFromParquetTaxi: Unsupported data type for column '%s' ...",
                        batch->column_name(j).c_str());
              isValid = false;
            }
          }
          else {
            // Handle null values (set to 0 or some default)
            //
            //
            point[idx] = -1000;
            isValid    = false;
            isValid    = true;
          }
        }
        else {
          NLogError("NUtils::CreateSparseFromParquetTaxi: Error getting scalar at (%d,%d): %s", i, j,
                    scalar_result.status().ToString().c_str());
          isValid = false;
        }
        idx++;
      }
      // if (i * batch_count < print_rows) std::cout << std::endl;
      if (isValid) {
        // print point
        // for (int d = 0; d < nDims; ++d) {
        //   NLogDebug("Point[%d=%s]=%f", d, hns->GetAxis(d)->GetName(), point[d]);
        // }
        hns->Fill(point);
      }
      else {
        NLogWarning("Skipping row %d due to invalid data.", i);
      }
    }
  }
  return hns;

#else
  NLogError("Parquet support is not enabled. Please compile with Parquet support.");
  return nullptr;
#endif
}
} // namespace Ndmspc
