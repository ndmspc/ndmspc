#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "TArrayD.h"
#include <TFileMerger.h>
#include <TString.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TH1.h>
#include <TSystem.h>
#include <THnSparse.h>
#include "Core.h"
#include "Utils.h"
#include "PointRun.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::PointRun);
/// \endcond

namespace Ndmspc {
std::string PointRun::fgEnvironment = "";
PointRun::PointRun(std::string macro) : TObject()
{
  ///
  /// Default constructor
  ///

  TH1::AddDirectory(kFALSE);
  fMacro = Utils::OpenMacro(macro.c_str());
  if (fMacro) fMacro->Load();
}

PointRun::~PointRun()
{
  ///
  /// Descructor
  ///
}

bool PointRun::LoadConfig(std::string config, std::string userConfig, std::string environment,
                          std::string userConfigRaw, bool show, std::string outfilename)
{
  ///
  /// Load config and set default PointRun parameters
  ///

  if (!Core::LoadConfig(config, userConfig, environment, userConfigRaw)) return 1;

  if (!gCfg["ndmspc"]["verbose"].is_null() && gCfg["ndmspc"]["verbose"].is_number_integer())
    fVerbose = gCfg["ndmspc"]["verbose"].get<int>();

  if (!gCfg["ndmspc"]["file"]["cache"].is_null() && gCfg["ndmspc"]["file"]["cache"].is_string()) {
    std::string cacheDir = gCfg["ndmspc"]["file"]["cache"].get<std::string>();
    if (!cacheDir.empty()) {
      Printf("Setting cache directory to '%s' ...", gSystem->ExpandPathName(cacheDir.c_str()));
      TFile::SetCacheFileDir(gSystem->ExpandPathName(cacheDir.c_str()), 1, 1);
    }
  }

  if (show) Printf("%s", gCfg.dump(2).c_str());

  if (!outfilename.empty()) {
    std::ofstream file(outfilename.c_str());
    file << gCfg;
    Printf("Config saved to file '%s' ...", outfilename.c_str());
    return false;
  }
  return true;
}

bool PointRun::Init(std::string extraPath)
{
  ///
  /// Init
  ///

  if (fVerbose >= 2) Printf("Ndmspc::PointRun::Init ...");
  if (!gCfg["ndmspc"]["process"]["type"].get<std::string>().compare("all") &&
      gCfg["ndmspc"]["process"]["ranges"].is_null() &&
      !gCfg["ndmspc"]["output"]["delete"].get<std::string>().compare("onInit")) {

    if (gCfg["ndmspc"]["output"]["host"].get<std::string>().empty()) {
      gCfg["ndmspc"]["output"]["opt"] = "";
    }

    std::string outFileName;
    if (!gCfg["ndmspc"]["output"]["dir"].get<std::string>().empty()) {
      // outFileName = gCfg["ndmspc"]["output"]["host"].get<std::string>() + "/";

      outFileName += gCfg["ndmspc"]["output"]["dir"].get<std::string>();
      outFileName += "/";

      std::string environment = gCfg["ndmspc"]["environment"].get<std::string>();
      outFileName += environment + "/";

      outFileName += Utils::GetCutsPath(gCfg["ndmspc"]["cuts"]);
      // std::string rebinStr = "";
      // for (auto & cut : gCfg["ndmspc"]["cuts"]) {
      //   Int_t rebin         = 1;
      //   Int_t rebin_start   = 1;
      //   Int_t rebin_minimum = 1;
      //   if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      //   if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
      //   if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();
      //
      //   if (rebin_start > 1) {
      //     rebin_minimum = (rebin_start % rebin);
      //   }
      //   outFileName += cut["axis"].get<std::string>() + "_";
      //   rebinStr += std::to_string(rebin) + "-" + std::to_string(rebin_minimum) + "_";
      // }
      // outFileName[outFileName.size() - 1] = '/';
      // rebinStr[rebinStr.size() - 1]       = '/';
      // outFileName += rebinStr;
      //
      outFileName += "bins";

      if (!extraPath.empty()) {
        outFileName += "/" + extraPath;
        outFileName.pop_back();
      }
    }

    if (!outFileName.empty()) {
      if (gCfg["ndmspc"]["output"]["host"].is_string() &&
          !gCfg["ndmspc"]["output"]["host"].get<std::string>().empty()) {

        Printf("Deleting output eos directory '%s' ...", outFileName.c_str());
        std::string rmUrl =
            TString::Format("%s/proc/user/?mgm.cmd=rm&mgm.path=%s&mgm.option=rf&mgm.format=json&filetype=raw",
                            gCfg["ndmspc"]["output"]["host"].get<std::string>().c_str(), outFileName.c_str())
                .Data();

        if (fVerbose >= 2) Printf("rmUrl '%s' ...", rmUrl.c_str());
        TFile * f = Ndmspc::Utils::OpenFile(rmUrl.c_str());
        if (!f) return 1;
        Printf("Directory '%s' deleted", outFileName.c_str());
        f->Close();
      }
      else {
        Printf("Directory '%s' deleted", outFileName.c_str());
        gSystem->Exec(TString::Format("rm -rf %s", outFileName.c_str()));
      }
    }
    // gCfg["ndmspc"]["output"]["delete"] = "";
  }
  if (fVerbose >= 2) Printf("Ndmspc::PointRun::Init done ...");

  return true;
}

TList * PointRun::OpenInputs()
{
  ///
  /// Open Input objects
  ///

  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::OpenInputs");

  if (fInputFile && fInputList) return fInputList;

  if (gCfg["ndmspc"]["data"]["file"].get<std::string>().empty()) {
    Printf("Error: Input file is empty !!! Aborting ...");
    return nullptr;
  }
  if (fVerbose >= 0) Printf("Opening file '%s' ...", gCfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  fInputFile = Ndmspc::Utils::OpenFile(gCfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  if (!fInputFile) {
    Printf("Error: Cannot open file '%s' !", gCfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
    return nullptr;
  }

  if (!fInputList) fInputList = new TList();

  THnSparse *s, *stmp;
  for (auto & obj : gCfg["ndmspc"]["data"]["objects"]) {
    if (obj.get<std::string>().empty()) continue;

    std::string dirName;
    if (!gCfg["ndmspc"]["data"]["directory"].is_null() && gCfg["ndmspc"]["data"]["directory"].is_string())
      dirName = gCfg["ndmspc"]["data"]["directory"].get<std::string>();

    std::stringstream srcfull(obj.get<std::string>().c_str());

    std::string srcName, sparseName;

    getline(srcfull, srcName, ':');
    getline(srcfull, sparseName, ':');
    if (fVerbose >= 2) Printf("srcName=%s customName=%s", srcName.c_str(), sparseName.c_str());

    std::stringstream src(srcName.c_str());
    std::string       item;

    s = nullptr;
    while (getline(src, item, '+')) {

      std::string objName;
      if (!dirName.empty()) objName = dirName + "/";
      objName += item;
      if (fVerbose >= 1) Printf("Opening obj='%s' ...", objName.c_str());
      if (s == nullptr) {

        s = (THnSparse *)fInputFile->Get(objName.c_str());
        if (s == nullptr) {
          if (fVerbose >= 1) Printf("Warning: Cannot open object '%s' !!!", objName.c_str());
          continue;
        }

        if (s && !sparseName.empty()) s->SetName(sparseName.c_str());
      }
      else {
        if (fVerbose >= 1) Printf("Adding obj='%s' ...", objName.c_str());
        stmp = (THnSparse *)fInputFile->Get(objName.c_str());
        if (stmp == nullptr) {
          if (fVerbose >= 1) Printf("Warning: Cannot open object '%s' !!!", objName.c_str());
          continue;
        }
        if (s) s->Add(stmp);
      }
    }
    if (s) {
      fInputList->Add(s);
    }
    else {
      if (fVerbose >= 1)
        Printf("Warning : Could not open '%s' from file '%s' !!! Skipping ...", obj.get<std::string>().c_str(),
               gCfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
      // return nullptr;
    }
  }

  TFunction * fun = gROOT->GetGlobalFunction("NdmspcOpenInputsUser", nullptr, kTRUE);
  if (fun) {
    gROOT->ProcessLine(TString::Format("NdmspcOpenInputsUser((Ndmspc::PointRun*)%p),", this));
  }

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::OpenInputs");

  return fInputList;
}

THnSparse * PointRun::CreateResult()
{
  ///
  /// Create result object
  ///

  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::CreateResult fResultObject=%p", (void *)fResultObject);

  if (fResultObject) return fResultObject;

  THnSparse * s = (THnSparse *)fInputList->At(0);

  int nDimsParams   = 0;
  int nDimsCuts     = 0;
  int nDimsProccess = fCurrentProcessHistogramAxes.size();

  for (auto & cut : gCfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    nDimsCuts++;
  }

  for (auto & value : gCfg["ndmspc"]["result"]["axes"]) {
    nDimsParams++;
  }

  const Int_t              ndims = nDimsCuts + nDimsParams + nDimsProccess + 1;
  Int_t                    bins[ndims];
  Double_t                 xmin[ndims];
  Double_t                 xmax[ndims];
  std::vector<std::string> names;
  std::vector<std::string> titles;
  std::vector<TAxis *>     cutAxes;
  fMapAxesType = new TH1S("mapAxesType", "Type Axes Map", ndims, 0, ndims);
  // _currentOutputRootDirectory->Add

  Int_t nParameters = gCfg["ndmspc"]["result"]["parameters"]["labels"].size();
  if (nParameters <= 0) return nullptr;

  Int_t i = 0;

  bins[i] = nParameters;
  xmin[i] = 0;
  xmax[i] = nParameters;
  names.push_back("parameters");
  titles.push_back("parameters");
  fMapAxesType->GetXaxis()->SetBinLabel(i + 1, "par");

  i++;

  // cfg["ndmspc"]["output"]["post"]
  int iTmp = 0;
  for (auto & a : fCurrentProcessHistogramAxes) {
    // a->Print();
    bins[i] = a->GetNbins();
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax();
    // TODO: handle variable binning

    names.push_back(a->GetName());
    std::string t = a->GetTitle();
    if (t.empty()) t = a->GetName();
    titles.push_back(t);
    if (iTmp < 2)
      fMapAxesType->GetXaxis()->SetBinLabel(i + 1, "data");
    else
      fMapAxesType->GetXaxis()->SetBinLabel(i + 1, "sys-out");
    iTmp++;
    i++;
  }

  Int_t rebin       = 1;
  Int_t rebin_start = 1;
  for (auto & cut : gCfg["ndmspc"]["cuts"]) {
    if (fVerbose >= 3) std::cout << "CreateResult() : " << cut.dump() << std::endl;

    Int_t rebin         = 1;
    Int_t rebin_start   = 1;
    Int_t rebin_minimum = 1;
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
    if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();

    if (rebin_start > 1) {
      rebin_minimum = (rebin_start % rebin);
    }

    TAxis * a = (TAxis *)s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str());
    if (a == nullptr) return nullptr;

    if (rebin > 1) {
      a                = (TAxis *)a->Clone();
      Int_t rebins_max = a->GetNbins() / rebin;
      if ((a->GetNbins() - rebin_minimum + 1) % rebin == 0) rebins_max++;
      Double_t arr[rebins_max];

      // Printf("Axis '%s' : rebin_max=%d rebin_minimum=%d nbins=%d", a->GetName(), rebins_max, rebin_minimum,
      //        a->GetNbins());
      Int_t count = 0;
      Int_t i;
      for (i = rebin_minimum; i <= a->GetNbins(); i += rebin) {
        // Printf("%d %f", i, a->GetBinUpEdge(i));
        arr[count++] = a->GetBinLowEdge(i);
      }
      // Printf("%s %d %d", a->GetName(), count, rebins_max);
      if (count < rebins_max) arr[count++] = a->GetBinLowEdge(i);
      // for (Int_t i = 0; i < count; i++) {
      //   Printf("%s %d %f", a->GetName(), i, arr[i]);
      // }
      a->Set(count - 1, arr);
      // Printf("Axis '%s' : %d", a->GetName(), a->GetNbins());
    }
    bins[i] = a->GetNbins();
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax();

    cutAxes.push_back(a);
    names.push_back(a->GetName());
    std::string t = a->GetTitle();
    if (t.empty()) t = a->GetName();
    titles.push_back(t);
    fMapAxesType->GetXaxis()->SetBinLabel(i + 1, "proj");
    i++;
  }
  // exit(1);

  for (auto & value : gCfg["ndmspc"]["result"]["axes"]) {
    if (!value["labels"].is_null()) {
      bins[i] = value["labels"].size();
      xmin[i] = 0;
      xmax[i] = value["labels"].size();
    }
    else if (!value["ranges"].is_null()) {
      bins[i] = value["ranges"].size();
      xmin[i] = 0;
      xmax[i] = value["ranges"].size();
    }
    else {
      Printf("Error: 'labels' or 'ranges' is missing !!!");
      return nullptr;
    }

    names.push_back(value["name"].get<std::string>().c_str());
    titles.push_back(value["name"].get<std::string>().c_str());
    fMapAxesType->GetXaxis()->SetBinLabel(i + 1, "sys-in");
    i++;
  }

  fCurrentPointLabels = names;

  THnSparse * fres = new THnSparseD("results", "Final results", i, bins, xmin, xmax);
  if (!fres) {
    Printf("Error: Could not create 'results' THnSparse object !!!");
    return nullptr;
  }
  int     iAxis = 0;
  TAxis * a     = fres->GetAxis(iAxis);
  if (!a) {
    Printf("Error: 'parameters' axis was not found !!!");
    return nullptr;
  }
  int iLablel = 1;
  for (auto & n : gCfg["ndmspc"]["result"]["parameters"]["labels"]) {
    a->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
    a->SetBinLabel(iLablel++, n.get<std::string>().c_str());
  }

  iAxis++;

  // cfg["ndmspc"]["output"]["post"]
  // i         = nDimsCuts + iPar + 1;
  int iLabel;
  int iPIdx = 0;
  for (auto & axis : fCurrentProcessHistogramAxes) {
    if (axis->GetXbins()->GetArray()) fres->GetAxis(iAxis)->Set(axis->GetNbins(), axis->GetXbins()->GetArray());
    fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());

    for (iLabel = 1; iLabel < fCurrentProccessHistogram->GetAxis(iPIdx)->GetNbins() + 1; iLabel++) {
      std::string l = fCurrentProccessHistogram->GetAxis(iPIdx)->GetBinLabel(iLabel);
      if (!l.empty()) fres->GetAxis(iAxis)->SetBinLabel(iLabel, l.c_str());
    }
    fCurrentPoint[iAxis] = fCurrentProcessHistogramPoint[iPIdx];
    iPIdx++;
    iAxis++;
  }

  // i = 1;
  for (auto & a : cutAxes) {
    // Printf("%s", )
    fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
    if (a->GetXbins()->GetArray()) fres->GetAxis(iAxis)->Set(a->GetNbins(), a->GetXbins()->GetArray());
    iAxis++;
  }
  int iPar = 0;
  // int iLabel;
  // for (auto & [key, value] : cfg["ndmspc"]["result"].items()) {
  for (auto & value : gCfg["ndmspc"]["result"]["axes"]) {
    iPar++;
    iLabel = 1;
    if (!value["labels"].is_null()) {
      for (auto & n : value["labels"]) {
        fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
        fres->GetAxis(iAxis)->SetBinLabel(iLabel++, n.get<std::string>().c_str());
      }
    }
    iLabel = 1;
    if (!value["ranges"].is_null()) {
      for (auto & n : value["ranges"]) {
        fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
        fres->GetAxis(iAxis)->SetBinLabel(iLabel++, n["name"].get<std::string>().c_str());
      }
    }
    iAxis++;
  }

  if (fVerbose >= 2) fres->Print("all");
  // return nullptr;

  // TODO! port fres to fResultObject
  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::CreateResult fResultObject=%p", (void *)fres);
  return fres;
}

bool PointRun::ApplyCuts()
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::ApplyCuts");

  /// TODO! port it to std::string
  TString     titlePostfix = "";
  THnSparse * s;

  Int_t iCut        = 0;
  Int_t rebin       = 1;
  Int_t rebin_start = 1;

  fCurrentPoint[iCut] = 0;
  for (size_t i = 0; i < fInputList->GetEntries(); i++) {
    s    = (THnSparse *)fInputList->At(i);
    iCut = 1;
    for (auto & cut : gCfg["ndmspc"]["cuts"]) {

      if (cut.is_null()) continue;

      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

      if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
      if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();

      if (cut["axis"].is_string() && cut["axis"].get<std::string>().empty()) {
        std::cerr << "Error: Axis name is empty ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }
      if (cut["bin"]["min"].get<Int_t>() < 0 || cut["bin"]["max"].get<Int_t>() < 0) {
        std::cerr << "Error: Bin min or max is less then 0 ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }

      Int_t id = s->GetListOfAxes()->IndexOf(s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str()));
      if (id == s->GetListOfAxes()->GetEntries()) {
        Printf("Axis '%s' was not found !!! Skipping ...", cut["axis"].get<std::string>().c_str());
        return false;
      }

      Int_t binLocal = Utils::GetBinFromBase(cut["bin"]["min"].get<int>(), rebin, rebin_start);
      fCurrentPoint[iCut + fCurrentProcessHistogramPoint.size()] = binLocal;

      Int_t binMin = cut["bin"]["min"].get<Int_t>();
      Int_t binMax = cut["bin"]["max"].get<Int_t>();
      // Ndmspc::Utils::RebinBins(binMin, binMax, rebin);
      // Int_t binDiff = cut["bin"]["max"].get<Int_t>() - cut["bin"]["min"].get<Int_t>() + 1;
      // Int_t binMin  = binLocal;
      // Int_t binMax  = binLocal + rebin * binDiff - 1;
      if (fVerbose >= 2)
        Printf("cut=%s binLocal=%d binMin=%d binMax=%d", cut["axis"].get<std::string>().c_str(), binLocal, binMin,
               binMax);
      s->GetAxis(id)->SetRange(binMin, binMax);

      if (i == 0) {
        if (s->GetAxis(id)->IsAlphanumeric()) {
          titlePostfix +=
              TString::Format("%s[%s bin=%d] ", s->GetAxis(id)->GetName(), s->GetAxis(id)->GetBinLabel(binMin), binMin);
        }
        else {
          titlePostfix += TString::Format("%s[%.2f,%.2f] ", s->GetAxis(id)->GetName(),
                                          s->GetAxis(id)->GetBinLowEdge(binMin), s->GetAxis(id)->GetBinUpEdge(binMax));
        }
      }
      iCut++;
    }
  }
  if (!titlePostfix.IsNull()) {
    titlePostfix.Remove(titlePostfix.Length() - 1);
    Printf("Processing '%s' ...", titlePostfix.Data());
    gCfg["ndmspc"]["projection"]["title"] = titlePostfix.Data();
  }

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ApplyCuts");
  return true;
}
bool PointRun::ProcessSinglePoint()
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::ProcessSinglePoint");

  fIsProcessOk = false;

  if (!ApplyCuts()) return false;

  fIsSkipBin = false;

  if (fResultObject != nullptr) {
    delete fResultObject;
    fResultObject = nullptr;
  }
  fResultObject = CreateResult();
  // fResultObject->Print();

  json resultAxes = gCfg["ndmspc"]["result"]["axes"];

  std::vector<std::string> names;
  for (auto & value : resultAxes) {
    std::string n = value["name"].get<std::string>();
    names.push_back(n.c_str());
  }

  // OutputFileOpen(cfg);

  if (fVerbose >= 1) Printf("Starting User Process() ...");
  fBinCount++;
  bool ok = ProcessRecursiveInner(resultAxes.size() - 1, names);

  if (fVerbose >= 1) Printf("User Process() done ...");

  // Store add fResultObject to final file
  OutputFileClose();

  if (!fIsProcessOk) {
    if (fVerbose >= 0) Printf("Skipping ...");
  }

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ProcessSinglePoint");
  return true;
}
bool PointRun::ProcessRecursive(int i)
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::ProcessRecursive[%d]", i);

  if (i < 0) {
    return ProcessSinglePoint();
  }

  THnSparse * s = (THnSparse *)fInputList->At(0);
  TAxis *     a = (TAxis *)s->GetListOfAxes()->FindObject(gCfg["ndmspc"]["cuts"][i]["axis"].get<std::string>().c_str());
  if (a == nullptr) {
    Printf("Error: Axis canot be found");
    return false;
  }
  Int_t start         = 1;
  Int_t end           = a->GetNbins();
  Int_t rebin         = 1;
  Int_t rebin_start   = 1;
  Int_t rebin_minimum = 1;
  if (gCfg["ndmspc"]["cuts"][i]["rebin"].is_number_integer()) rebin = gCfg["ndmspc"]["cuts"][i]["rebin"].get<Int_t>();
  if (gCfg["ndmspc"]["cuts"][i]["rebin_start"].is_number_integer())
    rebin_start = gCfg["ndmspc"]["cuts"][i]["rebin_start"].get<Int_t>();

  gCfg["ndmspc"]["cuts"][i]["rebin_minimum"] = rebin_minimum;
  if (rebin > 1) end /= rebin;
  if (rebin_start > 1) {
    rebin_minimum                              = (rebin_start % rebin);
    gCfg["ndmspc"]["cuts"][i]["rebin_minimum"] = rebin_minimum;
    start                                      = (rebin_start / rebin) + 1;
    end                                        = (a->GetNbins() - rebin_minimum + 1) / rebin;
    // Printf("%s start=%d end=%d rebin=%d nbins=%d rebin_start=%d rebin_minimum=%d", a->GetName(), start, end, rebin,
    //        a->GetNbins(), rebin_start, rebin_minimum);
    // exit(1);
  }

  if (gCfg["ndmspc"]["process"]["ranges"].is_array()) {
    int range_min = gCfg["ndmspc"]["process"]["ranges"][i][0].get<int>();
    int range_max = gCfg["ndmspc"]["process"]["ranges"][i][1].get<int>();
    if (range_max > end || range_min < start || range_min > range_max || range_min > end || range_max < start) {
      Printf("Error: Process range is out of bounds histogram(after rebin)=[%d,%d] request=[%d,%d] or requested min is "
             "higher then requested max !!!",
             start, end, range_min, range_max);
      gSystem->Exit(1);
    }
    start = range_min;
    if (gCfg["ndmspc"]["process"]["ranges"][i][1] < end) end = range_max;
    // TODO: Handle rebin and rebin_start
  }

  for (Int_t iBin = start; iBin <= end; iBin++) {
    Int_t binMin = (iBin - 1) * rebin + rebin_minimum;
    Int_t binMax = (iBin * rebin) - 1 + rebin_minimum;
    if (fVerbose >= 2)
      Printf("axis=%s rebin=%d rebin_minimum=%d binMin=%d binMax=%d [%f,%f]", a->GetName(), rebin, rebin_minimum,
             binMin, binMax, a->GetBinLowEdge(binMin), a->GetBinUpEdge(binMax));
    gCfg["ndmspc"]["cuts"][i]["bin"]["min"] = binMin;
    gCfg["ndmspc"]["cuts"][i]["bin"]["max"] = binMax;
    ProcessRecursive(i - 1);
  }

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ProcessRecursive[%d]", i);

  return true;
}
bool PointRun::ProcessRecursiveInner(Int_t i, std::vector<std::string> & n)
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::ProcessRecursiveInner[%d]", i);

