#include <NBinningPoint.h>
#include <TSystem.h>
#include <TROOT.h>
#include <TFolder.h>
#include "TDirectory.h"
#include "TError.h"
#include "NBinning.h"
#include "NBinningDef.h"
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"

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
  // InitTree("/tmp/ngnt.root", "ngnt");
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
  fFileName = "ngnt.root";
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

  NLogInfo("TTree:");
  NLogInfo("  filename='%s'", fFileName.c_str());
  NLogInfo("  tree name='%s' entries=%lld address=%p", fTree ? fTree->GetName() : "n/a",
           fTree ? fTree->GetEntries() : -1, fTree);
  NLogInfo("  tree entries=%lld", fTree ? fTree->GetEntries() : -1);
  NLogInfo("  prefix='%s'", fPrefix.c_str());
  NLogInfo("  postfix='%s'", fPostfix.c_str());
  std::string branchNames = NUtils::GetCoordsString(GetBrancheNames());
  NLogInfo("  branches: size=%d %s", fBranchesMap.size(), branchNames.c_str());

  if (opt.Contains("A")) {
    for (auto & kv : fBranchesMap) {
      kv.second.Print();
    }
  }
  else {
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
    // NLogWarning("NStorageTree::InitTree: File was not defined, using RAM memory as storage ...",
    //                  fFileName.c_str());
    fFileName = "";
    gROOT->cd();
  }

  NLogTrace("Initializing tree '%s' using filename '%s' ...", treename.c_str(), fFileName.c_str());

  if (!fFileName.empty()) {
    // Open file
    fFile = NUtils::OpenFile(fFileName.c_str(), "RECREATE");
    if (!fFile) {
      NLogError("NStorageTree::InitTree: Cannot open file '%s' !!!", fFileName.c_str());
      return false;
    }
    if (fPrefix.empty()) fPrefix = gSystem->GetDirName(fFileName.c_str());
    if (fPostfix.empty()) fPostfix = gSystem->BaseName(fFileName.c_str());
  }

  NLogTrace("NStorageTree::InitTree: Creating tree '%s' ...", treename.c_str());
  fTree = new TTree(treename.c_str(), "ngnt tree");
  if (!fTree) {
    NLogError("Cannot create tree '%s' using file '%s' !!!", treename.c_str(), fFileName.c_str());
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
    NLogError("NStorageTree::SetFileTree: tree is nullptr !!!");
    return false;
  }
  fFile = file;
  if (fFile) {
    NLogTrace("NStorageTree::SetFileTree: Setting file tree from file '%s' ...", fFile->GetName());
    // if (fPrefix.empty() || force) fPrefix = gSystem->GetDirName(fFile->GetName());
    // if (fPostfix.empty()) fPostfix = gSystem->BaseName(fFile->GetName());
    fPrefix   = gSystem->GetDirName(fFile->GetName());
    fPostfix  = gSystem->BaseName(fFile->GetName());
    fFileName = fFile->GetName();
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

  NLogTrace("Getting entry=%lld nbranches=%d ...", entry, fBranchesMap.size());
  // fTree->Print();
  // Print warning if entry is out of bounds and return 0
  if (entry < 0 || entry >= fTree->GetEntries()) {
    NLogWarning("Entry %lld is out of bounds [0, %lld]. Reading 0 bytes and objects remain from last valid entry.",
                entry, fTree->GetEntries() - 1);
    fBinning->GetPoint()->Reset();
    return 0;
  }

  if (point) {
    if (!point->SetPointContentFromLinearIndex(entry, checkBinningDef)) {
      NLogError("NStorageTree::GetEntry: Cannot set point content from linear index %lld !!!", entry);
      return 0;
    }
  }
  else {
    NLogWarning("NStorageTree::GetEntry: Binning point is nullptr, cannot set point content !!!");
    return 0;
  }
  Long64_t bytessum = 0;

  for (auto & kv : fBranchesMap) {
    NLogTrace("NStorageTree::GetEntry: Getting content from '%s' branch ...", kv.first.c_str());
    if (kv.second.GetBranch() == nullptr) {
      NLogError("NStorageTree::GetEntry: Branch '%s' is not initialized !!!", kv.first.c_str());
      continue;
    }
    // check if branch is enabled
    if (kv.second.GetBranchStatus() == 0) {
      NLogTrace("NStorageTree::GetEntry: Branch '%s' is disabled !!! Skipping ...", kv.first.c_str());
      continue;
    }
    bytessum += kv.second.GetEntry(fTree, entry);
  }

  // Print byte sum
  NLogDebug("NStorageTree::GetEntry: [entry=%lld] Bytes read : %.3f MB file='%s'", entry,
            (double)bytessum / (1024 * 1024), fFileName.empty() ? "memory" : fFileName.c_str());
  return bytessum;
}

