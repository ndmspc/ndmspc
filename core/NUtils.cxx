#include <TSystem.h>
#include <vector>
#include <string>
#include <sstream>
#include "THnSparse.h"
#include "NLogger.h"
#include "NUtils.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(Ndmspc::NUtils);
/// \endcond

namespace Ndmspc {

THnSparse * NUtils::Convert(TH1 * h1, std::vector<std::string> names, std::vector<std::string> titles)
{
  ///
  /// Convert TH1 to THnSparse
  ///

  if (h1 == nullptr) {
    NLogger::Error("TH1 h1 is null");
    return nullptr;
  }

  NLogger::Info("Converting TH1 '%s' to THnSparse ...", h1->GetName());

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

  for (size_t i = 0; i < nDims; i++) {
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
    NLogger::Error("TH2 h2 is null");
    return nullptr;
  }
  NLogger::Info("Converting TH2 '%s' to THnSparse ...", h2->GetName());
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
      NLogger::Error("Invalid axis index %d", i);
      return nullptr;
    }
    bins[i] = aIn->GetNbins();
    xmin[i] = aIn->GetXmin();
    xmax[i] = aIn->GetXmax();
  }

  THnSparse * hns = new THnSparseD(h2->GetName(), h2->GetTitle(), nDims, bins, xmin, xmax);

  for (int i = 0; i < nDims; i++) {
    TAxis * a   = hns->GetAxis(i);
    TAxis * aIn = nullptr;
    if (i == 0)
      aIn = h2->GetXaxis();
    else if (i == 1)
      aIn = h2->GetYaxis();
    else {
      NLogger::Error("Invalid axis index %d", i);
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

  for (size_t i = 0; i < nDims; i++) {
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
    NLogger::Error("TH3 h3 is null");
    return nullptr;
  }

  NLogger::Info("Converting TH3 '%s' to THnSparse ...", h3->GetName());

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
      NLogger::Error("Invalid axis index %d", i);
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
      NLogger::Error("Invalid axis index %d", i);
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

  for (size_t i = 0; i < nDims; i++) {
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
    NLogger::Error("THnSparse hns is null");
    return nullptr;
  }

  if (order.size() != hns->GetNdimensions() + newAxes.size()) {
    NLogger::Error("Invalid size %d [order] != %d [hns->GetNdimensions()+newAxes]", order.size(),
                   hns->GetNdimensions() + newAxes.size());
    return nullptr;
  }

  if (newAxes.size() != newPoint.size()) {
    NLogger::Error("Invalid size %d [newAxes] != %d [newPoint]", newAxes.size(), newPoint.size());
    return nullptr;
  }

  // loop over order and check if order contains values from 0 to hns->GetNdimensions() + newAxes.size()
  for (int i = 0; i < order.size(); i++) {
    if (order[i] < 0 || order[i] >= hns->GetNdimensions() + newAxes.size()) {
      NLogger::Error(
          "Invalid order[%d]=%d. Value is negative or higher then 'hns->GetNdimensions() + newAxes.size()' !!!", i,
          order[i]);
      return nullptr;
    }
  }

  // check if order contains unique values
  for (int i = 0; i < order.size(); i++) {
    for (int j = i + 1; j < order.size(); j++) {
      if (order[i] == order[j]) {
        NLogger::Error("Invalid order[%d]=%d and order[%d]=%d. Value is not unique !!!", i, order[i], j, order[j]);
        return nullptr;
      }
    }
  }

  // print info about original THnSparse
  NLogger::Info("Original THnSparse object:");
  hns->Print();

  NLogger::Info("Reshaping sparse axes ...");

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
      NLogger::Info("[ORIG] Axis [%d]->[%d]: %s %s %d %.2f %.2f", id, i, a->GetName(), a->GetTitle(), a->GetNbins(),
                    a->GetXmin(), a->GetXmax());
    }
    else {
      newAxesIndex = id - hns->GetNdimensions();
      a            = newAxes[newAxesIndex];
      NLogger::Info("[NEW ] Axis [%d]->[%d]: %s %s %d %.2f %.2f", id, i, a->GetName(), a->GetTitle(), a->GetNbins(),
                    a->GetXmin(), a->GetXmax());
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
  }

  // loop over all bins
  NLogger::Info("Filling all bins ...");
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
  // Calsculate sumw2
  if (opt.Contains("E")) {
    NLogger::Debug("Calculating sumw2 ...");
    hnsNew->Sumw2();
  }
  hnsNew->SetEntries(hns->GetEntries());
  NLogger::Info("Reshaped sparse axes:");
  // print all axes
  for (int i = 0; i < nDims; i++) {
    TAxis * a = hnsNew->GetAxis(i);
    NLogger::Info("Axis %d: %s %s %d %.2f %.2f", i, a->GetName(), a->GetTitle(), a->GetNbins(), a->GetXmin(),
                  a->GetXmax());
  }
  hnsNew->Print("all");
  return hnsNew;
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
  if (!f) return false;

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

std::vector<std::string> NUtils::Find(std::string path, std::string filename)
{
  ///
  /// Find files in path
  ///

  std::vector<std::string> files;
  path = gSystem->ExpandPathName(path.c_str());
  if (path.rfind("root://", 0) == 0) {
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
    Printf("Error: Nothing to merge, because path '%s' does not exist !!!", path.c_str());
    return files;
  }
  Printf("Doing local find %s -name %s", path.c_str(), filename.c_str());
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
  Printf("Doing eos find -f --name %s %s ", filename.c_str(), path.c_str());

  TUrl        url(path.c_str());
  std::string host      = url.GetHost();
  std::string directory = url.GetFile();
  std::string findUrl   = "root://";
  findUrl += host + "//proc/user/";
  findUrl += "?mgm.cmd=find&mgm.find.match=" + filename;
  findUrl += "&mgm.path=" + directory;
  findUrl += "&mgm.format=json&mgm.option=f&filetype=raw";
  Printf("Doing '%s' ...", findUrl.c_str());

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

bool NUtils::SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges, bool withOverflow)
{
  ///
  /// Set axis ranges
  ///
  ///

  if (sparse == nullptr) {
    NLogger::Error("Error: Sparse is nullptr ...");
    return false;
  }
  if (sparse->GetNdimensions() == 0) return true;

  NLogger::Trace("Setting axis ranges on '%s' THnSparse ...", sparse->GetName());
  /// Reset all axis ranges
  for (int i = 0; i < sparse->GetNdimensions(); i++) {
    if (withOverflow) {
      NLogger::Trace("Resetting '%s' axis ...", sparse->GetAxis(i)->GetName());
      sparse->GetAxis(i)->SetRange(0, 0);
    }
    else {
      NLogger::Trace("Resetting '%s' axis [%d,%d] ...", sparse->GetAxis(i)->GetName(), 1,
                     sparse->GetAxis(i)->GetNbins());
      sparse->GetAxis(i)->SetRange(1, sparse->GetAxis(i)->GetNbins());
    }
  }

  TAxis * axis = nullptr;
  for (int i = 0; i < ranges.size(); i++) {
    axis = sparse->GetAxis(ranges[i][0]);
    NLogger::Trace("Setting axis range %s=[%d,%d] ...", axis->GetName(), ranges[i][1], ranges[i][2]);
    if (ranges[i].size() != 3) {
      NLogger::Error("Error: Axis range must have 3 values, but has %zu ...", ranges[i].size());
      return false;
    }
    axis->SetRange(ranges[i][1], ranges[i][2]);
  }
  return true;
}

bool NUtils::SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow)
{
  ///
  /// Set axis ranges
  ///
  ///

  if (sparse == nullptr) {
    NLogger::Error("NUtils::SetAxisRanges: Sparse is nullptr ...");
    return false;
  }
  if (sparse->GetNdimensions() == 0) return true;

  NLogger::Trace("NUtils::SetAxisRanges: Setting axis ranges on '%s' THnSparse ...", sparse->GetName());
  /// Reset all axis ranges
  for (int i = 0; i < sparse->GetNdimensions(); i++) {
    if (withOverflow) {
      NLogger::Trace("NUtils::SetAxisRanges: Resetting '%s' axis ...", sparse->GetAxis(i)->GetName());
      sparse->GetAxis(i)->SetRange(0, 0);
    }
    else {
      NLogger::Trace("NUtils::SetAxisRanges: Resetting '%s' axis [%d,%d] ...", sparse->GetAxis(i)->GetName(), 1,
                     sparse->GetAxis(i)->GetNbins());
      sparse->GetAxis(i)->SetRange(1, sparse->GetAxis(i)->GetNbins());
    }
  }

  TAxis * axis = nullptr;
  for (const auto & [key, val] : ranges) {
    axis = sparse->GetAxis(key);
    NLogger::Trace("NUtils::SetAxisRanges: Setting axis range %s=[%d,%d] ...", axis->GetName(), val[0], val[1]);
    axis->SetRange(val[0], val[1]);
  }
  return true;
}
bool NUtils::GetAxisRangeInBase(TAxis * a, int rebin, int rebin_start, int bin, int & min, int & max)
{
  ///
  /// Returns axis range in base in min and max variables
  ///
  if (a == nullptr) {
    NLogger::Error("Error: Axis is nullptr ...");
    return false;
  }
  min = -1;
  max = -1;
  if (a->IsVariableBinSize()) {
    NLogger::Error("Error: Axis '%s' is variable bin size (not supported)...", a->GetName());
    return false;
  }

  NLogger::Trace("Getting axis range in base for '%s' rebin=%d rebin_start=%d bin=%d...", a->GetName(), rebin,
                 rebin_start, bin);

  min = rebin * (bin - 1) + rebin_start;
  max = min + rebin - 1;
  NLogger::Trace("Axis '%s' min=%d max=%d", a->GetName(), min, max);

  if (min < 1) {
    NLogger::Error("Error: Axis '%s' min=%d is lower then 1 ...", a->GetName(), min);
    min = -1;
    max = -1;
    return false;
  }

  if (max > a->GetNbins()) {
    NLogger::Error("Error: Axis '%s' max=%d is higher then %d ...", a->GetName(), max, a->GetNbins());
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

  NLogger::Trace("Getting axis range in base for '%s' min=%d max=%d rebin=%d rebin_start=%d...", a->GetName(), min, max,
                 rebin, rebin_start);

  int tmp;
  GetAxisRangeInBase(base, rebin, rebin_start, min, minBase, tmp);
  GetAxisRangeInBase(base, rebin, rebin_start, max, tmp, maxBase);
  NLogger::Trace("Axis '%s' minBase=%d maxBase=%d", a->GetName(), minBase, maxBase);

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

  NLogger::Info("%s", GetCoordsString(coords, index).c_str());
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
  NLogger::Debug("Permutations of vector: %s", GetCoordsString(v).c_str());
  for (const auto & perm : result) {
    NLogger::Debug("Permutation: %s", GetCoordsString(perm).c_str());
  }

  return result;
}
} // namespace Ndmspc