  if (fIsSkipBin) return false;

  if (!fResultObject) {
    fIsProcessExit = true;
    return false;
  }

  if (fIsProcessExit) gSystem->Exit(1);

  if (i < 0) {
    TList * outputList = new TList();
    SetOutputList(outputList);
    if (fBinCount == 1 && fVerbose >= 0 && !fCurrentPointValue.is_null())
      Printf("\tPoint: %s", fCurrentPointValue.dump().c_str());

    // TODO! Apply TMacro
    if (fVerbose >= 2) Printf("Running point macro '%s.C' ...", fMacro->GetName());
    /*fMacro.Exec();*/

    {
      /*TRedirectOutputGuard g{"/dev/null"};*/
      /*Longptr_t            ok = fMacro->Exec(TString::Format("(Ndmspc::PointRun*)%p", this));*/
      /*Longptr_t ok = fMacro->Exec(TString::Format("(Ndmspc::PointRun*)%p", this));*/
      Longptr_t ok = gROOT->ProcessLine(TString::Format("%s((Ndmspc::PointRun*)%p);", fMacro->GetName(), this));
      /*fMacro.Exec(TString::Format("(TList*)%p,(json*)%p", fInputList, &gCfg));*/

      /*fMacro.Exec(TString::Format("(TList*)%ld,(json&)%p,(THnSparse "*/
      /*                            "*)%ld,(int*)%ld,(std::vector<std::string>*)%ld,(json*)%ld,(TList*)%ld,0,0",*/
      /*                            (Longptr_t)fInputList, &gCfg, (Longptr_t)fResultObject,*/
      /*                            (Longptr_t)fCurrentPoint, (Longptr_t)&fCurrentPointLabels,*/
      /*                            (Longptr_t)&fCurrentPointValue, (Longptr_t)outputList)*/
      /*                .Data());*/
      /*bool ok = NdmspcPointMacro(_currentInputList, cfg, fResultObject, _currentPoint, _currentPointLabels,*/
      /*                           _currentPointValue, outputList, _currentSkipBin, _currentProcessExit);*/
      /*gSystem->Exit(0);*/
      if (ok && fVerbose >= 5) outputList->Print();
      if (ok) {
        fIsProcessOk = true;
      }
      else {
        return false;
      }
    }
    if (fCurrentOutputFile == nullptr) {
      OutputFileOpen();
    }

    TDirectory * outputDir = fCurrentOutputRootDirectory;

    int         iPoint = 1;
    std::string path;
    for (int iPoint = 1; iPoint < fResultObject->GetNdimensions(); iPoint++) {
      path += std::to_string(fCurrentPoint[iPoint]) + "/";
    }

    // if (gCfg["ndmspc"]["output"]["post"].is_string()) {
    //   std::string post = gCfg["ndmspc"]["output"]["post"].get<std::string>();
    //   if (!post.empty()) {
    //     path += post;
    //   }
    // }

    // Printf("path='%s'", path.c_str());

    fCurrentOutputRootDirectory->mkdir(path.c_str(), "", true);
    outputDir = fCurrentOutputRootDirectory->GetDirectory(path.c_str());

    outputDir->cd();
    outputList->Write();
    return true;
  }

