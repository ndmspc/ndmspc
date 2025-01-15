#include <TSystem.h>
#include <TFile.h>
#include <TMacro.h>
#include <TString.h>
#include <cstring>
#include <fstream>
#include <string>
#include "Utils.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(NdmSpc::Utils);
/// \endcond

namespace NdmSpc {

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
        // Printf("NdmSpc::Utils::OpenRootFile: Creating directory '%s' ...", filenameLocal.c_str());
        gSystem->mkdir(filenameLocal.c_str(), kTRUE);
      }
    }
  }
  return TFile::Open(filename.c_str(), mode.c_str());
}

std::string Utils::OpenRawFile(std::string filename)
{
  std::string content;
  TFile *     f = TFile::Open(TString::Format("%s?filetype=raw", filename.c_str()));
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

TMacro * Utils::OpenMacro(std::string filename)
{

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
std::string Utils::GetCutsPath(json cuts)
{
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
  return std::move(path);
}
Int_t Utils::GetBinFromBase(Int_t bin, Int_t rebin, Int_t rebin_start)
{
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

int Utils::SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                               bool normalizeToWidth, bool onlyPositive, double times)
{

  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  bool isValNan = TMath::IsNaN(val);
  bool isErrNaN = TMath::IsNaN(err);

  if (isValNan || isErrNaN) {
    if (verbose > 0)
      Printf("Error: SetResultValueError %s val=%f[isNaN=%d] err=%f[isNan=%d]", name.c_str(), val, isValNan, err,
             isErrNaN);
    return -2;
  }

  if (onlyPositive && val < 0) {
    return -3;
  }

  if (times > 0 && times * std::abs(val) < err) {
    if (verbose > 0)
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
  std::vector<std::string> out;

  for (auto found = input.find(delim); found != std::string_view::npos; found = input.find(delim)) {
    out.emplace_back(input, 0, found);
    input.remove_prefix(found + 1);
  }

  if (not input.empty()) out.emplace_back(input);

  return out;
}

} // namespace NdmSpc
