#include <TSystem.h>
#include <TFile.h>
#include <TMacro.h>
#include <TString.h>
#include <cstring>
#include <fstream>
#include <string>
#include "TH2.h"
#include "TUrl.h"
#include "Utils.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(Ndmspc::Utils);
/// \endcond

namespace Ndmspc {

TFile * Utils::OpenFile(std::string filename, std::string mode, bool createLocalDir)
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
        // Printf("Ndmspc::Utils::OpenRootFile: Creating directory '%s' ...", filenameLocal.c_str());
        gSystem->mkdir(filenameLocal.c_str(), kTRUE);
      }
    }
  }
  return TFile::Open(filename.c_str(), mode.c_str());
}

std::string Utils::OpenRawFile(std::string filename)
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
bool Utils::SaveRawFile(std::string filename, std::string content)
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

TMacro * Utils::OpenMacro(std::string filename)
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

// void Utils::RebinBins(int & min, int & max, int rebin)
//
// {
//   ///
//   /// Rebin bins
//   ///
//   Printf("%d %d %d", min, max, rebin);
//
//   Int_t binMin  = min;
//   Int_t binMax  = max;
//   Int_t binDiff = binMax - binMin;
//
//   if (rebin > 1) {
//     binMin = 1 + ((binMin - 1) * rebin);
//     binMax = ((binMin - 1) + rebin * (binDiff + 1));
//   }
//
//   min = binMin;
//   max = binMax;
// }
std::string Utils::GetBasePath(json cfg)
{
  std::string path;
  std::string hostUrl = cfg["ndmspc"]["output"]["host"].get<std::string>();
  if (!hostUrl.empty()) path = hostUrl + "/";
  path += cfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";
  path += cfg["ndmspc"]["environment"].get<std::string>() + "/";
  return gSystem->ExpandPathName(path.c_str());
}
std::string Utils::GetCutsPath(json cuts)
{
  ///
  /// Get cut path from config
  ///

  std::string path     = "";
  std::string rebinStr = "";
  for (auto & cut : cuts) {
    Int_t rebin         = 1;
    Int_t rebin_start   = 1;
    Int_t rebin_minimum = 1;
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
    if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();

    if (rebin_start > 1) {
      rebin_minimum = (rebin_start % rebin);
      if (rebin_minimum == 0) rebin_minimum = 1;
    }
    path += cut["axis"].get<std::string>() + "_";
    // Printf("rebin_minimum=%d rebin_start=%d rebin=%d", rebin_minimum, rebin_start, rebin);
    rebinStr += std::to_string(rebin) + "-" + std::to_string(rebin_minimum) + "_";
  }
  path[path.size() - 1]         = '/';
  rebinStr[rebinStr.size() - 1] = '/';
  path += rebinStr;

  // Printf("Path: %s", path.c_str());
  // exit(1);
  return path;
}
Int_t Utils::GetBinFromBase(Int_t bin, Int_t rebin, Int_t rebin_start)
{
  ///
  /// Returns bin from base
  ///

  if (rebin == 1) return bin;
  // Printf("GetBinFromBase %d %d %d", bin, rebin, rebin_start);

  return (bin / rebin) + 1;

  // Int_t binLocal      = (bin / rebin);
  // Int_t rebin_minimum = 1;
  // if (rebin_start > 1) {
  //   rebin_minimum = (rebin_start % rebin);
  //   if (rebin_minimum == 0) rebin_minimum = 1;
  //   // return binLocal + rebin_minimum;
  // }
  // // Printf("binLocal=%d", binLocal + rebin_minimum);
  // return binLocal + rebin_minimum;
}
std::vector<std::string> Utils::Find(std::string path, std::string filename)
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
    // Printf("%s", linesMerge.c_str());
    // gSystem->Exit(1);
  }
  // if (outHost.empty()) {
  //   if (gSystem->AccessPathName(pathFrom.c_str())) {
  //     Printf("Error: Nothing to merge, because path '%s' does not exist !!!", pathFrom.c_str());
  //     return false;
  //   }
  //   Printf("Doing local find %s -name %s", pathFrom.c_str(),
  //          gCfg["ndmspc"]["output"]["file"].get<std::string>().c_str());
  //   linesMerge = gSystem->GetFromPipe(TString::Format("find %s -name %s", pathFrom.c_str(),
  //                                                     gCfg["ndmspc"]["output"]["file"].get<std::string>().c_str())
  //                                         .Data());
  //   // Printf("%s", linesMerge.c_str());
  //   // gSystem->Exit(1);
  // }
  // else {
  //
  //
  // if (linesMerge.empty()) {
  //   Printf("Error: Nothing to merge, because path '%s' does not contain file '%s' !!!", pathFrom.c_str(),
  //          fromFile.c_str());

  return files;
}