  std::string axisName = gCfg["ndmspc"]["result"]["axes"][i]["name"].get<std::string>();
  if (!gCfg["ndmspc"]["result"]["axes"][i]["labels"].is_null()) {
    for (auto & v : gCfg["ndmspc"]["result"]["axes"][i]["labels"]) {
      TAxis * a                    = (TAxis *)fResultObject->GetListOfAxes()->FindObject(axisName.c_str());
      Int_t   id                   = fResultObject->GetListOfAxes()->IndexOf(a);
      fCurrentPoint[id]            = a->FindBin(v.get<std::string>().c_str());
      fCurrentPointValue[axisName] = v;
      fCurrentPointLabels[id]      = v.get<std::string>().c_str();
      ProcessRecursiveInner(i - 1, n);
    }
  }
  else if (!gCfg["ndmspc"]["result"]["axes"][i]["ranges"].is_null()) {
    for (auto & v : gCfg["ndmspc"]["result"]["axes"][i]["ranges"]) {
      TAxis * a                    = (TAxis *)fResultObject->GetListOfAxes()->FindObject(axisName.c_str());
      Int_t   id                   = fResultObject->GetListOfAxes()->IndexOf(a);
      fCurrentPoint[id]            = a->FindBin(v["name"].get<std::string>().c_str());
      fCurrentPointValue[axisName] = v;
      fCurrentPointLabels[id]      = v["name"].get<std::string>().c_str();
      ProcessRecursiveInner(i - 1, n);
    }
  }
  else {
    Printf("Error: ProcessRecursiveInner : No 'labels' or 'ranges' !!!");
    return false;
  }
  return true;

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ProcessRecursiveInner[%d]", i);
  return true;
}
void PointRun::OutputFileOpen()
{

  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::OutputFileOpen");

  // TString outputFileName;

  fCurrentOutputFileName = "";

  if (!gCfg["ndmspc"]["output"]["host"].get<std::string>().empty())
    fCurrentOutputFileName += gCfg["ndmspc"]["output"]["host"].get<std::string>().c_str();
  if (!gCfg["ndmspc"]["output"]["dir"].get<std::string>().empty())
    fCurrentOutputFileName += gCfg["ndmspc"]["output"]["dir"].get<std::string>().c_str();

  if (gCfg["ndmspc"]["cuts"].is_array() && !fCurrentOutputFileName.empty()) {

    // std::string axisName;
    // std::string rebinStr = "";
    // // cfgOutput["ndmspc"]["cuts"] = gCfg["ndmspc"]["cuts"];
    // for (auto & cut : gCfg["ndmspc"]["cuts"]) {
    //   if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    //   Int_t rebin         = 1;
    //   Int_t rebin_start   = 1;
    //   Int_t rebin_minimum = 1;
    //
    //   if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
    //   if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();
    //
    //   if (rebin_start > 1) {
    //     rebin_minimum = (rebin_start % rebin);
    //   }
    //
    //   if (axisName.length() > 0) {
    //     axisName += "_";
    //     rebinStr += "_";
    //   }
    //   axisName += cut["axis"].get<std::string>();
    //   rebinStr += std::to_string(rebin);
    //   rebinStr += "-";
    //   rebinStr += std::to_string(rebin_minimum);
    // }
    std::string cutsName = Utils::GetCutsPath(gCfg["ndmspc"]["cuts"]);
    // Printf("cutsName='%s'", cutsName.c_str());
    // exit(1);

    if (cutsName.length() > 0) {
      fCurrentOutputFileName += "/";
      fCurrentOutputFileName += gCfg["ndmspc"]["environment"].get<std::string>().c_str();
      fCurrentOutputFileName += "/";
      fCurrentOutputFileName += cutsName.c_str();
      // fCurrentOutputFileName += "/";
      fCurrentOutputFileName += "bins";
      fCurrentOutputFileName += "/";

      // TODO: check what is it used for. Remove it if not needed
      if (gCfg["ndmspc"]["output"]["post"].is_string()) {
        std::string post = gCfg["ndmspc"]["output"]["post"].get<std::string>();
        if (!post.empty()) {
          if (!fCurrentOutputFileName.empty() && fCurrentOutputFileName[fCurrentOutputFileName.size() - 1] != '/')
            fCurrentOutputFileName += "/";
          // _currentOutputFileName += "/bins/" + post;
          fCurrentOutputFileName += post;
        }
      }

      for (auto & cut : gCfg["ndmspc"]["cuts"]) {
        if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
        Int_t rebin         = 1;
        Int_t rebin_start   = 1;
        Int_t rebin_minimum = 1;

        if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
        if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();

        if (rebin_start > 1) {
          rebin_minimum = (rebin_start % rebin);
          if (rebin_minimum == 0) rebin_minimum = 1;
        }

        Int_t bin_min = cut["bin"]["min"].get<Int_t>();
        // Printf("bin_min=%d rebin_start=%d rebin=%d %d", bin_min, rebin_start, rebin,
        //        (bin_min - rebin_minimum) / rebin + 1);
        Int_t bin_min_converted = (bin_min - rebin_minimum) / rebin + 1;
        fCurrentOutputFileName += std::to_string(bin_min_converted) + "/";
      }
    }
  }
  else {
    fCurrentOutputFileName += gCfg["ndmspc"]["environment"].get<std::string>().c_str();
    fCurrentOutputFileName += "/";
  }

  if (!fCurrentOutputFileName.empty() && fCurrentOutputFileName[fCurrentOutputFileName.size() - 1] != '/')
    fCurrentOutputFileName += "/";

  if (!gCfg["ndmspc"]["output"]["file"].get<std::string>().empty())
    fCurrentOutputFileName += gCfg["ndmspc"]["output"]["file"].get<std::string>().c_str();

  fCurrentOutputFileName = gSystem->ExpandPathName(fCurrentOutputFileName.c_str());
  fCurrentOutputFile =
      Ndmspc::Utils::OpenFile(TString::Format("%s%s", fCurrentOutputFileName.c_str(),
                                              gCfg["ndmspc"]["output"]["opt"].get<std::string>().c_str())
                                  .Data(),
                              "RECREATE");
  // _currentOutputFile->cd();

  fCurrentOutputFile->mkdir("content");
  fCurrentOutputRootDirectory = fCurrentOutputFile->GetDirectory("content");
  fCurrentOutputRootDirectory->cd();

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::OutputFileOpen");
}
void PointRun::OutputFileClose()
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::OutputFileClose");

  if (fCurrentOutputFile == nullptr) return;

  if (fVerbose >= 2) Printf("Closing file '%s' ...", fCurrentOutputFileName.c_str());
  fCurrentOutputRootDirectory->Write();

  fCurrentOutputFile->cd();
  fResultObject->Write();
  fMapAxesType->Write();
  fCurrentOutputFile->Close();

  fCurrentOutputFile          = nullptr;
  fCurrentOutputRootDirectory = nullptr;

  if (fVerbose >= 0) Printf("Objects stored in '%s'", fCurrentOutputFileName.c_str());

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::OutputFileClose");
}
bool PointRun::Finish()
{

  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::Finish");
  if (fInputList) {
    fInputList->Clear();
    delete fInputList;
    fInputList = nullptr;
  }

  if (fInputFile) {
    fInputFile->Close();
    delete fInputFile;
    fInputFile = nullptr;
  }
  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::Finish");

  return true;
}
int PointRun::ProcessSingleFile()
{

  std::string type;
  if (gCfg["ndmspc"]["process"]["type"].is_string()) type = gCfg["ndmspc"]["process"]["type"].get<std::string>();

  if (type.empty()) {
    Printf("Warning: [ndmspc][process][type] is missing or is empty in configuration !!! Setting it ot 'single' ...");
    type                              = "single";
    gCfg["ndmspc"]["process"]["type"] = type;
  }

  TList * inputList = OpenInputs();
  if (inputList == nullptr) return 1;

  if (!type.compare("single")) {
    if (!ProcessSinglePoint()) return 3;
  }
  else if (!type.compare("all")) {
    json cuts;
    int  iCut = 0;
    for (auto & cut : gCfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      cuts[iCut++] = cut;
    }
    gCfg["ndmspc"]["cuts"] = cuts;

    ProcessRecursive(gCfg["ndmspc"]["cuts"].size() - 1);
  }
  else {
    Printf("Error: Value [process][type]='%s' is not supported !!! Exiting ...", type.c_str());
    return 4;
  }
  Finish();

  /*/// TODO! Handle failure*/
  /*Init();*/
  /*/// TODO! Handle failure*/
  /*OpenInputs();*/
  /**/
  /*Printf("Running point macro '%s.C' ...", fMacro.GetName());*/
  /*fMacro.Exec();*/
  return 0;
}
int PointRun::ProcessHistogramRun()
{

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ProcessHistogramRun");

  std::string fileNameHistogram = gCfg["ndmspc"]["data"]["histogram"]["file"].get<std::string>();
  std::string objName           = gCfg["ndmspc"]["data"]["histogram"]["obj"].get<std::string>();

  TFile * fProccessHistogram = Ndmspc::Utils::OpenFile(fileNameHistogram.c_str());
  if (!fProccessHistogram) {
    Printf("Error: Proccess input histogram file '%s' could not opened !!!", fileNameHistogram.c_str());
    return 1;
  }
  fCurrentProccessHistogram = (THnSparse *)fProccessHistogram->Get(objName.c_str());
  if (!fCurrentProccessHistogram) {
    Printf("Error: Proccess input histogram object '%s' could not opened !!!", objName.c_str());
    return 1;
  }
  TObjArray * axesArray = fCurrentProccessHistogram->GetListOfAxes();
  for (int iAxis = 0; iAxis < axesArray->GetEntries(); iAxis++) {

    TAxis * aTmp = (TAxis *)axesArray->At(iAxis);
    // Printf("axis=%s", aTmp->GetName());
    fCurrentProcessHistogramAxes.push_back(aTmp);
  }

  if (gCfg["ndmspc"]["data"]["histogram"]["bins"].is_array()) {

    for (auto & v : gCfg["ndmspc"]["data"]["histogram"]["bins"]) {
      Printf("%s", v.dump().c_str());
      int   i = 0;
      Int_t p[v.size()];
      for (auto & idx : v) {
        p[i] = idx;
        // _currentProcessHistogramPoint.push_back(idx);
        i++;
      }
      fCurrentProccessHistogram->SetBinContent(p, 1);
    }
  }
  /*fCurrentProccessHistogram->Print();*/
  if (fCurrentProccessHistogram->GetNbins()) {
    Int_t proccessPoint[fCurrentProccessHistogram->GetNdimensions()];
    for (int iBin = 0; iBin < fCurrentProccessHistogram->GetNbins(); iBin++) {
      fCurrentProcessHistogramPoint.clear();
      fCurrentProccessHistogram->GetBinContent(iBin, proccessPoint);
      // Printf("iBin=%d %d %d", iBin, proccessPoint[0], proccessPoint[1]);

      std::string path;
      for (auto & p : proccessPoint) {
        // printf("%d ", p);
        fCurrentProcessHistogramPoint.push_back(p);
        path += std::to_string(std::abs(p)) + "/";
      }
      // printf("\n");
      std::string fullPath = gCfg["ndmspc"]["data"]["histogram"]["base"].get<std::string>();
      fullPath += "/";
      fullPath += path;
      fullPath += gCfg["ndmspc"]["data"]["histogram"]["filename"].get<std::string>();
      // Printf("Path: %s %s", path.c_str(), fullPath.c_str());
      gCfg["ndmspc"]["data"]["file"] = fullPath;

      gCfg["ndmspc"]["output"]["post"] = path;

      // return 0;
      Init(path);
      Int_t rc = ProcessSingleFile();
      if (rc) {
        return rc;
      }
    }
  }
  else {
    Printf("Error: No entries in proccess histogram !!! Nothing to process !!!");
    return 1;
  }

  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::ProcessHistogramRun");
  return 0;
}
bool PointRun::Run(std::string filename, std::string userConfig, std::string environment, std::string userConfigRaw,
                   bool show, std::string outfilename)
{
  if (fVerbose >= 2) Printf("[<-] Ndmspc::PointRun::Run");

  if (!fMacro) return 1;

  if (!LoadConfig(filename, userConfig, environment, userConfigRaw, show, outfilename)) return false;
  /*fVerbose = 2;*/

  if (!gCfg["ndmspc"]["data"]["histogram"].is_null() && !gCfg["ndmspc"]["data"]["histogram"]["enabled"].is_null() &&
      gCfg["ndmspc"]["data"]["histogram"]["enabled"].get<bool>() == true) {
    ProcessHistogramRun();
  }
  else {
    ProcessSingleFile();
  }
  if (fVerbose >= 2) Printf("[->] Ndmspc::PointRun::Run");
  return true;
}

