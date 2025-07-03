#include <map>
#include <vector>
#include <NHnSparseTreeThreadData.h>
#include <THnSparse.h>
#include <TSystem.h>
#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include <TH1.h>
#include <TROOT.h>
#include "NDimensionalExecutor.h"
#include "NLogger.h"
#include "NTreeBranch.h"
#include "NUtils.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreeInfo.h"
#include "ROOT/RConfig.hxx"
#include "RtypesCore.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTree);
/// \endcond

namespace Ndmspc {

NHnSparseTree::NHnSparseTree() : THnSparse()
{
  ///
  /// Default constructor
  ///
}

NHnSparseTree::NHnSparseTree(const char * name, const char * title, Int_t dim, const Int_t * nbins,
                             const Double_t * xmin, const Double_t * xmax, Int_t chunksize)
    : THnSparse(name, title, dim, nbins, xmin, xmax, chunksize)
{
  ///
  /// Constructor
  ///

  InitBinnings({});
}

NHnSparseTree::NHnSparseTree(const std::string & filename, const std::string & treename) : THnSparse()
{
  ///
  /// Constructor
  ///
  InitTree(filename, treename);
  fPrefix  = gSystem->GetDirName(fFileName.c_str());
  fPostfix = gSystem->BaseName(fFileName.c_str());
}

NHnSparseTree::~NHnSparseTree()
{
  ///
  /// Destructor
  ///
  SafeDelete(fPointData);
  SafeDelete(fBinning);
}
NHnSparseTree * NHnSparseTree::Open(const std::string & filename, const std::string & branches,
                                    const std::string & treename)
{
  ///
  /// Load
  ///

  NLogger::Info("Opening '%s' with branches='%s' and treename='%s' ...", filename.c_str(), branches.c_str(),
                treename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogger::Error("NHnSparseTree::Open: Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogger::Error("NHnSparseTree::Open: Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  NHnSparseTreeInfo * hnstInfo = (NHnSparseTreeInfo *)tree->GetUserInfo()->FindObject("hnstInfo");
  if (!hnstInfo) {
    NLogger::Error("NHnSparseTree::Open: Cannot get HnSparseTree from tree '%s'", treename.c_str());
    return nullptr;
  }

  NHnSparseTree * hnst = hnstInfo->GetHnSparseTree();
  if (!hnst->SetFileTree(file, tree, true)) return nullptr;

  if (!hnst->InitBinnings({})) return nullptr;
  // hnst->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    NLogger::Trace("Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnst->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnst->fBranchesMap) {
      NLogger::Debug("Enabled branches: %s", kv.first.c_str());
    }
  }
  // Set all branches to be read
  hnst->SetBranchAddresses();
  hnst->GetPoint()->SetHnSparseTree(hnst);

  return hnst;
}

void NHnSparseTree::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  // THnSparse::Print(option);
  TString opt(option);

  // auto logger = Ndmspc::Logger::getInstance("");
  NLogger::Info("HnSparseTree name='%s' title='%s'", fName.Data(), fTitle.Data());
  NLogger::Info("THnSparse:");
  NLogger::Info("  dimensions: %d", fNdimensions);
  NLogger::Info("  filled bins = %lld", GetNbins());
  // print axes name nbins min max in one line
  for (Int_t i = 0; i < GetNdimensions(); i++) {
    TAxis * a = GetAxis(i);
    if (a->IsAlphanumeric()) {
      // build string with all labels coma separated and put in brackets
      std::string labels;
      for (int j = 1; j <= a->GetNbins(); j++) {
        labels += a->GetBinLabel(j);
        labels += ",";
      }
      if (labels.back() == ',') {
        labels.pop_back();
      }
      NLogger::Info("  [%d] name='%s' title='%s' nbins=%d labels=[%s]", i, a->GetName(), a->GetTitle(), a->GetNbins(),
                    labels.c_str());
    }
    else {
      NLogger::Info("  [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, a->GetName(), a->GetTitle(),
                    a->GetNbins(), a->GetXmin(), a->GetXmax());
    }
  }
  NLogger::Info("TTree:");
  NLogger::Info("  filename='%s'", fFileName.c_str());
  NLogger::Info("  tree name='%s'", fTree ? fTree->GetName() : "n/a");
  NLogger::Info("  tree entries=%lld", fTree ? fTree->GetEntries() : -1);
  NLogger::Info("  prefix='%s'", fPrefix.c_str());
  NLogger::Info("  postfix='%s'", fPostfix.c_str());
  NLogger::Info("  branches: [%d]", fBranchesMap.size());
  for (auto & kv : fBranchesMap) {
    kv.second.Print();
  }

  if (fBinning) fBinning->Print();
  if (fPointData) fPointData->Print("A");