std::vector<std::string> Utils::FindLocal(std::string path, std::string filename)
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
std::vector<std::string> Utils::FindEos(std::string path, std::string filename)
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

  TFile * f = Ndmspc::Utils::OpenFile(findUrl.c_str());
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
  for (auto & line : Utils::Tokenize(linesString, '\n')) {
    files.push_back("root://" + host + "/" + line);
  }
  return files;
}

int Utils::SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                               bool normalizeToWidth, bool onlyPositive, double times)
{
  ///
  /// Set result value and error
  ///

  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  bool isValNan = TMath::IsNaN(val);
  bool isErrNaN = TMath::IsNaN(err);

  if (isValNan || isErrNaN) {
    if (verbose >= 0)
      Printf("Error: SetResultValueError %s val=%f[isNaN=%d] err=%f[isNan=%d]", name.c_str(), val, isValNan, err,
             isErrNaN);
    return -2;
  }

  if (onlyPositive && val < 0) {
    return -3;
  }

  if (times > 0 && times * std::abs(val) < err) {
    if (verbose >= 0)
      Printf("Warning: Skipping '%s' because 'times * val < err' (  %f * %f < %f ) ...", name.c_str(), times,
             std::abs(val), err);
    return -4;
  }

  if (normalizeToWidth) {
    int nDimsCuts = 0;
    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      nDimsCuts++;
    }
    // Printf("1 %f %f", val, err);
    for (int iCut = 0; iCut < nDimsCuts; iCut++) {
      double w = output->GetAxis(iCut + 1)->GetBinWidth(point[iCut + 1]);
      val /= w;
      err /= w;
      // Printf("%d %f %f w=%f", iCut + 1, val, err, w);
    }
  }

  int idx = output->GetAxis(0)->FindBin(name.c_str());
  if (idx <= 0) {
    return idx;
  }
  point[0]       = idx;
  Long64_t binId = output->GetBin(point);
  output->SetBinContent(binId, val);
  output->SetBinError(binId, err);

  return idx;
}
std::vector<std::string> Utils::Tokenize(std::string_view input, const char delim)
{
  ///
  /// Tokenize helper function
  ///

  std::vector<std::string> out;

  for (auto found = input.find(delim); found != std::string_view::npos; found = input.find(delim)) {
    out.emplace_back(input, 0, found);
    input.remove_prefix(found + 1);
  }

  if (not input.empty()) out.emplace_back(input);

  return out;
}
std::vector<int> Utils::TokenizeInt(std::string_view input, const char delim)
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
std::vector<std::string> Utils::Truncate(std::vector<std::string> values, std::string value)
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

std::set<std::string> Utils::Unique(std::vector<std::string> & paths, int axis, std::string path, char token)
{
  ///
  /// Unique helper function
  ///

  std::set<std::string>    out;
  std::vector<std::string> truncatedPaths = Utils::Truncate(paths, path);
  for (auto & p : truncatedPaths) {
    std::vector<std::string> tokens = Tokenize(p, token);
    out.insert(tokens[axis]);
  }
  return out;
}
TH2D * Utils::GetMappingHistogram(std::string name, std::string title, std::set<std::string> x, std::set<std::string> y)
{
  ///
  /// Get mapping histogram
  ///

  int nBinsX = x.size();
  int nBinsY = y.size();
  if (nBinsY == 0) nBinsY = 1;
  TH2D * h = new TH2D(name.c_str(), title.c_str(), nBinsX, 0, nBinsX, nBinsY, 0, nBinsY);
  int    i = 1;
  for (auto & xV : x) {
    h->GetXaxis()->SetBinLabel(i, xV.c_str());
    i++;
  }
  i = 1;
  for (auto & yV : y) {
    h->GetYaxis()->SetBinLabel(i, yV.c_str());
    i++;
  }
  return h;
}
} // namespace Ndmspc