bool PointRun::Generate(std::string name, std::string inFile, std::string inObjectName)
{

  ///
  /// Generate point macro and config
  ///

  Printf("Genrating point run with name '%s' ...", name.c_str());
  json cfg = R"({

  "user": {
    "proj": 0,
    "minEntries": 1,
    "verbose": 0
  },
  "ndmspc": {
    "environments": {
      "local": {
        "output": { "dir": "$HOME/.ndmspc/analyses/generated", "host": "" }
      },
    },
    "environment": "local",
    "data": {
      "file": "input.root",
      "objects": ["hNSparse"]
    },
    "cuts": [],
    "result": {
      "parameters": { "labels": ["Integral"], "default": "Integral" }
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "content.root",
      "opt": "?remote=1"
    },
    "process": {
      "type": "single"
    },
    "log": {
      "type": "error-only",
      "dir": "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"
    },
    "job":{
      "inputs": []
    },
    "verbose": 0
  }
})"_json;

  std::string macroTemplateHeader = R""""(
#include <TROOT.h>
#include <TList.h>
#include <THnSparse.h>
#include <TH1D.h>
#include <ndmspc/PointRun.h>
#include <ndmspc/Utils.h>
)"""";

  std::string macroTemplate = R""""(
{
  json                     cfg          = pr->Cfg();
  TList *                  inputList    = pr->GetInputList();
  THnSparse *              resultObject = pr->GetResultObject();
  Int_t *                  point        = pr->GetCurrentPoint();
  std::vector<std::string> pointLabels  = pr->GetCurrentPointLabels();
  json                     pointValue   = pr->GetCurrentPointValue();
  TList *                  outputList   = pr->GetOutputList();
  
  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }
  
  THnSparse * hs = (THnSparse *)inputList->At(0);

  int projId = cfg["user"]["proj"].get<int>();
  TH1D *      h  = hs->Projection(projId, "O");
  
  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();  
  h->SetNameTitle("h", TString::Format("h - %s", titlePostfix.Data()).Data());
  outputList->Add(h);

  // Skip bin (do not save any output)
  if (h->GetEntries() < cfg["user"]["minEntries"].get<int>()) 
    return false; 

  Double_t integral = h->Integral();
  if (verbose >= 0)
    Printf("Integral = %f ", integral);


  if (resultObject) {
     Ndmspc::Utils::SetResultValueError(cfg, resultObject, "Integral", point, integral, TMath::Sqrt(integral), false, true);
  }

  if (!gROOT->IsBatch() && !cfg["ndmspc"]["process"]["type"].get<std::string>().compare("single")) {
    h->DrawCopy();
  }

  return true;
}

)"""";

  cfg["ndmspc"]["data"]["file"]    = inFile.c_str();
  cfg["ndmspc"]["data"]["objects"] = {inObjectName.c_str()};

  if (cfg["ndmspc"]["cuts"].size() == 0) {
    // Generate all axis
    // {"enabled": false, "axis": "hNSparseAxisName", "bin" : {"min":3, "max": 3}, "rebin":1}
    TFile * tmpFile = Ndmspc::Utils::OpenFile(inFile.c_str());
    if (!tmpFile) {
      Printf("Error: Problem opening file '%s' !!! Exiting ...", inFile.c_str());
      return 1;
    }

    THnSparse * tmpSparse = (THnSparse *)tmpFile->Get(inObjectName.c_str());
    if (!tmpSparse) {
      Printf("Error: Problem opening object '%s' !!! Exiting ...", inObjectName.c_str());
      return 1;
    }
    tmpSparse->ls();
    TObjArray * axes = tmpSparse->GetListOfAxes();
    TAxis *     a;
    for (int i = 0; i < axes->GetEntries(); i++) {
      a = (TAxis *)axes->At(i);
      if (!a) continue;
      // a->Print();
      Printf("Init axis '%s' with enabled=false", a->GetName());
      cfg["ndmspc"]["cuts"][i]["enabled"]    = false;
      cfg["ndmspc"]["cuts"][i]["axis"]       = a->GetName();
      cfg["ndmspc"]["cuts"][i]["bin"]["min"] = 1;
      cfg["ndmspc"]["cuts"][i]["bin"]["max"] = 1;
      cfg["ndmspc"]["cuts"][i]["rebin"]      = 1;
    }

    tmpFile->Close();
  }

  std::string   outputConfig = name + ".json";
  std::ofstream fileConfig(outputConfig.c_str());
  fileConfig << std::setw(2) << cfg << std::endl;

  std::string   outputMacro = name + ".C";
  std::ofstream file(outputMacro.c_str());
  file << macroTemplateHeader.c_str();
  file << "bool " << name.c_str() << "(Ndmspc::PointRun *pr)";
  file << macroTemplate.c_str();

  Printf("File '%s.C' and '%s.json' were generated ...", name.c_str(), name.c_str());
  return true;
}