  if (opt.Contains("P")) {
    NLogger::Info("Printing content ...");
    std::vector<int> mins;
    std::vector<int> maxs;
    std::vector<int> ids;
    std::string      header = "| content | ";

    Long64_t nShowMax = 100;
    Long64_t nBins    = 0;
    for (int i = 0; i < GetNdimensions(); i++) {
      mins.push_back(1);
      int max = GetAxis(i)->GetNbins();
      nBins += max;
      if (nBins > nShowMax) max = 1;
      maxs.push_back(max);
      ids.push_back(i);
      header += GetAxis(i)->GetName();
      header += " | ";
    }
    bool allBins   = opt.Contains("A");
    bool urlike    = opt.Contains("U");
    bool forceBins = opt.Contains("B");

    if (!urlike) NLogger::Info("%s", header.c_str());
    NDimensionalExecutor executor(mins, maxs);
    auto                 loop_task = [this, allBins, urlike, forceBins](const std::vector<int> & coords) {
      Int_t p[GetNdimensions()];
      NUtils::VectorToArray(coords, p);

      double content = GetBinContent(p);
      // content        = 1;
      if (urlike) {
        if (allBins || content > 0) {

          std::string pointStr  = GetFullPath(coords);
          std::string coordsStr = NUtils::GetCoordsString(coords, -1);
          NLogger::Info("[%1s] %s\t%s", content > 0 ? "*" : " ", coordsStr.c_str(), pointStr.c_str());
        }
      }
      else {
        if (allBins || content > 0) {
          // Print current point
          std::string pointStr = GetPointStr(coords);
          NLogger::Info("| %.0f %s", content, pointStr.c_str());
        }
      }
    };
    executor.Execute(loop_task);
  }
}

std::string NHnSparseTree::GetFullPath(std::vector<int> coords) const
{

  ///
  /// Return full path
  ///
  std::string pointStr = fPrefix;
  pointStr += "/";
  for (int i = 0; i < GetNdimensions(); i++) {
    TAxis * a = GetAxis(i);
    if (a->IsAlphanumeric()) {
      pointStr += a->GetBinLabel(coords[i]);
    }
    else {
      pointStr += std::to_string(coords[i]);
    }
    pointStr += "/";
  }
  pointStr += fPostfix;
  return pointStr;
}
std::string NHnSparseTree::GetPointStr(const std::vector<int> & coords) const
{
  ///
  /// Return point string
  ///
  std::string pointStr = "| ";
  for (int i = 0; i < GetNdimensions(); i++) {
    TAxis * a = GetAxis(i);
    if (a->IsAlphanumeric()) {
      pointStr += a->GetBinLabel(coords[i]);
    }
    else {
      pointStr += std::to_string(coords[i]);
      // TString::Format("%d [%.3f,%.3f]", coords[i], a->GetBinLowEdge(coords[i]), a->GetBinUpEdge(coords[i])).Data();
    }
    pointStr += " | ";
  }
  // check if pointStr has space and remove it
  if (pointStr.back() == ' ') {
    pointStr.pop_back();
  }

  return pointStr;
}
void NHnSparseTree::GetPointMinMax(const std::vector<int> & coords, std::vector<double> & min,
                                   std::vector<double> & max) const
{
  ///
  /// Return point min and max
  ///

  // loop over all axes and check if coords are valid
  for (int i = 0; i < GetNdimensions(); i++) {
    TAxis * a = GetAxis(i);

    min[i] = a->GetBinLowEdge(coords[i]);
    max[i] = a->GetBinUpEdge(coords[i]);
  }
}

bool NHnSparseTree::SetFileTree(TFile * file, TTree * tree, bool force)
{
  ///
  /// Set File and tree
  ///
  if (!file || !tree) {
    NLogger::Error("File or tree is nullptr !!!");
    return false;
  }
  fFile = file;
  fTree = tree;
  if (fPrefix.empty() || force) fPrefix = gSystem->GetDirName(file->GetName());
  if (fPostfix.empty()) fPostfix = gSystem->BaseName(file->GetName());
  // print prefix and postfix
  return true;
}

