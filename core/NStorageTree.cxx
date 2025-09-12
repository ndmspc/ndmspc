#include <TSystem.h>
#include <TROOT.h>
#include "NBinning.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NStorageTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NStorageTree);
/// \endcond

namespace Ndmspc {
NStorageTree::NStorageTree() : TObject()
{
  ///
  /// Constructor
  ///
  // InitTree("/tmp/hnst.root", "hnst");
}
NStorageTree::~NStorageTree()
{
  ///
  /// Destructor
  ///
}

void NStorageTree::Print(Option_t * option) const
{
  ///
  /// Print object
  ///

  TString opt(option);
  opt.ToUpper();

  NLogger::Info("TTree:");
  NLogger::Info("  filename='%s'", fFileName.c_str());
  NLogger::Info("  tree name='%s' entries=%lld address=%p", fTree ? fTree->GetName() : "n/a",
                fTree ? fTree->GetEntries() : -1, fTree);
  NLogger::Info("  tree entries=%lld", fTree ? fTree->GetEntries() : -1);
  NLogger::Info("  prefix='%s'", fPrefix.c_str());
  NLogger::Info("  postfix='%s'", fPostfix.c_str());
  NLogger::Info("  branches: [%d]", fBranchesMap.size());

  if (opt.Contains("A")) {
    for (auto & kv : fBranchesMap) {
      kv.second.Print();
    }
  }
}
bool NStorageTree::InitTree(const std::string & filename, const std::string & treename)
{
  ///
  /// Init tree
  ///

  // Set filename
  if (!filename.empty()) {
    fFileName = gSystem->ExpandPathName(filename.c_str());
  }
  else {
    NLogger::Warning("NStorageTree::InitTree: File was not defined, using RAM memory as storage ...",
                     fFileName.c_str());
    fFileName = filename;
    gROOT->cd();
  }

  NLogger::Trace("Initializing tree '%s' using filename '%s' ...", treename.c_str(), fFileName.c_str());

  if (!fFileName.empty()) {
    // Open file
    fFile = NUtils::OpenFile(fFileName.c_str(), "RECREATE");
    if (!fFile) {
      NLogger::Error("NStorageTree::InitTree: Cannot open file '%s' !!!", fFileName.c_str());
      return false;
    }
    if (fPrefix.empty()) fPrefix = gSystem->GetDirName(fFileName.c_str());
    if (fPostfix.empty()) fPostfix = gSystem->BaseName(fFileName.c_str());
  }

  fTree = new TTree(treename.c_str(), "hnst tree");
  if (!fTree) {
    NLogger::Error("Cannot create tree '%s' using file '%s' !!!", treename.c_str(), fFileName.c_str());
    return false;
  }

  return true;
}
bool NStorageTree::SetFileTree(TFile * file, TTree * tree, bool force)
{
  ///
  /// Set File and tree
  ///
  if (!tree) {
    NLogger::Error("NStorageTree::SetFileTree: tree is nullptr !!!");
    return false;
  }
  fFile = file;
  if (fFile) {
    if (fPrefix.empty() || force) fPrefix = gSystem->GetDirName(fFile->GetName());
    if (fPostfix.empty()) fPostfix = gSystem->BaseName(fFile->GetName());
  }
  fTree = tree;
  // print prefix and postfix
  return true;
}

Long64_t NStorageTree::GetEntry(Long64_t entry, NBinningPoint * point)
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
  // TODO: Need to set the point data from the binning content
  // Int_t * point = new Int_t[fBinning->GetContent()->GetNdimensions()];
  // fBinning->GetContent()->GetBinContent(entry, point);
  // fPointData->SetPointContent(NUtils::ArrayToVector(point, fBinning->GetContent()->GetNdimensions()));
  // delete[] point;
  point->SetPointContentFromLinearIndex(entry);

  // Print byte sum
  NLogger::Debug("[entry=%lld] Bytes read : %.3f MB file='%s'", entry, (double)bytessum / (1024 * 1024),
                 fFileName.empty() ? "memory" : fFileName.c_str());
  return bytessum;
}