bool PointRun::Merge(std::string config, std::string userConfig, std::string environment, std::string userConfigRaw,
                     std::string fileOpt)
{
  ///
  /// Merge specific projection
  ///

  if (!Core::LoadConfig(config, userConfig, environment, userConfigRaw)) return false;

  if (gCfg["ndmspc"]["output"]["host"].get<std::string>().empty()) {
    gCfg["ndmspc"]["output"]["opt"] = "";
  }

  std::string hostUrl = gCfg["ndmspc"]["output"]["host"].get<std::string>();
  // if (hostUrl.empty()) {
  //   Printf("Error:  cfg[ndmspc][output][host] is empty!!!");
  //   return 2;
  // }
  std::string path;
  if (!hostUrl.empty()) path = hostUrl + "/";
  path += gCfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  path += environment + "/";

  // int nDimsCuts = 0;
  // std::string rebinStr  = "";
  // for (auto & cut : gCfg["ndmspc"]["cuts"]) {
  //   if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
  //   path += cut["axis"].get<std::string>() + "_";
  //   rebinStr += std::to_string(cut["rebin"].get<Int_t>()) + "_";
  //   nDimsCuts++;
  // }
  // path[path.size() - 1] = '/';
  // path += rebinStr;
  // path[path.size() - 1] = '/';
  path += Utils::GetCutsPath(gCfg["ndmspc"]["cuts"]);

  path = gSystem->ExpandPathName(path.c_str());

  std::string outFile = path + "results.root";

  path += "bins";

  TUrl        url(path.c_str());
  std::string outHost        = url.GetHost();
  std::string inputDirectory = url.GetFile();
  std::string linesMerge     = "";

  if (outHost.empty()) {
    if (gSystem->AccessPathName(path.c_str())) {
      Printf("Error: Nothing to merge, because path '%s' does not exist !!!", path.c_str());
      return false;
    }
    Printf("Doing local find %s -name %s", path.c_str(), gCfg["ndmspc"]["output"]["file"].get<std::string>().c_str());
    linesMerge = gSystem->GetFromPipe(
        TString::Format("find %s -name %s", path.c_str(), gCfg["ndmspc"]["output"]["file"].get<std::string>().c_str())
            .Data());
    // Printf("%s", linesMerge.c_str());
    // gSystem->Exit(1);
  }
  else {

    Printf("Doing eos find -f %s", path.c_str());

    std::string contentFile = gCfg["ndmspc"]["output"]["file"].get<std::string>();
    TString     findUrl;

    // Vector of string to save tokens
    std::vector<std::string> tokens;

    if (!inputDirectory.empty()) {
      findUrl = TString::Format(
          "root://%s//proc/user/?mgm.cmd=find&mgm.find.match=%s&mgm.path=%s&mgm.format=json&mgm.option=f&filetype=raw",
          outHost.c_str(), contentFile.c_str(), inputDirectory.c_str());

      TFile * f = Ndmspc::Utils::OpenFile(findUrl.Data());
      if (!f) return 1;

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
      while (getline(check1, intermediate, '&')) {
        tokens.push_back(intermediate);
      }
    }
    else {
      tokens.push_back(contentFile.c_str());
    }
    linesMerge = tokens[0];
  }
  std::stringstream check2(linesMerge);
  std::string       line;
  std::string       outFileLocal = "/tmp/ndmspc-merged-" + std::to_string(gSystem->GetPid()) + ".root";
  bool              copy         = true;
  if (hostUrl.empty()) {
    outFileLocal = outFile;
    copy         = false;
  }
  outFileLocal = gSystem->ExpandPathName(outFileLocal.c_str());

  TFileMerger m(kFALSE);
  m.OutputFile(TString::Format("%s%s", outFileLocal.c_str(), fileOpt.c_str()));
  // m.AddObjectNames("results");
  // m.AddObjectNames("content");
  // Int_t default_mode = TFileMerger::kAll | TFileMerger::kIncremental;
  // Int_t mode         = default_mode | TFileMerger::kOnlyListed;
  while (std::getline(check2, line)) {

    Printf("Adding file '%s' ...", line.data());
    if (!outHost.empty()) {
      m.AddFile(TString::Format("root://%s/%s", outHost.c_str(), line.c_str()).Data());
    }
    else {
      m.AddFile(line.c_str());
    }
  }

  Printf("Merging ...");
  m.Merge();
  // m.PartialMerge(mode);
  if (copy) {
    Printf("Copy '%s' to '%s' ...", outFileLocal.c_str(), outFile.c_str());
    TFile::Cp(outFileLocal.c_str(), outFile.c_str());
    std::string rm = "rm -f " + outFileLocal;
    Printf("Doing '%s' ...", rm.c_str());
    gSystem->Exec(rm.c_str());
  }
  Printf("Output: '%s'", outFile.c_str());
  Printf("Done ...");

  return true;
}

} // namespace Ndmspc