bool NHnSparseTree::InitTree(const std::string & filename, const std::string & treename)
{
  ///
  /// Init tree
  ///

  // Set filename
  if (!filename.empty()) {
    fFileName = gSystem->ExpandPathName(filename.c_str());
  }

  NLogger::Trace("Initializing tree '%s' using filename '%s' ...", treename.c_str(), fFileName.c_str());

  // Open file
  fFile = NUtils::OpenFile(fFileName.c_str(), "RECREATE");
  if (!fFile) {
    NLogger::Error("Cannot open file '%s'", fFileName.c_str());
    return false;
  }

  fTree = new TTree(treename.c_str(), "hnst tree");
  if (!fTree) {
    NLogger::Error("Cannot create tree '%s' using file '%s' !!!", treename.c_str(), fFileName.c_str());
    return false;
  }
  if (fPrefix.empty()) fPrefix = gSystem->GetDirName(fFileName.c_str());
  if (fPostfix.empty()) fPostfix = gSystem->BaseName(fFileName.c_str());

  return true;
}
bool NHnSparseTree::InitAxes(TObjArray * newAxes, int n)
{
  ///
  /// Init axes
  ///
  NLogger::Trace("Initializing axes [%d]...", newAxes->GetEntries());

  if (newAxes == nullptr) {
    NLogger::Error("NHnSparseTree::InitAxes: newAxes is nullptr !!!");
    return false;
  }
  if (fBinning == nullptr) {
    std::vector<TAxis *> axes;
    for (Int_t i = 0; i < newAxes->GetEntries(); i++) {

      TAxis * a = (TAxis *)newAxes->At(i);
      if (a == nullptr) {
        NLogger::Error("NHnSparseTree::InitAxes : Axis %d is nullptr !!!", i);
        return false;
      }
      NLogger::Trace("Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, a->GetName(), a->GetTitle(),
                     a->GetNbins(), a->GetXmin(), a->GetXmax());
      axes.push_back((TAxis *)a->Clone());
    }
    NLogger::Trace("Creating new binning form new axes [%d]...", axes.size());
    fBinning = new NBinning(axes);
    // fBinning->Print();
    if (fPointData) delete fPointData;
    fPointData = new NHnSparseTreePoint(this);
  }
  fPointData->Reset();
  for (Int_t i = 0; i < newAxes->GetEntries(); i++) {
    TAxis * a = (TAxis *)newAxes->At(i);
    TString axisname(a->GetName());
    bool    isUser = axisname.Contains("/U");
    if (isUser) {
      axisname.ReplaceAll("/U", "");
      a->SetName(axisname.Data());
    }
  }

  Init("hnTree", "HnSparseTree", newAxes, kTRUE);

  return true;
}
void NHnSparseTree::SaveEntry(NHnSparseTree * hnstIn, std::vector<std::vector<int>> ranges, bool useProjection)
{
  ///
  /// Save entry
  ///
  NLogger::Trace("Saving entry in HnSparseTree ...");

  for (auto & kv : fBranchesMap) {
    NLogger::Trace("Saving content from %s ...", kv.first.c_str());
    if (hnstIn) {
      THnSparse * in = (THnSparse *)hnstIn->GetBranch(kv.first)->GetObject();
      if (ranges.size() > 0) {
        NUtils::SetAxisRanges(in, ranges);
      }
      kv.second.Branch(fTree, nullptr);
      kv.second.SaveEntry(hnstIn->GetBranch(kv.first), useProjection);
    }
    else {
      kv.second.SaveEntry(GetBranch(kv.first), false);
    }
  }

  // Filling entry to tree
  Int_t nBytes = FillTree();
  NLogger::Debug("[entry=%lld] Bytes written : %.3f MB file='%s'", fTree->GetEntries() - 1,
                 (Double_t)nBytes / (1024 * 1024), fTree->GetCurrentFile()->GetName());
}

Int_t NHnSparseTree::FillTree()
{
  ///
  /// Fill tree
  ///
  // NLogger::Info("Filling tree ...");

  if (fTree == nullptr) {
    NLogger::Error("Tree is not initialized !!! Run 'HnSparseTree::InitTree(...)' first !!!");
    return -1;
  }

  // fPointData->Print();
  Int_t * point = new Int_t[GetNdimensions()];
  NUtils::VectorToArray(fPointData->GetPointStorage(), point);
  point[GetNdimensions() - 1] = 1; // Set last dimension to entry number
  std::string pointStr        = NUtils::GetCoordsString(NUtils::ArrayToVector(point, GetNdimensions()));
  NLogger::Trace("Filling tree with point: %s", pointStr.c_str());
  SetBinContent(point, 1);

  Int_t   contentDim   = fBinning->GetContent()->GetNdimensions();
  Int_t * contentPoint = new Int_t[contentDim];
  NUtils::VectorToArray(fPointData->GetPointContent(), contentPoint);
  fBinning->GetContent()->SetBinContent(contentPoint, 1);
  SetEntries(fTree->GetEntries() + 1);
  delete[] point;

  fFile->cd();
  return fTree->Fill();
}

