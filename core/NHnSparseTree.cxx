#include <map>
#include <vector>
#include <fstream>
#include <TBufferJSON.h>
#include <THnSparse.h>
#include <TSystem.h>
#include <TVirtualPad.h>
#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include <TH1.h>
#include <TROOT.h>
#include "NBinningDef.h"
#include "NHnSparseObject.h"
#include "NHnSparseTreeThreadData.h"
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

  Reset();
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
  // point[GetNdimensions() - 1] = 1; // Set last dimension to entry number
  std::string pointStr = NUtils::GetCoordsString(NUtils::ArrayToVector(point, GetNdimensions()));
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
      // fBinning->SetDefinitigon(binning);
      fBinning->AddBinningDefinition("default", binning, true);
      fBinning->Print();
      NBinningDef * def = fBinning->GetDefinition("default");
      // add binningIn for reset of axes
      for (int iAxis = 0; iAxis < hns->GetNdimensions(); iAxis++) {
        // print axis
        TAxis *     axis     = hns->GetAxis(iAxis);
        std::string axisName = axis->GetName();
        NLogger::Debug("XXXXXXX Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", iAxis, axisName.c_str(),
                       axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
        if (!binning[axisName].empty()) {
          // if (axisName == "axis1-pt" || axisName == "axis2-ce" || axisName == "axis5-eta") {
          fBinning->AddBinningViaBinWidths(iAxis + 1, binning[axisName]);
          def->SetAxisDefinition(axisName, binning[axisName]);
          // def.Print();
          // fBinning->GetDefinition()[axisName] = binning[axisName];
        }
        else {
          fBinning->AddBinning(iAxis + 1, {axis->GetNbins(), 1, 1}, 1);
          // fBinning->GetDefinition()[axisName] = {{axis->GetNbins()}};
          def->SetAxisDefinition(axisName, {{axis->GetNbins()}});
        }
      }
      def->Print();
      std::vector<Long64_t> entries;
      fBinning->FillAll(entries);
      NLogger::Debug("Here is the binning definition: [1]");
      InitAxes(fBinning->GenerateListOfAxes());
      NLogger::Debug("Here is the binning definition: [2]");
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

bool NHnSparseTree::ImportBinning(std::string binningName, std::map<std::string, std::vector<std::vector<int>>> binning,
                                  THnSparse * hnstIn)
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
  fBinning->AddBinningDefinition(binningName, binning, true);
  NBinningDef * def = fBinning->GetDefinition(binningName);
  if (def == nullptr) {
    NLogger::Error("Binning definition is nullptr !!!");
    return false;
  }
  // add binningIn for reset of axes
  for (int iAxis = 0; iAxis < hnstIn->GetNdimensions(); iAxis++) {
    // print axis
    TAxis *     axis     = hnstIn->GetAxis(iAxis);
    std::string axisName = axis->GetName();
    NLogger::Debug("XXXXXXX Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", iAxis, axisName.c_str(),
                   axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    if (!binning[axisName].empty()) {
      // if (axisName == "axis1-pt" || axisName == "axis2-ce" || axisName == "axis5-eta") {
      fBinning->AddBinningViaBinWidths(iAxis + 1, binning[axisName]);
      // fBinning->GetDefinition()[axisName] = binning[axisName];
      def->SetAxisDefinition(axisName, binning[axisName]);
    }
    else {
      fBinning->AddBinning(iAxis + 1, {axis->GetNbins(), 1, 1}, 1);
      // fBinning->GetDefinition()[axisName] = {{axis->GetNbins()}};
      def->SetAxisDefinition(axisName, {{axis->GetNbins()}});
    }
  }
  std::vector<Long64_t> entries;
  fBinning->FillAll(entries);
  InitAxes(fBinning->GenerateListOfAxes());

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

        if (fBinning) SetPoint(hnstCurrent->GetPoint()); // Set the point in the current HnSparseTree

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
THnSparse * NHnSparseTree::GetTHnSparseFromObject(const std::string & name, std::map<int, std::vector<int>> ranges)
{

  ///
  /// Return THnSparse from branch object and custom ranges
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
  NUtils::SetAxisRanges(hns, ranges);
  return hns;
}
TH1D * NHnSparseTree::ProjectionFromObject(const std::string & name, int xaxis, std::map<int, std::vector<int>> ranges,
                                           Option_t * option)
{
  ///
  /// Returns projection for TH1
  ///
  THnSparse * hns = GetTHnSparseFromObject(name, ranges);
  if (!hns) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found or is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  return hns->Projection(xaxis, option);
}
TH2D * NHnSparseTree::ProjectionFromObject(const std::string & name, int yaxis, int xaxis,
                                           std::map<int, std::vector<int>> ranges, Option_t * option)
{
  ///
  /// Returns projection for TH2
  ///
  THnSparse * hns = GetTHnSparseFromObject(name, ranges);
  if (!hns) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found or is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  return hns->Projection(yaxis, xaxis, option);
}
TH3D * NHnSparseTree::ProjectionFromObject(const std::string & name, int xaxis, int yaxis, int zaxis,
                                           std::map<int, std::vector<int>> ranges, Option_t * option)
{
  ///
  /// Returns projection for TH3
  ///
  THnSparse * hns = GetTHnSparseFromObject(name, ranges);
  if (!hns) {
    NLogger::Error("NHnSparseTree::Projection: Branch '%s' not found or is not a THnSparse !!!", name.c_str());
    return nullptr;
  }
  return hns->Projection(xaxis, yaxis, zaxis, option);
}

bool NHnSparseTree::ExportNavigatorObject(NHnSparseObject * obj, std::vector<std::vector<int>> levels, int level,
                                          std::map<int, std::vector<int>> ranges,
                                          std::map<int, std::vector<int>> rangesBase)
{

  ///
  /// Returns the navigator object in json format
  ///

  TH1::AddDirectory(kFALSE);

  // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: levels=%zu level=%d ...", levels.size(), level);
  //
  obj->SetNLevels(levels.size());
  obj->SetLevel(level);

  if (level < levels.size()) {

    // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: levels[%d]=%s...", level,
    //                NUtils::GetCoordsString(levels[level]).c_str());

    // Generate projection histogram

    // loop for every bin in the current level

    std::vector<int> minsBin;
    std::vector<int> maxsBin;
    for (auto & idx : levels[level]) {
      // NLogger::Debug("[B%d] Axis %d: %s", level, idx, GetAxis(idx)->GetName());
      // int minBase = 0, maxBase = 0;
      // NUtils::GetAxisRangeInBase(GetAxis(idx), 1, GetAxis(idx)->GetNbins(), fBinning->GetAxes()[idx], minBase,
      // maxBase); ranges[idx] = {minBase, maxBase};            // Set the ranges for the axis
      minsBin.push_back(1);                        // Get the minimum bin edge);
      maxsBin.push_back(GetAxis(idx)->GetNbins()); // Get the maximum bin edge);
    }

    NDimensionalExecutor executorBin(minsBin, maxsBin);
    auto loop_task_bin = [this, &obj, levels, level, ranges, rangesBase](const std::vector<int> & coords) {
      // NLogger::Debug("[B%d] Processing coordinates: coords=%s levels=%s", level,
      //                                NUtils::GetCoordsString(coords, -1).c_str(),
      //                                NUtils::GetCoordsString(levels[level]).c_str());
      NLogger::Debug("[L%d] Generating %zuD histogram %s with ranges: %s", level, levels[level].size(),
                     NUtils::GetCoordsString(levels[level]).c_str(), ranges.size() == 0 ? "[]" : "");

      std::vector<int> axesIds = levels[level];
      ///////// Make projection histogram /////////
      THnSparse * hns   = nullptr;
      Int_t       nDims = axesIds.size();
      Int_t       dims[nDims];
      for (int i = 0; i < nDims; i++) {
        dims[i] = axesIds[i];
      }

      NUtils::SetAxisRanges(this, ranges); // Set the ranges for the axes
      hns = static_cast<THnSparse *>(ProjectionND(axesIds.size(), dims, "O"));
      if (!hns) {
        NLogger::Error("NHnSparseTree::ExportNavigatorObject: Projection failed for level %d !!!", level);
        return;
      }
      // int nCells = h->GetNcells();
      // h->SetMinimum(0);
      // h->SetStats(kFALSE);

      std::string title = "";
      for (auto & axisId : axesIds) {
        TAxis * a = GetAxis(axisId);
        title += std::string(a->GetName()) + " vs ";
      }
      title = title.substr(0, title.size() - 4); // Remove last " vs "
      if (ranges.size() > 0) title += " for ranges: ";
      for (const auto & [axisId, range] : rangesBase) {
        // NLogger::Debug("XX Axis '%s' range: [%d, %d]", GetAxis(axisId)->GetName(), range[0], range[1]);
        TAxis * a = fBinning->GetAxes()[axisId];
        title += TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(range[0]), a->GetBinUpEdge(range[1]));
      }
      hns->SetTitle(title.c_str());
      // hns->Print();

      // int index = -1;
      // if (coords.size() == 1) index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]));
      // if (coords.size() == 2)
      //   index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]));
      // if (coords.size() == 3)
      //   index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]),
      //                                         h->GetZaxis()->GetBinCenter(coords[2]));

      if (obj->GetHnSparse() == nullptr) {
        NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Setting histogram '%s' ...", hns->GetTitle());
        obj->SetHnSparse(hns);
      }

      int   indexInProj = -1;
      TH1 * hProj       = nullptr;
      if (level < 3) {
        if (nDims == 1) {
          hProj       = Projection(axesIds[0]);
          indexInProj = hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]));
        }
        else if (nDims == 2) {
          hProj = Projection(axesIds[1], axesIds[0]);
          indexInProj =
              hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]), hProj->GetYaxis()->GetBinCenter(coords[1]));
        }
        else if (nDims == 3) {
          hProj = Projection(axesIds[0], axesIds[1], axesIds[2]);
          indexInProj =
              hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]), hProj->GetYaxis()->GetBinCenter(coords[1]),
                                hProj->GetZaxis()->GetBinCenter(coords[2]));
        }
        if (!hProj) {
          NLogger::Error("NHnSparseTree::ExportNavigatorObject: Projection failed for level %d !!!", level);
          return;
        }
        // hProj->SetTitle(title.c_str());
        // NLogger::Debug("[L%d] Projection histogram '%s' for coords=%s index=%d", level, hProj->GetTitle(),
        //                NUtils::GetCoordsString(coords, -1).c_str(), indexInProj);
        //
        // hProj->SetMinimum(0);
        // hProj->SetStats(kFALSE);
        // hProj->Draw();
        // gPad->ModifiedUpdate();
        // gSystem->Sleep(1000);
      }
      //////// End of projection histogram ////////

      std::map<int, std::vector<int>> rangesTmp     = ranges;
      std::map<int, std::vector<int>> rangesBaseTmp = rangesBase;
      for (auto & kv : rangesBaseTmp) {
        std::vector<int> range = rangesTmp[kv.first];
        NLogger::Debug("[L%d]   Axis %d[%s]: rangeBase=%s range=%s", level, kv.first, GetAxis(kv.first)->GetName(),
                       NUtils::GetCoordsString(kv.second).c_str(), NUtils::GetCoordsString(range).c_str());
      }
      int minBase = 0, maxBase = 0;
      int i = 0;
      for (auto & c : coords) {
        // NLogger::Debug("Coordinate: %d v=%d axis=%d", i, coords[i], axes[i]);
        NUtils::GetAxisRangeInBase(GetAxis(axesIds[i]), c, c, fBinning->GetAxes()[axesIds[i]], minBase, maxBase);
        // NLogger::Debug("Axis %d: minBase=%d maxBase=%d", axesIds[i], minBase, maxBase);
        rangesTmp[axesIds[i]]     = {c, c};             // Set the range for the first axis
        rangesBaseTmp[axesIds[i]] = {minBase, maxBase}; // Set the range for the first axis
        i++;
      }

      NHnSparseObject * o      = obj->GetChild(indexInProj);
      int               nCells = hProj->GetNcells();
      if (o == nullptr) {
        NLogger::Debug("[L%d] Creating new child for index %d nCells=%d ...", level, indexInProj, nCells);
        o = new NHnSparseObject(hns->GetListOfAxes());
        if (obj->GetChildren().size() != nCells) obj->SetChildrenSize(nCells);
        obj->SetChild(o, indexInProj); // Set the child at the index
        obj = o;
      }
      else {
        NLogger::Error("[L%d] Using existing child for index %d [NOT OK] ...", level, indexInProj);
      }
      if (level == levels.size() - 1) {
        NLogger::Debug("[L%d] Filling projections from all branches %s for ranges:", level,
                       NUtils::GetCoordsString(levels[level]).c_str());
        for (auto & kv : rangesBaseTmp) {
          std::vector<int> range = rangesTmp[kv.first];
          NLogger::Debug("[L%d]   Axis %d[%s]: rangeBase=%s range=%s", level, kv.first, GetAxis(kv.first)->GetName(),
                         NUtils::GetCoordsString(kv.second).c_str(), NUtils::GetCoordsString(range).c_str());
        }
        // Get projectiosn histograms from all branches

        Int_t *  cCoords = new Int_t[GetNdimensions()];
        Long64_t linBin  = 0;

        NUtils::SetAxisRanges(this, rangesTmp); // Set the ranges for the axes
        std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{CreateIter(true /*use axis range*/)};
        std::vector<int>                                linBins;

        // loop over all bins in the sparse

        while ((linBin = iter->Next()) >= 0) {
          linBins.push_back(linBin);
        }
        if (linBins.empty()) {
          // continue;
          return; // No bins found, nothing to process
        }
        // bool skipBin = false; // Skip bin if no bins are found
        NLogger::Debug("Branch object Point coordinates: %s", NUtils::GetCoordsString(linBins, -1).c_str());
        for (auto & [key, val] : fBranchesMap) {
          if (val.GetBranchStatus() == 0) {
            NLogger::Debug("[L%d] Branch '%s' is disabled, skipping ...", level, key.c_str());
            continue; // Skip disabled branches
          }
          NLogger::Debug("[L%d] Processing branch '%s' with %zu objects to loop ...", level, key.c_str(),
                         linBins.size());

          if (obj->GetParent()->GetObjectContentMap()[key].size() != nCells)
            obj->GetParent()->ResizeObjectContentMap(key, nCells);

          //           if (skipBin) continue; // Skip processing if the previous bin was skipped
          // TODO: Make it configurable
          int projectionAxis = 0;

          TH1 * hProjTmp = nullptr;
          // loop over all linBins
          for (int lb : linBins) {
            GetEntry(lb);
            if (hProjTmp == nullptr) {
              hProjTmp = ProjectionFromObject(key, projectionAxis, rangesBaseTmp);
              // NLogger::Debug("AAAA %.0f", hProjTmp ? hProjTmp->GetEntries() : 0);
            }
            else {
              TH1 * temp = ProjectionFromObject(key, projectionAxis, rangesBaseTmp);
              // NLogger::Debug("BBBB %.0f", temp ? temp->GetEntries() : 0);
              if (temp) {
                hProjTmp->Add(temp);
                delete temp; // Delete the temporary histogram to avoid memory leaks
              }
            }
          }
          if (!hProjTmp) {
            // skipBin = true; // Skip bin if no histogram is created
            continue;
          }
          // generate histogram title from axis base ranges
          std::string title = "Projection of " + key + " ";
          for (const auto & [axis, range] : rangesBaseTmp) {
            TAxis * a = fBinning->GetAxes()[axis];

            title +=
                TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(range[0]), a->GetBinUpEdge(range[1]));
          }
          hProjTmp->SetTitle(title.c_str());
          NLogger::Debug("[L%d] Projection histogram '%s' for branch '%s' storing with indexInProj=%d, entries=%.0f",
                         level, hProjTmp->GetTitle(), key.c_str(), indexInProj, hProjTmp->GetEntries());
          obj->GetParent()->SetObject(key, hProjTmp, indexInProj);
          // hProjTmp->Draw();
          // gPad->Update();
          // gPad->Modified();
          // gSystem->ProcessEvents();
          // TODO: We may to set entries from projection histogram to the bin content of mapping file
        }
      }

      // execute next level
      ExportNavigatorObject(obj, levels, level + 1, rangesTmp, rangesBaseTmp);
      obj = obj->GetParent();
    };
    executorBin.Execute(loop_task_bin);
  }
  else {
    // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Reached the end of levels, level=%d", level);
    return true;
  }

  // std::vector<int>     mins = {1};
  // std::vector<int>     maxs = {(int)levels.size()};
  // NDimensionalExecutor executor(mins, maxs);
  // auto                 loop_task = [this, &obj, levels, level, ranges](const std::vector<int> & coords) {
  //   NLogger::Debug("Processing coordinates: %s level=%d", NUtils::GetCoordsString(coords, -1).c_str(), level);
  // };
  // executor.Execute(loop_task);

  // if (level < levels.size()) {

  // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: levels[%d]=%s...", level,
  //                NUtils::GetCoordsString(levels[level]).c_str());

  // // Get current projection histogram
  // TObject * object = nullptr;
  // TH1 *     h      = nullptr;
  //
  // std::vector<int> axesIds = levels[level];
  //
  // if (obj.empty()) {
  //   if (axesIds.size() == 1)
  //     h = Projection(axesIds[0]);
  //   else if (axesIds.size() == 2)
  //     h = Projection(axesIds[1], axesIds[0]);
  //   else if (axesIds.size() == 3)
  //     h = Projection(axesIds[0], axesIds[1], axesIds[2]);
  //   else {
  //     NLogger::Error("NHnSparseTree::ExportNavigatorObject: Invalid number of levels (%zu) !!!", axesIds.size());
  //     return false;
  //   }
  //   if (!h) {
  //     NLogger::Error("NHnSparseTree::ExportNavigatorObject: Projection failed for level %d !!!", level);
  //     return false;
  //   }
  //   int nCells = h->GetNcells();
  //   h->SetMinimum(0);
  //   h->SetStats(kFALSE);
  //
  //   object = h; // Get the object from the branch
  //   obj    = json::parse(TBufferJSON::ConvertToJSON(h).Data());
  // }
  //
  // if (level < levels.size() - 1) {
  //   // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Hist only level=%d !!!", level);
  //   std::vector<int> mins, maxs;
  //   std::string      n;
  //   for (auto & idx : axesIds) {
  //     NLogger::Debug("Axis %d: %s", idx, GetAxis(idx)->GetName());
  //     int minBase = 0, maxBase = 0;
  //     NUtils::GetAxisRangeInBase(GetAxis(idx), 1, GetAxis(idx)->GetNbins(), fBinning->GetAxes()[idx], minBase,
  //                                maxBase);
  //     n += GetAxis(idx)->GetName();
  //     mins.push_back(1);                        // Get the minimum bin edge);
  //     maxs.push_back(GetAxis(idx)->GetNbins()); // Get the maximum bin edge);
  //   }
  //   NLogger::Debug("NHnSparseTree::ExportNavigatorObject: %s mins=%s maxs=%s", n.c_str(),
  //                  NUtils::GetCoordsString(mins).c_str(), NUtils::GetCoordsString(maxs).c_str());
  //   NDimensionalExecutor executor(mins, maxs);
  //   auto                 loop_task = [this, &obj, levels, level, ranges](const std::vector<int> & coords) {
  //     NLogger::Debug("Processing coordinates: %s", NUtils::GetCoordsString(coords, -1).c_str());
  //     return;
  //     std::vector<int> axesIds = levels[level + 1];
  //     for (auto & idx : axesIds) {
  //       NLogger::Debug("Axis %d: %s", idx, GetAxis(idx)->GetName());
  //     }
  //
  //     int minBase = 0, maxBase = 0;
  //     //
  //     int i = 0;
  //
  //     std::map<int, std::vector<int>> rangesTmp = ranges;
  //     for (auto & c : coords) {
  //       // NLogger::Debug("Coordinate: %d v=%d axis=%d", i, coords[i], axes[i]);
  //       NUtils::GetAxisRangeInBase(GetAxis(axesIds[i]), c, c, fBinning->GetAxes()[axesIds[i]], minBase, maxBase);
  //       NLogger::Debug("Axis %d: minBase=%d maxBase=%d", axesIds[i], minBase, maxBase);
  //       rangesTmp[axesIds[i]] = {minBase, maxBase}; // Set the range for the first axis
  //       i++;
  //     }
  //     //
  //     //   TH1 * h = nullptr;
  //     //   if (axesIds.size() == 1)
  //     //     h = Projection(axesIds[0]);
  //     //   else if (axesIds.size() == 2)
  //     //     h = Projection(axesIds[1], axesIds[0]);
  //     //   else if (axesIds.size() == 3)
  //     //     h = Projection(axesIds[0], axesIds[1], axesIds[2]);
  //     //   else {
  //     //     NLogger::Error("NHnSparseTree::ExportNavigatorObject: Invalid number of levels (%zu) !!!",
  //     //     axesIds.size()); return false;
  //     //   }
  //     //   if (!h) {
  //     //     NLogger::Error("NHnSparseTree::ExportNavigatorObject: Projection failed for level %d !!!", level);
  //     //     return false;
  //     //   }
  //     //   int nCells = h->GetNcells();
  //     //   h->SetMinimum(0);
  //     //   h->SetStats(kFALSE);
  //     //
  //     //   std::string title;
  //     //   for (const auto & [axisId, range] : rangesTmp) {
  //     //     NLogger::Debug("XX Axis '%s' range: [%d, %d]", GetAxis(axisId)->GetName(), range[0], range[1]);
  //     //     TAxis * a = fBinning->GetAxes()[axisId];
  //     //     title += TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(rangesTmp[axisId][0]),
  //     //                                              a->GetBinUpEdge(rangesTmp[axisId][1]));
  //     //   }
  //     //   h->SetTitle(title.c_str());
  //     //
  //     //   int index = -1;
  //     //   if (coords.size() == 1) index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]));
  //     //   if (coords.size() == 2)
  //     //     index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]));
  //     //   if (coords.size() == 3)
  //     //     index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]),
  //     //                                           h->GetZaxis()->GetBinCenter(coords[2]));
  //     //
  //     //   // NLogger::Debug("QQQQQQQQQQQQ Index in histogram: %d", index);
  //     // json objLevel = json::parse(TBufferJSON::ConvertToJSON(h).Data());
  //     //
  //     // obj["children"][index] = objLevel;
  //     // ExportNavigatorObject(obj["children"][index], levels, level + 1, rangesTmp);
  //     ExportNavigatorObject(obj, levels, level + 1, rangesTmp);
  //     //
  //     //   return true;
  //   };
  //   executor.Execute(loop_task);
  //   //
  //   return true;
  // }
  //
  // ExportNavigatorObject(obj, levels, level + 1, ranges);

  // std::cout << obj.dump() << std::endl;

  // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Exporting navigator object at level %d ...", level);
  // TObject * object = nullptr;
  //
  // std::vector<int>       axes = levels[level];
  // TH1 *                  h    = nullptr;
  // std::vector<TObject *> hists;
  // int                    nCells;
  // if (axes.size() == 1) {
  //   h      = Projection(axes[0]);
  //   nCells = h->GetNcells();
  // }
  // else if (axes.size() == 2) {
  //   h      = Projection(axes[1], axes[0]);
  //   nCells = h->GetNcells();
  // }
  // else if (levels[level].size() == 3) {
  //   h      = Projection(axes[0], axes[1], axes[2]);
  //   nCells = h->GetNcells();
  // }
  // else {
  //   NLogger::Error("NHnSparseTree::ExportNavigatorObject: Invalid number of levels (%zu) !!!",
  //   levels[level].size()); return false;
  // }
  //
  // if (!h) {
  //   NLogger::Error("NHnSparseTree::ExportNavigatorObject: Projection failed for level %d !!!", level);
  //   return false;
  // }
  //
  // h->SetMinimum(0);
  // h->SetStats(kFALSE);
  // object = h; // Get the object from the branch
  // if (level < levels.size() - 1) {
  //   NLogger::Debug("NHnSparseTree::ExportNavigatorObject: AAAA level=%d !!!", level);
  //   std::vector<int> mins, maxs;
  //   // loop ove levels[level]
  //   for (auto & idx : axes) {
  //     NLogger::Debug("Axis %d: %s", idx, GetAxis(idx)->GetName());
  //     int minBase = 0, maxBase = 0;
  //     NUtils::GetAxisRangeInBase(GetAxis(idx), 1, GetAxis(idx)->GetNbins(), fBinning->GetAxes()[idx], minBase,
  //     maxBase); mins.push_back(1);                        // Get the minimum bin edge);
  //     maxs.push_back(GetAxis(idx)->GetNbins()); // Get the maximum bin edge);
  //   }
  //   NDimensionalExecutor executor(mins, maxs);
  //   auto                 loop_task = [this, &obj, levels, level, ranges, axes, h](const std::vector<int> & coords)
  //   {
  //     NLogger::Debug("Processing coordinates: %s", NUtils::GetCoordsString(coords, -1).c_str());
  //
  //     int minBase = 0, maxBase = 0;
  //     //
  //     int i = 0;
  //
  //     std::map<int, std::vector<int>> rangesTmp = ranges;
  //     for (auto & c : coords) {
  //       // NLogger::Debug("Coordinate: %d v=%d axis=%d", i, coords[i], axes[i]);
  //       NUtils::GetAxisRangeInBase(GetAxis(axes[i]), c, c, fBinning->GetAxes()[axes[i]], minBase, maxBase);
  //       NLogger::Debug("Axis %d: minBase=%d maxBase=%d", axes[i], minBase, maxBase);
  //       rangesTmp[axes[i]] = {minBase, maxBase}; // Set the range for the first axis
  //       i++;
  //     }
  //
  //     int index = -1;
  //     if (coords.size() == 1) index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]));
  //     if (coords.size() == 2)
  //       index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]));
  //     if (coords.size() == 3)
  //       index = h->FindFixBin(h->GetXaxis()->GetBinCenter(coords[0]), h->GetYaxis()->GetBinCenter(coords[1]),
  //                                             h->GetZaxis()->GetBinCenter(coords[2]));
  //
  //     NLogger::Debug("QQQQQQQQQQQQ Index in histogram: %d", index);
  //     json objLevel          = json::parse(TBufferJSON::ConvertToJSON(h).Data());
  //     obj["children"][index] = objLevel;
  //     std::cout << obj.dump(-1) << std::endl;
  //
  //     ExportNavigatorObject(obj["children"][index], levels, level + 1, rangesTmp);
  //   };
  //   executor.Execute(loop_task);
  //
  //   return true;
  // }
  // else {
  //
  //   // Print ranges
  //   for (const auto & [axis, range] : ranges) {
  //     NLogger::Debug("Axis '%s' range: [%d, %d]", GetAxis(axis)->GetName(), range[0], range[1]);
  //   }
  //   NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Exporting navigator object at level %d [END] ...",
  //   level);
  //   // return true;
  //   std::map<std::string, std::vector<TObject *>> branchProjections;
  //   for (auto & [key, val] : fBranchesMap) {
  //     std::vector<TObject *> & v = branchProjections[key];
  //     v.resize(nCells, nullptr); // Initialize the vector to hold the histograms
  //   }
  //
  //   // TODO: Fix this to use the correct binning from base axis
  //   std::vector<int> sizes;
  //   sizes.push_back(GetAxis(axes[0])->GetNbins());
  //   axes.size() > 1 ? sizes.push_back(GetAxis(axes[1])->GetNbins()) : sizes.push_back(1);
  //   (axes.size() > 2) ? sizes.push_back(GetAxis(axes[2])->GetNbins()) : sizes.push_back(1);
  //
  //   bool skipBin = false;
  //
  //   int minBase = 0, maxBase = 0;
  //   for (int k = 1; k <= sizes[2]; ++k) {
  //     if (axes.size() > 2) {
  //       GetAxis(axes[2])->SetRange(k, k);
  //       if (sizes[2] == 1) {
  //         ranges[axes[2]] = {
  //             1, fBinning->GetAxes()[axes[2]]->GetNbins()}; // Set the range for the third axis if no axes are
  //             specified
  //       }
  //       else {
  //         // TODO: Fix this to use the correct binning from base axis
  //         NUtils::GetAxisRangeInBase(GetAxis(axes[2]), k, k, fBinning->GetAxes()[axes[2]], minBase, maxBase);
  //         ranges[axes[2]] = {minBase, maxBase}; // Set the range for the first axis
  //         // ranges[axes[2]] = {k, k}; // Set the range for the third axis]
  //       }
  //     }
  //     for (int j = 1; j <= sizes[1]; ++j) {
  //       if (axes.size() > 1) {
  //         GetAxis(axes[1])->SetRange(j, j);
  //         if (sizes[1] == 1)
  //           ranges[axes[1]] = {
  //               1,
  //               fBinning->GetAxes()[axes[1]]->GetNbins()}; // Set the range for the second axis if no axes are
  //               specified
  //         else {
  //           // TODO: Fix this to use the correct binning from base axis
  //           NUtils::GetAxisRangeInBase(GetAxis(axes[1]), j, j, fBinning->GetAxes()[axes[1]], minBase, maxBase);
  //           ranges[axes[1]] = {minBase, maxBase}; // Set the range for the first axis
  //           // ranges[axes[1]] = {j, j}; // Set the range for the second axis
  //         }
  //       }
  //       for (int i = 1; i <= sizes[0]; ++i) {
  //         if (axes.size() > 0) {
  //           GetAxis(axes[0])->SetRange(i, i);
  //           if (sizes[0] == 1) {
  //             ranges[axes[0]] = {1, fBinning->GetAxes()[axes[0]]
  //                                       ->GetNbins()}; // Set the range for the first axis if no axes are specified
  //           }
  //           else {
  //             // int b           = fBinning->GetAxisRanges({axes[0], i, i});
  //             // TODO: Fix this to use the correct binning from base axis
  //
  //             NUtils::GetAxisRangeInBase(GetAxis(axes[0]), i, i, fBinning->GetAxes()[axes[0]], minBase, maxBase);
  //             ranges[axes[0]] = {minBase, maxBase}; // Set the range for the first axis
  //             // ranges[axes[0]] = {i, i}; // Set the range for the first axis
  //           }
  //         }
  //         // Create sparse iterator
  //
  //         Int_t *                                         cCoords = new Int_t[GetNdimensions()];
  //         Long64_t                                        linBin  = 0;
  //         std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{CreateIter(true /*use axis range*/)};
  //         std::vector<int>                                linBins;
  //
  //         // loop over all bins in the sparse
  //
  //         while ((linBin = iter->Next()) >= 0) {
  //           linBins.push_back(linBin);
  //         }
  //         if (linBins.empty()) {
  //           NLogger::Warning("No bins found for the current axis ranges. Skipping bin (%d, %d, %d)", i, j, k);
  //           skipBin = true; // Skip bin if no bins are found
  //           continue;
  //         }
  //         NLogger::Debug("Point coordinates: %s", NUtils::GetCoordsString(linBins, -1).c_str());
  //         for (auto & [key, val] : fBranchesMap) {
  //           if (skipBin) continue; // Skip processing if the previous bin was skipped
  //           NLogger::Debug("Processing bin (%d, %d, %d) for %s", i, j, k, key.c_str());
  //
  //           // Print ranges
  //           for (const auto & [axis, range] : ranges) {
  //             NLogger::Debug("XX Axis '%s' range: [%d, %d]", GetAxis(axis)->GetName(), range[0], range[1]);
  //           }
  //
  //           int   projectionAxis = 0;
  //           TH1 * im             = nullptr;
  //
  //           // loop over all linBins
  //           for (int lb : linBins) {
  //             GetEntry(lb);
  //             if (im == nullptr) {
  //               im = ProjectionFromObject(key, projectionAxis);
  //               // print im entries
  //               // NLogger::Debug("%.0f", im ? im->GetEntries() : 0);
  //             }
  //             else {
  //               TH1 * temp = ProjectionFromObject(key, projectionAxis);
  //               // NLogger::Debug("%.0f", temp ? temp->GetEntries() : 0);
  //               if (temp) {
  //                 im->Add(temp);
  //                 delete temp; // Delete the temporary histogram to avoid memory leaks
  //               }
  //             }
  //           }
  //           if (!im) {
  //             skipBin = true; // Skip bin if no histogram is created
  //             continue;
  //           }
  //           // generate histogram title from axis base ranges
  //           std::string title = "Projection of " + key + " ";
  //           for (const auto & [axis, range] : ranges) {
  //             NLogger::Debug("XX Axis '%s' range: [%d, %d]", GetAxis(axis)->GetName(), range[0], range[1]);
  //             TAxis * a = fBinning->GetAxes()[axis];
  //             title += TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(ranges[axis][0]),
  //                                      a->GetBinUpEdge(ranges[axis][1]));
  //           }
  //           im->SetTitle(title.c_str());
  //           NLogger::Debug("entries=%.0f", im->GetEntries());
  //           im->Draw();
  //           gPad->Update();
  //           gPad->Modified();
  //           gSystem->ProcessEvents();
  //           int index = -1;
  //           if (axes.size() == 1) index = h->FindFixBin(h->GetXaxis()->GetBinCenter(i));
  //           if (axes.size() == 2) index = h->FindFixBin(h->GetXaxis()->GetBinCenter(i),
  //           h->GetYaxis()->GetBinCenter(j)); if (axes.size() == 3)
  //             index = h->FindFixBin(h->GetXaxis()->GetBinCenter(i), h->GetYaxis()->GetBinCenter(j),
  //                                   h->GetZaxis()->GetBinCenter(k));
  //
  //           if (key == fBranchesMap.begin()->first) {
  //             // if (im->GetEntries() < 0) {
  //             //   NLogger::Trace("    Skipping bin (%d, %d, %d) with index %d due to low entries: %f", i, j, k,
  //             index,
  //             //                  im->GetEntries());
  //             //   if (axes.size() == 0) h->SetBinContent(i, 0); // Set bin content to 0 if histogram is empty
  //             //   if (axes.size() == 1) h->SetBinContent(i, j, 0);
  //             //   if (axes.size() == 2) h->SetBinContent(i, j, k, 0);
  //             //   skipBin = true;
  //             //   continue; // Skip bins with low entries
  //             // }
  //             // else {
  //             if (axes.size() == 1) {
  //               h->SetBinContent(i, im->GetEntries());
  //             }
  //             else if (axes.size() == 2) {
  //               h->SetBinContent(i, j, im->GetEntries());
  //             }
  //             else if (axes.size() == 3) {
  //               h->SetBinContent(i, j, k, im->GetEntries());
  //             }
  //             // }
  //           }
  //           branchProjections[key][index] = im; // Store the histogram in the vector at the correct index
  //           NLogger::Trace("Branch '%s' at bin (%d, %d, %d): entries=%.0f was stored", key.c_str(), i, j, k,
  //                          im->GetEntries());
  //         }
  //         skipBin = false; // Reset skipBin for the next iteration
  //       }
  //     }
  //   }
  //
  // json objLevel = json::parse(TBufferJSON::ConvertToJSON(object).Data());
  // if (obj.empty()) {
  //   obj = objLevel;
  //   for (auto & [key, val] : fBranchesMap) {
  //     json hJsonObj        = json::parse(TBufferJSON::ToJSON(&branchProjections[key]).Data());
  //     obj["children"][key] = hJsonObj;
  //   }
  //   // delete all histograms in branchProjections
  //   for (auto & [key, val] : branchProjections) {
  //     for (auto & hist : val) {
  //       if (hist) {
  //         delete hist; // Delete the histogram to avoid memory leaks
  //       }
  //     }
  //   }
  //   branchProjections.clear();
  // }
  // else {
  //   obj["children"] = objLevel;
  // }
  // ExportNavigatorObject(obj, levels, level + 1, ranges);
  // }

  //   return true;
  // }
  // else {
  //   // NLogger::Debug("NHnSparseTree::ExportNavigatorObject: Exporting navigator object at level %d [END]...",
  //   level);
  //   // std::cout << obj.dump(-1) << std::endl;
  //   // std::ofstream out("/tmp/navigator_object.json");
  //   // out << obj.dump(-1) << std::endl;
  // }
  return true;
}

} // namespace Ndmspc