Int_t NStorageTree::Fill(NBinningPoint * point, NStorageTree * hnstIn, std::vector<std::vector<int>> ranges,
                         bool useProjection)
{
  ///
  /// Fill Entry
  ///
  NLogger::Trace("NStorageTree::Fill: Filling entry in NStorageTree ...");

  for (auto & kv : fBranchesMap) {
    NLogger::Trace("NStorageTree::Fill: Saving content from %s ...", kv.first.c_str());
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

  Long64_t bin = point->Fill();
  if (bin < 0) {
    NLogger::Error("NStorageTree::Fill: Failed to fill point !!!");
    return -2;
  }

  if (fTree == nullptr) {
    NLogger::Error("NStorageTree::Fill: Tree is not initialized !!! Run 'NStorageTree::InitTree(...)' first !!!");
    return -2;
  }

  // NLogger::Info("NStorageTree::Fill: bin=%lld -> tree entries=%lld", bin, fTree->GetEntries());
  // if (bin > fTree->GetEntries()) {
  //   NLogger::Error("NStorageTree::Fill: Filled bin %lld is greater than tree entries %lld !!!", bin,
  //                  fTree->GetEntries());
  //   return -2;
  // }

  // Filling entry to tree
  Int_t nBytes = fTree->Fill();
  NLogger::Debug("[entry=%lld] Bytes written : %.3f MB file='%s'", fTree->GetEntries() - 1,
                 (Double_t)nBytes / (1024 * 1024),
                 fTree->GetCurrentFile() ? fTree->GetCurrentFile()->GetName() : "memory");

  return nBytes;
}

bool NStorageTree::Close(bool write, NBinning * binning)
{
  ///
  /// Close
  ///

  TList * userInfo = fTree->GetUserInfo();
  if (binning) {
    userInfo->Add(binning->Clone());
  }
  else {
    NLogger::Error("NStorageTree::Close: Binning is not present, cannot store binning in user info !!! "
                   "Skipping to store tree content also ...");
    if (fFile) fFile->Close();
    return false;
  }
  userInfo->Add(Clone());
  if (write) {
    if (fFile) {
      fTree->Write("", TObject::kOverwrite);
      fFile->Close();
      NLogger::Info("Output was stored in file '%s'", fFileName.c_str());
    }
  }
  else {
    if (fFile) {
      fFile->Close();
      NLogger::Info("File '%s' was closed", fFileName.c_str());
    }
  }
  fFile = nullptr;
  fTree = nullptr;
  return true;
}

std::vector<std::string> NStorageTree::GetBrancheNames()
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
bool NStorageTree::AddBranch(const std::string & name, void * address, const std::string & className)
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
NTreeBranch * NStorageTree::GetBranch(const std::string & name)
{
  ///
  /// Return branch by name
  ///
  if (name.empty()) {
    NLogger::Error("NStorageTree::GetBranch: Branch name is empty !!!");
    return nullptr;
  }

  // Print all branches
  // for (const auto & kv : fBranchesMap) {
  //   NLogger::Debug("NStorageTree::GetBranch: Branch '%s' : %s", kv.first.c_str(), kv.second.GetBranchStatus() ?
  //   "enabled" : "disabled");
  // }

  if (fBranchesMap.find(name) == fBranchesMap.end()) {
    // NLogger::Error("NStorageTree::GetBranch: Branch '%s' not found !!!", name.c_str());
    return nullptr;
  }

  return &fBranchesMap[name];
}
TObject * NStorageTree::GetBranchObject(const std::string & name)
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

void NStorageTree::SetBranchAddresses()
{
  ///
  /// Set branch addresses
  ///

  if (!fTree) {
    NLogger::Error("NStorageTree::SetBranchAddresses:Tree is nullptr !!!");
    return;
  }

  // NLogger::Trace("Setting branch addresses ...");

  // Print size of branches map
  NLogger::Trace("NStorageTree::SetBranchAddresses: Setting branch addresses for %d branches ...", fBranchesMap.size());
  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {
    NLogger::Trace("NStorageTree::SetBranchAddresses: Setting branch address '%s' ...", kv.first.c_str());
    kv.second.SetBranchAddress(fTree);
  }
}

void NStorageTree::SetEnabledBranches(std::vector<std::string> branches)
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

} // namespace Ndmspc