Long64_t NHnSparseTree::GetEntry(Long64_t entry)
{
  ///
  /// Get entry
  ///

  NLogger::Trace("Getting entry=%lld nbranches=%d ...", entry, fBranchesMap.size());
  // fTree->Print();
  // Print warning if entry is out of bounds and return 0
  if (entry < 0 || entry >= fTree->GetEntries()) {
    NLogger::Warning("Entry %lld is out of bounds [0, %lld). Reading 0 bytes and objects remain from last valid entry.",
                     entry, fTree->GetEntries() - 1);
    return 0;
  }

  Long64_t bytessum = 0;

  for (auto & kv : fBranchesMap) {
    NLogger::Trace("Getting content from '%s' branch ...", kv.first.c_str());
    if (kv.second.GetBranch() == nullptr) {
      NLogger::Error("Branch '%s' is not initialized !!!", kv.first.c_str());
      continue;
    }
    // check if branch is enabled
    if (kv.second.GetBranchStatus() == 0) {
      NLogger::Trace("Branch '%s' is disabled !!! Skipping ...", kv.first.c_str());
      continue;
    }
    bytessum += kv.second.GetEntry(fTree, entry);
  }
  Int_t * point = new Int_t[fBinning->GetContent()->GetNdimensions()];
  fBinning->GetContent()->GetBinContent(entry, point);
  fPointData->SetPointContent(NUtils::ArrayToVector(point, fBinning->GetContent()->GetNdimensions()));
  delete[] point;

  // Print byte sum
  NLogger::Debug("[entry=%lld] Bytes read : %.3f MB file='%s'", entry, (double)bytessum / (1024 * 1024),
                 fFileName.c_str());
  return bytessum;
}

bool NHnSparseTree::Close(bool write)
{
  ///
  /// Close
  ///

  if (fFile) {
    if (write) {
      fFile->cd();

      TList * userInfo = fTree->GetUserInfo();
      SetNameTitle("hnstMap", "HnSparseTree mapping");
      userInfo->Add((THnSparse *)Clone());
      NHnSparseTreeInfo * info = new NHnSparseTreeInfo();
      // info->SetHnSparseTree(this);
      info->SetHnSparseTree((NHnSparseTree *)Clone());
      userInfo->Add(info);

      fTree->Write("", TObject::kOverwrite);
      fFile->Close();
      NLogger::Info("Output was stored in file '%s'", fFileName.c_str());
    }
    else {
      fFile->Close();
      NLogger::Info("File '%s' was closed", fFileName.c_str());
    }
    fFile = nullptr;
    fTree = nullptr;
  }
  else {
    NLogger::Error("File is nullptr !!!");
    return false;
  }
  return true;
}

bool NHnSparseTree::AddBranch(const std::string & name, void * address, const std::string & className)
{

  if (fBranchesMap.find(name) != fBranchesMap.end()) {
    return fBranchesMap[name].GetBranch();
  }

  if (fTree == nullptr) {
    NLogger::Error("Tree is not initialized !!! Run 'HnSparseTree::InitTree(...)' first !!!");
    return false;
  }

  fBranchesMap[name] = NTreeBranch(fTree, name, address, className);
  return true;
}
TObject * NHnSparseTree::GetBranchObject(const std::string & name)
{
  ///
  /// Return branch object
  ///
  auto * branch = GetBranch(name);
  if (!branch) {
    return nullptr;
  }
  return branch->GetObject();
}

void NHnSparseTree::SetBranchAddresses()
{
  ///
  /// Set branch addresses
  ///

  if (!fTree) {
    NLogger::Error("Tree is nullptr !!!");
    return;
  }

  // NLogger::Trace("Setting branch addresses ...");

  // Print size of branches map
  NLogger::Trace("NHnSparseTree::SetBranchAddresses: Setting branch addresses for %d branches ...",
                 fBranchesMap.size());
  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {
    NLogger::Trace("NHnSparseTree::SetBranchAddresses: Setting branch address '%s' ...", kv.first.c_str());
    kv.second.SetBranchAddress(fTree);
  }
}