Int_t NStorageTree::Fill(NBinningPoint * point, NStorageTree * hnstIn, bool ignoreFilledCheck,
                         std::vector<std::vector<int>> ranges, bool useProjection)
{
  ///
  /// Fill Entry
  ///
  NLogTrace("NStorageTree::Fill: Filling entry in NStorageTree ...");

  if (fTree == nullptr) {
    NLogError("NStorageTree::Fill: Tree is not initialized !!! Run 'NStorageTree::InitTree(...)' first !!!");
    return -2;
  }

  for (auto & kv : fBranchesMap) {
    NLogTrace("NStorageTree::Fill: Saving content from %s ...", kv.first.c_str());
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

    NLogWarning("Point was already processed, skipping ...");
    // NLogWarning("NStorageTree::Fill: Point was already filled !!!");
    return -2;
  }

  // NLogInfo("NStorageTree::Fill: bin=%lld -> tree entries=%lld", bin, fTree->GetEntries());
  // if (bin > fTree->GetEntries()) {
  //   NLogError("NStorageTree::Fill: Filled bin %lld is greater than tree entries %lld !!!", bin,
  //                  fTree->GetEntries());
  //   return -2;
  // }

  if (fTree == nullptr) {
    NLogError("NStorageTree::Fill: Tree is not initialized !!!");
    return -2;
  }
  // Filling entry to tree
  Int_t nBytes = fTree->Fill();
  NLogTrace("NStorageTree::Fill: Filled entry %lld -> tree entries=%lld %d", bin, fTree->GetEntries(), nBytes);

  if (fFile && nBytes <= 0) {
    NLogError("NStorageTree::Fill: Failed to fill tree '%s' in file '%s' !!!", fTree->GetName(), fFile->GetName());
    return -3;
  }

  Long64_t entry = fTree->GetEntries() - 1;
  fBinning->GetDefinition()->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());
  point->SetEntryNumber(entry);
  NLogDebug("NStorageTree::Fill: [entry=%lld] Bytes written : %.3f MB file='%s'", entry,
            (Double_t)nBytes / (1024 * 1024), fTree->GetCurrentFile() ? fTree->GetCurrentFile()->GetName() : "memory");

  return nBytes;
}

bool NStorageTree::Close(bool write, std::map<std::string, TList *> outputs)
{
  ///
  /// Close
  ///

  if (!fTree) {
    NLogError("NStorageTree::Close: Tree is not initialized !!!");
    return false;
  }

  if (write) {
    TList * userInfo = fTree->GetUserInfo();
    if (fBinning) {
      userInfo->Add(fBinning->Clone());
    }
    else {
      NLogError("NStorageTree::Close: Binning is not present, cannot store binning in user info !!! "
                "Skipping to store tree content also ...");
      if (fFile) fFile->Close();
      return false;
    }
    userInfo->Add(Clone());

    if (fFile) {
      fFile->cd();

      fFile->mkdir("outputs");
      fFile->cd("outputs");
      for (auto & kv : outputs) {
        if (kv.second && !kv.second->IsEmpty()) {

          kv.second->Write(kv.first.c_str(), TObject::kSingleKey);
          NLogDebug("NStorageTree::Close: Output list '%s' with %d objects was written", kv.first.c_str(),
                    kv.second->GetEntries());
        }
      }
      fFile->cd();
      fTree->Write("", TObject::kOverwrite);
      fFile->Close();
      NLogDebug("NStorageTree::Close: NGnTree was written to file '%s' ...", fFile->GetName());
    }
  }
  else {
    if (fFile) {
      fFile->Close();
      NLogDebug("File '%s' was closed", fFile->GetName());
    }
  }
  fFile = nullptr;
  fTree = nullptr;
  return true;
}

