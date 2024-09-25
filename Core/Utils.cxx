#include <TSystem.h>
#include <TFile.h>
#include <TMacro.h>
#include <TString.h>
#include <cstring>
#include <fstream>
#include "Utils.h"
namespace NdmSpc {
using std::ifstream;

json        Utils::fCfg;
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
json & Utils::LoadConfig(std::string config, std::string userConfig)
{
  std::string fileContent = OpenRawFile(config);
  if (!fileContent.empty()) {
    fCfg = json::parse(fileContent);
    Printf("Using config file '%s' ...", config.c_str());
    if (!userConfig.empty()) {
      std::string fileContentUser = NdmSpc::Utils::OpenRawFile(userConfig);
      if (!fileContentUser.empty()) {
        json userCfg = json::parse(fileContentUser);
        fCfg.merge_patch(userCfg);
        Printf("Merging user config file '%s' ...", userConfig.c_str());
      }
      else {
        Printf("Warning: User config '%s' was specified, but it was not open !!!", userConfig.c_str());
        return fCfg;
      }
    }
  }
  else {
    Printf("Error: Problem opening config file '%s' !!! Exiting ...", config.c_str());
    return fCfg;
  }

  return fCfg;
}
void Utils::RebinBins(int & min, int & max, int rebin)

{
  ///
  /// Rebin bins
  ///

  Int_t binMin  = min;
  Int_t binMax  = max;
  Int_t binDiff = binMax - binMin;

  if (rebin > 1) {
    binMin = 1 + ((binMin - 1) * rebin);
    binMax = ((binMin - 1) + rebin * (binDiff + 1));
  }
  min = binMin;
  max = binMax;
}

int Utils::SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                               bool normalizeToWidth, bool onlyPositive, double times)
{

  bool isValNan = TMath::IsNaN(val);
  bool isErrNaN = TMath::IsNaN(err);

  if (isValNan || isErrNaN) {
    Printf("Error: SetResultValueError %s val=%f[isNaN=%d] err=%f[isNan=%d]", name.c_str(), val, isValNan, err,
           isErrNaN);
    return -2;
  }

  if (onlyPositive && val < 0) {
    return -3;
  }

  if (times > 0 && times * std::abs(val) < err) {
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