void NHnSparseTree::SetEnabledBranches(std::vector<std::string> branches)
{
  ///
  /// Enable branches
  ///

  if (branches.empty()) {
    NLogger::Trace("Enabling all branches ...");
    return;
  }

  // if (!fTree) {
  //   NLogger::Error("Tree is nullptr !!!");
  //   return;
  // }
  //
  // fTree->SetBranchStatus("*", 0);

  // loop over all branches
  for (auto & kv : fBranchesMap) {
    if (branches.empty() || std::find(branches.begin(), branches.end(), kv.first) != branches.end()) {
      NLogger::Trace("Enabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(1);
      // fTree->SetBranchStatus(kv.first.c_str(), 1);
    }
    else {
      NLogger::Trace("Disabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(0);
      // fTree->SetBranchStatus(kv.first.c_str(), 0);
    }
  }
}

bool NHnSparseTree::InitBinnings(std::vector<TAxis *> axes)
{
  ///
  /// Init binnings
  ///

  if (fBinning) {
    NLogger::Trace("Binning already initialized ...");
    return true;
  }
  if (axes.empty()) {
    int dim = GetNdimensions();
    for (int i = 0; i < dim; i++) {
      axes.push_back(GetAxis(i));
    }
  }
  // if (fBinning) {
  //   delete fBinning;
  // }
  fBinning = new NBinning(axes);
  if (fPointData) delete fPointData;
  fPointData = new NHnSparseTreePoint(this);
  return true;
}

std::vector<std::string> NHnSparseTree::GetBrancheNames()
{
  ///
  /// Get branch names
  ///
  std::vector<std::string> keys;
  for (auto & kv : fBranchesMap) {
    keys.push_back(kv.first);
  }
  return keys;
}
bool NHnSparseTree::Import(std::string filename, std::string directory, std::vector<std::string> objNames,
                           std::map<std::string, std::vector<std::vector<int>>> binning)
{
  ///
  /// Import from THnSparse
  ///
  std::string cachedir = TFile::GetCacheFileDir();
  TFile *     f        = NUtils::OpenFile(filename.c_str(), cachedir.empty() ? "READ" : "CACHEREAD");
  if (f == nullptr) {
    NLogger::Error("Cannot open file '%s'", filename.c_str());
    return false;
  }
  // loop over objNames and print content
  std::map<std::string, THnSparse *> objects;
  for (size_t i = 0; i < objNames.size(); i++) {
    std::string objName = directory;
    if (!directory.empty()) {
      objName += "/";
    }
    objName += objNames[i];
    NLogger::Info("Initializing 'hnst' from %s", objName.c_str());
    // THnSparse * hns = (THnSparse *)f->Get(objName.c_str());
    TObject *   obj      = f->Get(objName.c_str());
    THnSparse * hns      = dynamic_cast<THnSparse *>(obj);
    objects[objNames[i]] = hns;
    if (hns == nullptr) {
      NLogger::Warning("Cannot find object '%s' in file '%s'!!! Skipping ...", objName.c_str(), filename.c_str());
      continue;
    }
    if (GetNdimensions() == 0) {
      // Add default binning (integrated binning)
      InitAxes(hns->GetListOfAxes());
      fBinning->GetMap()->Reset();
      fBinning->SetDefinition(binning);
      // add binningIn for reset of axes
      for (int iAxis = 0; iAxis < hns->GetNdimensions(); iAxis++) {
        // print axis
        TAxis *     axis     = hns->GetAxis(iAxis);
        std::string axisName = axis->GetName();
        // NLogger::Debug("XXXXXXX Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", iAxis, axisName.c_str(),
        //                axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
        if (!binning[axisName].empty()) {
          // if (axisName == "axis1-pt" || axisName == "axis2-ce" || axisName == "axis5-eta") {
          fBinning->AddBinningViaBinWidths(iAxis + 1, binning[axisName]);
          fBinning->GetDefinition()[axisName] = binning[axisName];
        }
        else {
          fBinning->AddBinning(iAxis + 1, {axis->GetNbins(), 1, 1}, 1);
          fBinning->GetDefinition()[axisName] = {{axis->GetNbins()}};
        }
      }
      fBinning->FillAll();
      InitAxes(fBinning->GetListOfAxes());
    }
    // print class name
    std::string className = obj->IsA()->GetName();
    NLogger::Debug("Object '%s' class='%s'", objName.c_str(), obj->IsA()->GetName());
    if (obj->IsA()->InheritsFrom("THnSparseD"))
      className = "THnSparseD";
    else if (obj->IsA()->InheritsFrom("THnSparseF"))
      className = "THnSparseF";
    else if (obj->IsA()->InheritsFrom("THnSparseL"))
      className = "THnSparseL";
    else if (obj->IsA()->InheritsFrom("THnSparseI"))
      className = "THnSparseI";
    else if (obj->IsA()->InheritsFrom("THnSparseS"))
      className = "THnSparseS";
    else if (obj->IsA()->InheritsFrom("THnSparseC"))
      className = "THnSparseC";
    NLogger::Debug("Adding branch %s with class '%s' ...", objNames[i].c_str(), className.c_str());
    AddBranch(objNames[i], nullptr, className);
    GetBranch(objNames[i])->SetAddress(objects[objNames[i]]);
  }
  // loop over all selected bins via ROOT iterarot for THnSparse
  THnSparse * cSparse = fBinning->GetContent();
  // cSparse->GetAxis(3)->SetRange(11, 11);
  NLogger::Info("Number of entries in content: %lld", cSparse->GetNbins());
  Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogger::Debug("Bin %lld: %s", linBin, binCoordsStr.c_str());
    // std::vector<std::vector<int>> coordsRange = fBinning->GetCoordsRange(cCoordsVector);
    //
    // Setting original sparse object
    for (size_t i = 0; i < objNames.size(); i++) {
      GetBranch(objNames[i])->SetAddress(objects[objNames[i]]);
    }

    std::vector<std::vector<int>> axisRanges = fBinning->GetAxisRanges(cCoordsVector);
    fPointData->SetPointContent(cCoordsVector);
    // fPointData->Print("A");

    SaveEntry(this, axisRanges, true);
  }

  f->Close();

  return true;
}

bool NHnSparseTree::ImportBinning(std::map<std::string, std::vector<std::vector<int>>> binning, THnSparse * hnstIn)
{
  ///
  /// Import binning
  ///
  if (fBinning == nullptr) {
    NLogger::Error("Binning is not initialized !!!");
    return false;
  }
  if (binning.empty()) {
    NLogger::Error("Binning is empty !!!");
    return false;
  }
  if (hnstIn == nullptr) {
    hnstIn = this;
  }

  fBinning->GetMap()->Reset();
  // add binningIn for reset of axes
  for (int iAxis = 0; iAxis < hnstIn->GetNdimensions(); iAxis++) {
    // print axis
    TAxis *     axis     = hnstIn->GetAxis(iAxis);
    std::string axisName = axis->GetName();
    // NLogger::Debug("XXXXXXX Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", iAxis, axisName.c_str(),
    //                axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    if (!binning[axisName].empty()) {
      // if (axisName == "axis1-pt" || axisName == "axis2-ce" || axisName == "axis5-eta") {
      fBinning->AddBinningViaBinWidths(iAxis + 1, binning[axisName]);
      fBinning->GetDefinition()[axisName] = binning[axisName];
    }
    else {
      fBinning->AddBinning(iAxis + 1, {axis->GetNbins(), 1, 1}, 1);
      fBinning->GetDefinition()[axisName] = {{axis->GetNbins()}};
    }
  }
  fBinning->FillAll();
  InitAxes(fBinning->GetListOfAxes());

  return true;
}

