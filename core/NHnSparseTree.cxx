#include <map>
#include <THnSparse.h>
#include <TSystem.h>
#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include "NDimensionalExecutor.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreeInfo.h"

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
}
NHnSparseTree * NHnSparseTree::Open(const std::string & filename, const std::string & branches,
                                    const std::string & treename)
{
  ///
  /// Load
  ///

  NLogger::Info("Opening '%s' ...", filename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogger::Error("Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogger::Error("Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  NHnSparseTreeInfo * hnstInfo = (NHnSparseTreeInfo *)tree->GetUserInfo()->FindObject("hnstInfo");
  if (!hnstInfo) {
    NLogger::Error("Cannot get HnSparseTree from tree '%s'", treename.c_str());
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
    NLogger::Info("Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnst->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnst->fBranchesMap) {
      NLogger::Info("Enabled branches: %s", kv.first.c_str());
    }
  }
  // Sett all branches to be read
  hnst->SetBranchAddresses();
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

  if (fPoint) {
    std::string pointStr = NUtils::GetCoordsString(NUtils::ArrayToVector(fPoint, GetNdimensions()));
    NLogger::Info("Current point: %s", pointStr.c_str());
  }

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
std::string NHnSparseTree::GetPointStr(std::vector<int> coords) const
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
    }
    pointStr += " | ";
  }
  // check if pointStr has space and remove it
  if (pointStr.back() == ' ') {
    pointStr.pop_back();
  }

  return pointStr;
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

  NLogger::Info("Initializing tree '%s' using filename '%s' ...", treename.c_str(), fFileName.c_str());

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
    NLogger::Error("newAxes is nullptr !!!");
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
      axes.push_back((TAxis *)a->Clone());
    }
    // if (fBinning) {
    //   delete fBinning;
    // }
    NLogger::Trace("Creating new binning form new axes [%d]...", axes.size());
    fBinning = new NBinning(axes);
    // fBinning->Print();
  }

  Init("hnTree", "HnSparseTree", newAxes, kTRUE);

  if (fPoint) {
    delete[] fPoint;
    fPoint = nullptr;
  }
  fPoint = new Int_t[GetNdimensions()];
  // Set fPoint elements to -1
  for (int i = 0; i < GetNdimensions(); i++) {
    fPoint[i] = -1;
  }

  return true;
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

  // Print point coordinates
  std::string pointStr = NUtils::GetCoordsString(NUtils::ArrayToVector(fPoint, GetNdimensions()));
  NLogger::Debug("Filling tree with point: %s", pointStr.c_str());
  SetBinContent(fPoint, 1);

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
  // if (fPoint == nullptr) {
  //   fPoint = new Int_t[GetNdimensions()];
  // }
  // GetBinContent(entry, fPoint);
  // for (int i = 0; i < GetNdimensions(); i++) {
  //   NLogger::Info("Point[%d]=%d", i, fPoint[i]);
  // }

  // Print byte sum
  NLogger::Debug("[entry=%lld] Bytes read : %.3f MB", entry, (double)bytessum / (1024 * 1024));
  return bytessum;
}

bool NHnSparseTree::Close(bool write)
{
  ///
  /// Close
  ///
  NLogger::Info("Closing file %s ...", fFileName.c_str());

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
      NLogger::Info("Output was stored in '%s' ...", fFileName.c_str());
    }
    else {
      fFile->Close();
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

  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {

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
} // namespace Ndmspc
