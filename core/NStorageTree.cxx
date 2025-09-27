#include <NBinningPoint.h>
#include <TSystem.h>
#include <TROOT.h>
#include <TFolder.h>
#include "TDirectory.h"
#include "TError.h"
#include "NBinning.h"
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "RtypesCore.h"
#include "NStorageTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NStorageTree);
/// \endcond

namespace Ndmspc {
NStorageTree::NStorageTree(NBinning * binning) : TObject(), fBinning(binning)
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

void NStorageTree::Clear(Option_t * option)
{
  ///
  /// Clear object
  ///
  TString opt(option);
  opt.ToUpper();

  if (opt.Contains("F")) {
    if (fFile) {
      fFile->Close();
      delete fFile;
      fFile = nullptr;
    }
    fTree = nullptr;
  }
  fBranchesMap.clear();
  fFileName = "hnst.root";
  fPrefix.clear();
  fPostfix.clear();
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
    // NLogger::Warning("NStorageTree::InitTree: File was not defined, using RAM memory as storage ...",
    //                  fFileName.c_str());
    fFileName = "";
    gROOT->cd();
  }

  NLogger::Debug("Initializing tree '%s' using filename '%s' ...", treename.c_str(), fFileName.c_str());

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

Long64_t NStorageTree::GetEntry(Long64_t entry, NBinningPoint * point, bool checkBinningDef)
{
  ///
  /// Get entry
  ///

  NLogger::Trace("Getting entry=%lld nbranches=%d ...", entry, fBranchesMap.size());
  // fTree->Print();
  // Print warning if entry is out of bounds and return 0
  if (entry < 0 || entry >= fTree->GetEntries()) {
    NLogger::Warning("Entry %lld is out of bounds [0, %lld]. Reading 0 bytes and objects remain from last valid entry.",
                     entry, fTree->GetEntries() - 1);
    fBinning->GetPoint()->Reset();
    return 0;
  }

  if (point) {
    if (!point->SetPointContentFromLinearIndex(entry, checkBinningDef)) {
      NLogger::Error("NStorageTree::GetEntry: Cannot set point content from linear index %lld !!!", entry);
      return 0;
    }
  }
  else {
    NLogger::Warning("NStorageTree::GetEntry: Binning point is nullptr, cannot set point content !!!");
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

  // Print byte sum
  NLogger::Debug("[entry=%lld] Bytes read : %.3f MB file='%s'", entry, (double)bytessum / (1024 * 1024),
                 fFileName.empty() ? "memory" : fFileName.c_str());
  return bytessum;
}

Int_t NStorageTree::Fill(NBinningPoint * point, NStorageTree * hnstIn, bool ignoreFilledCheck,
                         std::vector<std::vector<int>> ranges, bool useProjection)
{
  ///
  /// Fill Entry
  ///
  NLogger::Trace("NStorageTree::Fill: Filling entry in NStorageTree ...");

  if (fTree == nullptr) {
    NLogger::Error("NStorageTree::Fill: Tree is not initialized !!! Run 'NStorageTree::InitTree(...)' first !!!");
    return -2;
  }

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

  Long64_t bin = point->Fill(ignoreFilledCheck);
  if (bin < 0 && ignoreFilledCheck == false) {
    fBinning->GetDefinition()->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());

    NLogger::Warning("Point was already processed, skipping ...");
    // NLogger::Warning("NStorageTree::Fill: Point was already filled !!!");
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
  NLogger::Trace("NStorageTree::Fill: Filled entry %lld -> tree entries=%lld %d", bin, fTree->GetEntries(), nBytes);

  if (fFile && nBytes <= 0) {
    NLogger::Error("NStorageTree::Fill: Failed to fill tree '%s' in file '%s' !!!", fTree->GetName(), fFile->GetName());
    return -3;
  }

  Long64_t entry = fTree->GetEntries() - 1;
  // point->SetEntryNumber(fTree->GetEntries() - 1);
  fBinning->GetDefinition()->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());
  NLogger::Debug("[entry=%lld] Bytes written : %.3f MB file='%s'", entry, (Double_t)nBytes / (1024 * 1024),
                 fTree->GetCurrentFile() ? fTree->GetCurrentFile()->GetName() : "memory");

  return nBytes;
}

bool NStorageTree::Close(bool write, std::map<std::string, TList *> outputs)
{
  ///
  /// Close
  ///

  if (!fTree) {
    NLogger::Error("NStorageTree::Close: Tree is not initialized !!!");
    return false;
  }

  TList * userInfo = fTree->GetUserInfo();
  if (fBinning) {
    userInfo->Add(fBinning->Clone());
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
      fFile->cd();

      fFile->mkdir("outputs");
      fFile->cd("outputs");
      for (auto & kv : outputs) {
        if (kv.second && !kv.second->IsEmpty()) {

          kv.second->Write(kv.first.c_str(), TObject::kSingleKey);
          NLogger::Debug("Output list '%s' with %d objects was written", kv.first.c_str(), kv.second->GetEntries());
        }
      }
      fFile->cd();
      fTree->Write("", TObject::kOverwrite);
      fFile->Close();
      NLogger::Info("HnSparseTree was written to file '%s' ...", fFileName.c_str());
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
    NLogger::Warning("Branch '%s' already exists, returning existing branch ...", name.c_str());
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

void NStorageTree::SetEnabledBranches(std::vector<std::string> branches, int status)
{
  ///
  /// Enable/disable branches
  ///

  // loop over all branches
  for (auto & kv : fBranchesMap) {
    if (branches.empty() || std::find(branches.begin(), branches.end(), kv.first) != branches.end()) {
      NLogger::Trace("%s branch '%s' ...", status == 1 ? "Enabling" : "Disabling", kv.first.c_str());
      kv.second.SetBranchStatus(status == 1 ? 1 : 0);
    }
    else {
      NLogger::Trace("%s branch '%s' ...", status == 1 ? "Disabling" : "Enabling", kv.first.c_str());
      kv.second.SetBranchStatus(status == 1 ? 0 : 1);
    }
  }
}

Long64_t NStorageTree::Merge(TCollection * list)
{
  ///
  /// Merge trees
  ///

  if (!list) {
    NLogger::Error("NStorageTree::Merge: List is nullptr !!!");
    return -1;
  }
  if (list->IsEmpty()) {
    NLogger::Error("NStorageTree::Merge: List is empty !!!");
    return -1;
  }
  // return 0;
  if (fTree == nullptr) {
    NLogger::Error("NStorageTree::Merge: Tree is not initialized !!! Run 'NStorageTree::InitTree(...)' first !!!");
    return -1;
  }

  Long64_t nmerged = list->GetEntries();
  NLogger::Info("Merging %d trees into tree '%s' ...", list->GetEntries(), fTree->GetName());
  NLogger::Debug("NStorageTree::Merge: Merging %zu objects via NStorageTree::Merge ...", list->GetEntries());

  // Loop over all binning definitions in the list
  // for (auto & kv : fBinning->GetDefinitions()) {
  //   NLogger::Info("Merging binning definition '%s' ...", kv.first.c_str());
  //   kv.second->GetIds().clear();
  // }

  // clear all binning definition contents and ids before merging
  // for (auto & kv : fBinning->GetDefinitions()) {
  //   auto binningDefIn = obj->GetBinning()->GetDefinition(kv.first);
  //   if (binningDefIn) {
  //     // binningDefIn->GetContent()->Reset();
  //     // binningDefIn->GetIds().clear();
  //   }
  // }
  NBinningPoint * point = fBinning->GetPoint();

  TIter          next(list);
  NStorageTree * obj     = nullptr;
  THnSparse *    cSparse = fBinning->GetContent();
  if (cSparse == nullptr) {
    NLogger::Error("NHnSparseTree::Merge: Content is nullptr !!! Cannot merge and exiting ...");
    return 0;
  }
  NLogger::Debug("NStorageTree::Merge: Number of entries in content: %lld", cSparse->GetNbins());
  Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogger::Debug("NStorageTree::Merge: Bin %lld(idx=%lld): %s", linBin, idx, binCoordsStr.c_str());

    // if (linBin >= cSparse->GetNbins()) break;
    // continue;

    while ((obj = dynamic_cast<NStorageTree *>(next()))) {
      if (obj == this || !obj) continue;
      NLogger::Debug("NHnSparseTree::Merge: Searching object '%s' with %lld entries ...", obj->GetFileName().c_str(),
                     obj->GetTree()->GetEntries());
      // continue;
      Long64_t bin = obj->GetBinning()->GetContent()->GetBin(cCoords);
      if (bin < obj->GetTree()->GetEntries()) {
        // obj->Print();
        obj->GetEntry(bin, point);
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
        // SetPoint(obj->GetPoint()); // Set the point in the current HnSparseTree
        // SaveEntry();
        // fBinning->SetPoint(obj->GetBinning()->GetPoint());
        // NLogger::Info("Filling point %s ...", binCoordsStr.c_str());
        Fill(point, obj, true, {}, false);
        // fill all binning definition ids when present
        for (auto & kv : fBinning->GetDefinitions()) {
          auto binningDefIn = obj->GetBinning()->GetDefinition(kv.first);
          if (binningDefIn) {
            NLogger::Debug("Adding bin %lld to binning definition '%s' entry %lld...", idx, kv.first.c_str(),
                           point->GetEntryNumber());
            binningDefIn->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());
            // NLogger::Debug("  IDs before: %s", NUtils::GetCoordsString(kv.second->GetIds(), -1).c_str());
            // binningDefIn->GetContent()->Print();
            // binningDefIn->GetIds().push_back(idx);
          }
        }
      }
    }
    next.Reset(); // Reset the iterator to start from the beginning again
  }
  return nmerged;
}

} // namespace Ndmspc