bool NHnSparseTree::Process(Ndmspc::ProcessFuncPtr func, const std::vector<int> & mins, const std::vector<int> & maxs,
                            int nThreads, NHnSparseTree * hnstIn, NHnSparseTree * hnstOut)
{
  ///
  /// Process function
  ///
  TH1::AddDirectory(kFALSE);

  bool batch = gROOT->IsBatch();

  if (func == nullptr) {
    NLogger::Error("Process function is nullptr !!!");
    return false;
  }

  if (nThreads < 0) {
    NLogger::Error("Number of threads must be greater then or equal 0 !!!");
    return false;
  }

  int maxBins = fBinning->GetContent()->GetNbins();
  if (nThreads > maxBins) {
    NLogger::Warning("Number of threads (%d) is greater than number of entries (%lld). Using %lld threads.", nThreads,
                     maxBins, maxBins);
    nThreads = maxBins;
  }

  // Print number of threads
  NLogger::Info("Processing with %d threads ...", nThreads);

  if (nThreads == 1) {
    Ndmspc::NDimensionalExecutor executor(mins, maxs);

    // fBranchesMap.clear(); // Clear branches map to avoid conflicts
    AddBranch("output", nullptr, "TList");

    TList * outputGlobal = new TList();
    auto    task         = [this, func, hnstIn, outputGlobal](const std::vector<int> & coords) {
      NHnSparseTree * hnstCurrent = hnstIn;
      if (hnstCurrent == nullptr) {
        hnstCurrent = this;
      }

      if (hnstCurrent->GetTree()->GetEntries() > 0) {
        Ndmspc::NLogger::Debug("Processing entry %d of %d", coords[0], hnstCurrent->GetTree()->GetEntries());
        hnstCurrent->GetEntry(coords[0] - 1);
        // hnstCurrent->GetBranch("output")->Print();
        // return; // Skip processing if entry is already processed
        // hnstCurrent->GetPoint()->SetHnSparseTree(hnstCurrent);
      }
      else {
        Ndmspc::NLogger::Trace("Processing entry %d", coords[0]);
      }
      TList * output = new TList();
      // output->SetOwner(true);                   // Set owner to delete objects in the list
      func(hnstCurrent->GetPoint(), output, outputGlobal, 0); // Call the lambda function

      if (output && output->GetEntries() > 0) {
        // output->Print();
        GetBranch("output")->SetAddress(output); // Set the output list as branch address
        SetPoint(hnstCurrent->GetPoint());       // Set the point in the current HnSparseTree

        SaveEntry();
      }
      else {
        Ndmspc::NLogger::Warning("Function output is nullptr !!!");
      }

      delete output; // Clean up the output list
    };
    executor.Execute(task);
    outputGlobal->Draw("APE");
  }
  else {

    // Optional: Suppress ROOT's info messages for cleaner output during thread execution
    gROOT->SetBatch(kTRUE);

    if (hnstIn == nullptr) {
      hnstIn = this;
    }

    std::string enabledBranches;
    // Generate string of enabled branches from fBranchesMap
    for (auto & kv : hnstIn->GetBranchesMap()) {
      if (kv.second.GetBranchStatus() == 1) {
        NLogger::Info("Enabled branch: %s", kv.first.c_str());
        enabledBranches += kv.first + ",";
      }
    }
    if (!enabledBranches.empty()) {
      enabledBranches.pop_back(); // Remove the last comma
    }

    Ndmspc::NDimensionalExecutor executor_par(mins, maxs);
    // 1. Create the vector of NThreadData objects
    size_t num_threads_to_use = nThreads;

    // set num_threads_to_use to the number of available threads
    if (num_threads_to_use < 1) num_threads_to_use = std::thread::hardware_concurrency();
    Ndmspc::NLogger::Info("Using %zu threads\n", num_threads_to_use);
    std::vector<Ndmspc::NHnSparseTreeThreadData> thread_data_vector(num_threads_to_use);
    for (size_t i = 0; i < thread_data_vector.size(); ++i) {
      thread_data_vector[i].SetAssignedIndex(i);
      thread_data_vector[i].SetFileName(hnstIn->GetFileName());
      thread_data_vector[i].SetEnabledBranches(enabledBranches); // Set enabled branches
      thread_data_vector[i].SetProcessFunc(func);                // Set the processing function
      thread_data_vector[i].GetHnstOutput(hnstOut);              // Set the input HnSparseTree
      thread_data_vector[i].SetOutputGlobal(new TList());        // Set global output list
      // thread_data_vector[i].GetHnSparseTree(); // Initialize the NHnSparseTree
    }

    auto start_par = std::chrono::high_resolution_clock::now();

    // 2. Define the lambda (type in signature updated)
    auto parallel_task = [](const std::vector<int> & coords, Ndmspc::NHnSparseTreeThreadData & thread_obj) {
      thread_obj.Print();
      thread_obj.Process(coords);
    };

    // 3. Call ExecuteParallel (template argument updated)
    // Use renamed class NThreadData
    executor_par.ExecuteParallel<Ndmspc::NHnSparseTreeThreadData>(parallel_task, thread_data_vector);
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    Ndmspc::NLogger::Info("Parallel execution completed and it took %.3f s", par_duration.count() / 1000);

    // Print number of results
    Ndmspc::NLogger::Info("Closing %zu results ...", thread_data_vector.size());
    for (auto & data : thread_data_vector) {
      data.GetHnstOutput()->Close(true); // Close the output file and write the tree
      Ndmspc::NLogger::Trace("Thread %zu: File '%s' closed and tree written", data.GetAssignedIndex(),
                             data.GetHnstOutput()->GetFileName().c_str());
    }

    Ndmspc::NLogger::Info("Merging %zu results in to '%s' ...", thread_data_vector.size(),
                          hnstOut->GetFileName().c_str());
    TList *                           mergeList  = new TList();
    Ndmspc::NHnSparseTreeThreadData * outputData = new Ndmspc::NHnSparseTreeThreadData();
    outputData->SetHnstOutput(hnstOut);

    for (auto & data : thread_data_vector) {
      // Ndmspc::NLogger::Trace("Adding thread data %zu to merge list file=%s ...", data.GetAssignedIndex(),
      //                        data.GetHnstOutput()->GetFileName().c_str());
      mergeList->Add(&data);
    }
    Long64_t nmerged = outputData->Merge(mergeList);
    Ndmspc::NLogger::Info("Merged %lld objects successfully", nmerged);

    // --- Process Results (Inspect the original vector) ---
    // Merging results
    // std::cout << "Results from " << thread_data_vector.size() << " provided TObjects:" << std::endl;
    // long long total_items = 0;
    // long long total_sum   = 0;
    // for (const auto & data : thread_data_vector) {
    //   data.Print();
    //   total_items += data.GetItemCount();
    //   total_sum += data.GetCoordSum();
    // }
    // std::cout << "Total items processed: " << total_items << std::endl;
    // std::cout << "Total coordinate sum: " << total_sum << std::endl;
    gROOT->SetBatch(batch);
  }

  return true;
}