std::vector<std::string> NStorageTree::GetBrancheNames(bool onlyEnabled) const
{
  ///
  /// Get branch names
  ///
  std::vector<std::string> keys;
  for (auto & kv : fBranchesMap) {
    // TODO:: Checking if enabled
    if (kv.second.GetBranchStatus() == 0 && onlyEnabled) continue;
    keys.push_back(kv.first);
  }
  return keys;
}
bool NStorageTree::AddBranch(const std::string & name, void * address, const std::string & className)
{

  if (fBranchesMap.find(name) != fBranchesMap.end()) {
    NLogWarning("Branch '%s' already exists, returning existing branch ...", name.c_str());
    return fBranchesMap[name].GetBranch();
  }

  if (fTree == nullptr) {
    NLogError("Tree is not initialized !!! Run 'NGnTree::InitTree(...)' first !!!");
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
    NLogError("NStorageTree::GetBranch: Branch name is empty !!!");
    return nullptr;
  }

  if (fBranchesMap.find(name) == fBranchesMap.end()) {
    // NLogError("NStorageTree::GetBranch: Branch '%s' not found !!!", name.c_str());
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
    NLogError("NStorageTree::SetBranchAddresses:Tree is nullptr !!!");
    return;
  }

  // NLogTrace("Setting branch addresses ...");

  // Print size of branches map
  NLogTrace("NStorageTree::SetBranchAddresses: Setting branch addresses for %d branches ...", fBranchesMap.size());
  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {
    NLogTrace("NStorageTree::SetBranchAddresses: Setting branch address '%s' ...", kv.first.c_str());
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
      NLogTrace("%s branch '%s' ...", status == 1 ? "Enabling" : "Disabling", kv.first.c_str());
      kv.second.SetBranchStatus(status == 1 ? 1 : 0);
    }
    else {
      NLogTrace("%s branch '%s' ...", status == 1 ? "Disabling" : "Enabling", kv.first.c_str());
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
    NLogError("NStorageTree::Merge: List is nullptr !!!");
    return -1;
  }
  if (list->IsEmpty()) {
    NLogError("NStorageTree::Merge: List is empty !!!");
    return -1;
  }
  // return 0;
  if (fTree == nullptr) {
    NLogError("NStorageTree::Merge: Tree is not initialized !!! Run 'NStorageTree::InitTree(...)' first !!!");
    return -1;
  }

  Long64_t nmerged = list->GetEntries();
  NLogTrace("NStorageTree::Merge: Merging %d trees into tree '%s' ...", list->GetEntries(), fTree->GetName());
  NLogTrace("NStorageTree::Merge: Merging %zu objects via NStorageTree::Merge ...", list->GetEntries());

  // Loop over all binning definitions in the list
  for (auto & kv : fBinning->GetDefinitions()) {
    NLogTrace("NStorageTree::Merge: Clearing ids in binning definition '%s' ...", kv.first.c_str());
    kv.second->GetIds().clear();
  }

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
    NLogError("NHnSparseTree::Merge: Content is nullptr !!! Cannot merge and exiting ...");
    return 0;
  }
  NLogTrace("NStorageTree::Merge: Number of entries in content: %lld", cSparse->GetNbins());
  Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogTrace("NStorageTree::Merge: Bin %lld(idx=%lld): %s", linBin, idx, binCoordsStr.c_str());

    // if (linBin >= cSparse->GetNbins()) break;
    // continue;
    NLogTrace("NStorageTree::Merge: BEGIN linBin=%lld ===================================", linBin);
    bool found = false;

    while ((obj = dynamic_cast<NStorageTree *>(next()))) {
      if (found) break;
      if (obj == this || !obj) continue;
      NLogTrace("NHnSparseTree::Merge: Scanning object '%s' with %lld entries ...", obj->GetFileName().c_str(),
                obj->GetTree()->GetEntries());
      // continue;
      Long64_t binObj = fBinning->GetContent()->GetBin(cCoords, false);
      if (binObj < 0) {
        NLogTrace("NStorageTree::Merge: Bin %lld(idx=%lld): %s -> file='%s' binObj=%lld, skipping ...", linBin, idx,
                  binCoordsStr.c_str(), obj->GetFileName().c_str(), binObj);
        continue;
      }
      NLogTrace("NStorageTree::Merge: binObj=%lld", binObj);
      Long64_t binGlobal = (Long64_t)fBinning->GetContent()->GetBinContent(binObj);
      NLogTrace("NStorageTree::Merge: bcObj=%lld", binGlobal);
      // Long64_t bin = fBinning->GetContent()->GetBin(cCoords, false);
      Long64_t bin = obj->GetBinning()->GetContent()->GetBin(cCoords, false);

      NLogTrace("NStorageTree::Merge: Bin %lld(idx=%lld): %s -> file='%s' is bin=%lld binGlobal=%lld", linBin, idx,
                binCoordsStr.c_str(), obj->GetFileName().c_str(), bin, binGlobal);
      if (bin < 0) {
        NLogTrace("NStorageTree::Merge: Bin %lld(idx=%lld): %s -> file='%s' bin=%lld, skipping ...", linBin, idx,
                  binCoordsStr.c_str(), obj->GetFileName().c_str(), bin);
        continue;
      }

      NLogTrace("NStorageTree::Merge: bin=%lld obj->GetTree()->GetEntries()=%lld", bin, obj->GetTree()->GetEntries());
      if (bin < obj->GetTree()->GetEntries()) {
        // obj->Print();
        obj->GetEntry(bin, point);
        // point->SetEntryNumber(bin); // Set the entry number to the linear bin index
        // point->Print();
        // // FIXME: check why ids are not filled correctly when merging
        found = false;
        NLogTrace("NHnSparseTree::Merge: Looping over all binning definitions to fill ids ...");
        for (auto & name : fBinning->GetDefinitionNames()) {
          // check if id
          auto binningDefIn = obj->GetBinning()->GetDefinition(name);
          if (binningDefIn) {
            NLogTrace("NHnSparseTree::Merge: Checking bin %lld to binning definition '%s' entry %lld  linBin=%d "
                      "binGlobal=%lld ...",
                      idx, name.c_str(), point->GetEntryNumber(), linBin, binGlobal);

            // binningDefIn->Print();
            // check if binGlobal is already in the binningDefIn->GetIds()
            if (std::find(binningDefIn->GetIds().begin(), binningDefIn->GetIds().end(), binGlobal) ==
                binningDefIn->GetIds().end()) {
              // binningDefIn->GetIds().push_back(binGlobal);
              NLogTrace("***** NHnSparseTree::Merge: Found and added %lld to definition '%s' Skipping", binGlobal,
                        name.c_str());
              continue;
            }
            else {
              NLogTrace("***** NHnSparseTree::Merge: ID %lld already exists in definition '%s', Found ...", binGlobal,
                        name.c_str());
              found = true;
            }

            //
            // // binningDefIn->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());
            // // binningDefIn->GetContent()->Print();
            // // NLogDebug("  IDs before: %s", NUtils::GetCoordsString(kv.second->GetIds(), -1).c_str());
            NBinningDef * def = fBinning->GetDefinition(name);
            // binningDefIn->Print();
            // def->Print();
            def->GetIds().push_back(linBin);
            // Long64_t xxx = def->GetContent()->GetBin(point->GetStorageCoords(), false);
            // NLogDebug("  xxx = %lld", xxx);
            // if (xxx >= 0) {
            //   // def->GetContent()->SetBinContent(point->GetStorageCoords(), point->GetEntryNumber());
            //   def->GetIds().push_back(point->GetEntryNumber());
            //   def->Print();
            //   NLogInfo("***** NHnSparseTree::Merge: Found %lld", point->GetEntryNumber());
            //   found = true;
            // }
          }
        }
        if (found == false) {
          NLogWarning("NHnSparseTree::Merge: Bin %lld(idx=%lld): %s -> bin in obj='%s' is bin=%lld binGlobal=%lld "
                      "but no definition found, skipping ...",
                      linBin, idx, binCoordsStr.c_str(), obj->GetFileName().c_str(), bin, binGlobal);
          continue;
        }
        // Lopp over all branches in the object
        for (auto & kv : obj->GetBranchesMap()) {
          NLogTrace("NHnSparseTree::Merge: Merging branch '%s' ...", kv.first.c_str());
          if (kv.second.GetBranchStatus() == 0) {
            NLogTrace("NHnSparseTree::Merge: Branch '%s' is disabled !!! Skipping ...", kv.first.c_str());
            continue;
          }
          // Get the branch object
          TObject * branchObj = kv.second.GetObject();
          if (!branchObj) {
            NLogWarning("NHnSparseTree::Merge: Branch '%s' object is nullptr !!! Skipping ...", kv.first.c_str());
            continue;
          }

          NTreeBranch * b = GetBranch(kv.first);
          if (b == nullptr) {
            AddBranch(kv.first, kv.second.GetObject(), branchObj->IsA()->GetName());
            b = GetBranch(kv.first);
          }
          b->SetAddress(branchObj); // Set the branch address
        }
        // SetPoint(obj->GetPoint()); // Set the point in the current NGnTree
        // SaveEntry();
        // fBinning->SetPoint(obj->GetBinning()->GetPoint());
        Fill(point, obj, true, {}, false);
        NLogTrace("NHnSparseTree::Merge: Filling point %s linBin=%d idx=%d entry_number=%d...", binCoordsStr.c_str(),
                  linBin, idx, point->GetEntryNumber());
      }
    }
    NLogTrace("NHnSparseTree::Merge: END linBin=%lld ===================================", linBin);
    next.Reset(); // Reset the iterator to start from the beginning again
  }

  // Print all definition ids after merging
  for (auto & kv : fBinning->GetDefinitions()) {
    NLogTrace("NHnSparseTree::Merge: IDs in definition '%s': %s", kv.first.c_str(),
              NUtils::GetCoordsString(kv.second->GetIds(), -1).c_str());
  }

  return nmerged;
}

} // namespace Ndmspc