Long64_t NHnSparseTree::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///

  if (!list) return 0; // No list to merge from
  NLogger::Trace("Merging %zu objects via NHnSparseTree::Merge ...", list->GetEntries());
  Long64_t        nmerged = list->GetEntries();
  TIter           next(list);
  NHnSparseTree * obj     = nullptr;
  THnSparse *     cSparse = fBinning->GetContent();
  if (cSparse == nullptr) {
    NLogger::Error("NHnSparseTree::Merge: Content is nullptr !!! Cannot merge and exiting ...");
    return 0;
  }
  NLogger::Trace("Number of entries in content: %lld", cSparse->GetNbins());
  Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogger::Trace("Bin %lld(idx=%lld): %s", linBin, idx, binCoordsStr.c_str());

    // if (linBin >= cSparse->GetNbins()) break;
    // continue;

    while ((obj = dynamic_cast<NHnSparseTree *>(next()))) {
      if (obj == this || !obj) continue;
      // NLogger::Info("Searching object '%s' with %lld entries ...", obj->GetFileName().c_str(),
      //               obj->GetTree()->GetEntries());
      Long64_t bin = obj->GetBinning()->GetContent()->GetBin(cCoords);
      if (bin < obj->GetTree()->GetEntries()) {
        // obj->Print();
        obj->GetEntry(bin);
        // Lopp over all branches in the object
        for (auto & kv : obj->GetBranchesMap()) {
          NLogger::Trace("Merging branch '%s' ...", kv.first.c_str());
          if (kv.second.GetBranchStatus() == 0) {
            NLogger::Trace("Branch '%s' is disabled !!! Skipping ...", kv.first.c_str());
            continue;
          }
          // Get the branch object
          TObject * branchObj = kv.second.GetObject();
          if (!branchObj) {
            NLogger::Warning("NHnSparseTree::Merge: Branch '%s' object is nullptr !!! Skipping ...", kv.first.c_str());
            continue;
          }

          NTreeBranch * b = GetBranch(kv.first);
          if (b == nullptr) {
            AddBranch(kv.first, kv.second.GetObject(), branchObj->IsA()->GetName());
            b = GetBranch(kv.first);
          }
          b->SetAddress(branchObj); // Set the branch address
        }
        SetPoint(obj->GetPoint()); // Set the point in the current HnSparseTree
        SaveEntry();
      }
    }
    next.Reset(); // Reset the iterator to start from the beginning again
  }
  return nmerged;
}
NTreeBranch * NHnSparseTree::GetBranch(const std::string & name)
{
  ///
  /// Return branch by name
  ///
  if (name.empty()) {
    NLogger::Error("Branch name is empty !!!");
    return nullptr;
  }

  // Print all branches
  // for (const auto & kv : fBranchesMap) {
  //   NLogger::Debug("Branch '%s' : %s", kv.first.c_str(), kv.second.GetBranchStatus() ? "enabled" : "disabled");
  // }

  if (fBranchesMap.find(name) == fBranchesMap.end()) {
    // NLogger::Error("NHnSparseTree::GetBranch: Branch '%s' not found !!!", name.c_str());
    return nullptr;
  }

  return &fBranchesMap[name];
}
TH1D * NHnSparseTree::ProjectionFromObject(const std::string & name, int xaxis, Option_t * option)
{
  ///
  /// Returns projection for TH1
  ///
  TObject * obj = GetBranchObject(name);
  if (!obj) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found !!!", name.c_str());
    return nullptr;
  }
  if (!obj->IsA()->InheritsFrom("THnSparse")) {
    NLogger::Error("NHnSparseTree::Projection: Object '%s' is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  THnSparse * hns = (THnSparse *)obj;

  return hns->Projection(xaxis, option);
}
TH2D * NHnSparseTree::ProjectionFromObject(const std::string & name, int yaxis, int xaxis, Option_t * option)
{
  ///
  /// Returns projection for TH2
  ///
  TObject * obj = GetBranchObject(name);
  if (!obj) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found !!!", name.c_str());
    return nullptr;
  }
  if (!obj->IsA()->InheritsFrom("THnSparse")) {
    NLogger::Error("NHnSparseTree::Projection: Object '%s' is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  THnSparse * hns = (THnSparse *)obj;

  return hns->Projection(yaxis, xaxis, option);
}
TH3D * NHnSparseTree::ProjectionFromObject(const std::string & name, int xaxis, int yaxis, int zaxis, Option_t * option)
{
  ///
  /// Returns projection for TH3
  ///
  TObject * obj = GetBranchObject(name);
  if (!obj) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found !!!", name.c_str());
    return nullptr;
  }
  if (!obj->IsA()->InheritsFrom("THnSparse")) {
    NLogger::Error("NHnSparseTree::Projection: Object '%s' is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  THnSparse * hns = (THnSparse *)obj;

  return hns->Projection(xaxis, yaxis, zaxis, option);
}

} // namespace Ndmspc
