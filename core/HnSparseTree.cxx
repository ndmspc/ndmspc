#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include <TH2D.h>
#include <THnSparse.h>
#include <root/TObjArray.h>
#include <string>
#include <vector>
#include "TBranch.h"
#include <TObjArray.h>
#include <TObjString.h>
#include <TObject.h>
#include <TSystem.h>
#include <HnSparseTreeInfo.h>
#include "Logger.h"
#include "RtypesCore.h"
#include "Utils.h"
#include "HnSparseTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::HnSparseTree);
/// \endcond

namespace Ndmspc {

HnSparseTree::HnSparseTree() : THnSparse()
{
  ///
  /// Default constructor
  ///
}

HnSparseTree::HnSparseTree(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin,
                           const Double_t * xmax, Int_t chunksize)
    : THnSparse(name, title, dim, nbins, xmin, xmax, chunksize)
{
  ///
  /// Constructor
  ///
}

HnSparseTree::HnSparseTree(const std::string & filename, const std::string & treename) : THnSparse()
{
  ///
  /// Constructor
  ///
  InitTree(filename, treename);
  fPrefix  = gSystem->GetDirName(filename.c_str());
  fPostfix = gSystem->BaseName(filename.c_str());
}

HnSparseTree::~HnSparseTree()
{
  ///
  /// Destructor
  ///
  if (fPoint) {
    delete[] fPoint;
  }
}
HnSparseTree * HnSparseTree::Load(const std::string & filename, const std::string & treename,
                                  const std::string & branches)
{
  ///
  /// Load
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Loading '%s' ...", filename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    logger->Error("Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    logger->Error("Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  HnSparseTreeInfo * hnstInfo = (HnSparseTreeInfo *)tree->GetUserInfo()->FindObject("hnstInfo");
  if (!hnstInfo) {
    logger->Error("Cannot get HnSparseTree from tree '%s'", treename.c_str());
    return nullptr;
  }

  HnSparseTree * hnst = hnstInfo->GetHnSparseTree();
  if (!hnst->SetFileTree(file, tree)) return nullptr;
  // hnst->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::Utils::Tokenize(branches, ',');
    hnst->EnableBranches(enabledBranches);
  }

  // return nullptr;

  /// TODO:: Set Axes correctlry
  hnst->SetBranchAddresses();

  return hnst;
}
bool HnSparseTree::SetFileTree(TFile * file, TTree * tree)
{
  ///
  /// Set File and tree
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  if (!file || !tree) {
    logger->Error("File or tree is nullptr !!!");
    return false;
  }
  fFile    = file;
  fTree    = tree;
  fPrefix  = gSystem->GetDirName(file->GetName());
  fPostfix = gSystem->BaseName(file->GetName());
  // print prefix and postfix
  return true;
}
void HnSparseTree::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  // THnSparse::Print(option);

  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Printing HnSparseTree with %lld entries and %d dimensions ...", fTree ? fTree->GetEntries() : -1,
               GetNdimensions());

  for (auto & kv : fBranchesMap) {
    kv.second.Print();
  }
  // // Print all axes
  // for (Int_t i = 0; i < GetNdimensions(); i++) {
  //   GetAxis(i)->Print();
  // }
}

bool HnSparseTree::InitTree(const std::string & filename, const std::string & treename)
{
  ///
  /// Init tree
  ///

  // Set filename
  if (!filename.empty()) {
    fFileName = filename;
  }

  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Initializing tree '%s' using filename '%s' ...", treename.c_str(), filename.c_str());

  // Open file
  fFile = Utils::OpenFile(fFileName.c_str(), "RECREATE");
  if (!fFile) {
    logger->Error("Cannot open file '%s'", fFileName.c_str());
    return false;
  }

  fTree = new TTree(treename.c_str(), "Ndh tree");
  if (!fTree) {
    logger->Error("Cannot create tree '%s' using file '%s' !!!", treename.c_str(), filename.c_str());
    return false;
  }
  fPrefix  = gSystem->GetDirName(filename.c_str());
  fPostfix = gSystem->BaseName(filename.c_str());

  return true;
}

bool HnSparseTree::AddBranch(const std::string & name, void * address, const std::string & className)
{

  if (fBranchesMap.find(name) != fBranchesMap.end()) {
    return fBranchesMap[name].GetBranch();
  }

  if (fTree == nullptr) {
    auto logger = Ndmspc::Logger::getInstance("");
    logger->Error("Tree is not initialized !!! Run 'HnSparseTree::InitTree(...)' first !!!");
    return false;
  }

  fBranchesMap[name] = HnSparseTreeBranch(fTree, name, address, className);
  return true;
}

bool HnSparseTree::InitAxes(TObjArray * newAxes, int n)
{
  ///
  /// Init axes
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Trace("Initializing axes ...");

  if (newAxes == nullptr) {
    logger->Error("newAxes is nullptr !!!");
    return false;
  }

  Init("hnTree", "HnSparseTree", newAxes, kTRUE);
  if (fPoint) {
    delete[] fPoint;
    fPoint = nullptr;
  }
  fPoint = new Int_t[GetNdimensions()];
  if (fAxes.GetAxes().size() > 0) {
    fAxes.Print();

    // fAxes.GetAxes().clear();
    return true;
  }
  TAxis * axis = 0;
  for (Int_t i = 0; i < GetNdimensions(); i++) {
    axis      = GetAxis(i);
    fPoint[i] = -1;
    int rebin = 1;
    // TODO: mabe save n as fN so we use it for splitting axes which are filled with single bin or multiple bins
    if (n > 0 && i >= n) {
      rebin = axis->GetNbins();
    }
    Axis a(axis, rebin);
    fAxes.AddAxis(a);
    a.Print();
  }
  // fAxes.Print();

  return true;
}

Int_t HnSparseTree::FillTree()
{
  ///
  /// Fill tree
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  // logger->Info("Filling tree ...");

  if (fTree == nullptr) {
    logger->Error("Tree is not initialized !!! Run 'HnSparseTree::InitTree(...)' first !!!");
    return -1;
  }

  SetBinContent(fPoint, 1);

  fFile->cd();
  return fTree->Fill();
}
// TODO: Rename it to better name : Add indexws by current axis bin range limits
void HnSparseTree::AddProjectionIndexes()
{
  ///
  /// Add projection indexes
  ///

  Int_t *                                         bins   = new Int_t[GetNdimensions()];
  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t v   = GetBinContent(linBin, bins);
    Long64_t idx = GetBin(bins);
    fIndexes.push_back(idx);
  }
  delete[] bins;
}

void HnSparseTree::SetBranchAddresses()
{
  ///
  /// Set branch addresses
  ///

  auto logger = Ndmspc::Logger::getInstance("");
  if (!fTree) {
    logger->Error("Tree is nullptr !!!");
    return;
  }

  // logger->Info("Setting branch addresses ...");

  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {

    kv.second.SetBranchAddress(fTree);
  }
}
void HnSparseTree::EnableBranches(std::vector<std::string> branches)
{
  ///
  /// Enable branches
  ///

  auto logger = Ndmspc::Logger::getInstance("");

  if (branches.empty()) {
    logger->Debug("Enabling all branches ...");
    return;
  }

  // if (!fTree) {
  //   logger->Error("Tree is nullptr !!!");
  //   return;
  // }
  //
  // fTree->SetBranchStatus("*", 0);

  // loop over all branches
  for (auto & kv : fBranchesMap) {
    if (branches.empty() || std::find(branches.begin(), branches.end(), kv.first) != branches.end()) {
      logger->Debug("Enabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(1);
      // fTree->SetBranchStatus(kv.first.c_str(), 1);
    }
    else {
      logger->Debug("Disabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(0);
      // fTree->SetBranchStatus(kv.first.c_str(), 0);
    }
  }
}

Long64_t HnSparseTree::GetEntry(Long64_t entry)
{
  ///
  /// Get entry
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Trace("Getting entry=%lld ...", entry);
  // fTree->Print();

  Long64_t bytessum = 0;

  for (auto & kv : fBranchesMap) {
    // logger->Info("Getting content from %s ...", kv.first.c_str());
    bytessum += kv.second.GetEntry(fTree, entry);
  }

  // fTree->GetEntry(entry);
  return bytessum;
}

void HnSparseTree::SaveEntry(HnSparseTree * hnstIn, Long64_t entry, std::vector<std::vector<int>> ranges,
                             bool useProjection)
{
  ///
  /// Save entry
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Trace("Saving entry=%lld ...", entry);

  for (auto & kv : fBranchesMap) {
    logger->Trace("Saving content from %s ...", kv.first.c_str());
    THnSparse * in = (THnSparse *)hnstIn->GetBranch(kv.first)->GetObject();
    if (ranges.size() > 0) {
      Utils::SetAxisRanges(in, ranges);
    }
    kv.second.Branch(fTree, nullptr);
    kv.second.SaveEntry(hnstIn->GetBranch(kv.first), useProjection);
    // break;
  }

  // Filling entry to tree
  FillTree();
}

int HnSparseTree::FillPoints(std::vector<std::vector<int>> points)
{
  ///
  /// Fill points
  ///

  Logger * logger = Logger::getInstance("");

  for (auto & point : points) {
    int iPoint = 0;
    for (auto & p : point) {
      fPoint[iPoint] = p;
      iPoint++;
    }
    SetBinContent(fPoint, 1);
  }

  return 0;
}

bool HnSparseTree::Close()
{
  ///
  /// Close
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Closing ...");

  if (fFile) {
    fFile->cd();

    TList * userInfo = fTree->GetUserInfo();
    SetNameTitle("hnstMap", "HnSparseTree mapping");
    userInfo->Add((THnSparse *)Clone());
    HnSparseTreeInfo * info = new HnSparseTreeInfo();
    // info->SetHnSparseTree(this);
    info->SetHnSparseTree((HnSparseTree *)Clone());
    userInfo->Add(info);

    fTree->Write("", TObject::kOverwrite);
    fFile->Close();
    logger->Info("Output was stored in '%s' ...", fFileName.c_str());
  }
  return true;
}
void HnSparseTree::Play(int timeout, std::string branches)
{
  ///
  /// Play
  ///

  auto logger = Ndmspc::Logger::getInstance("");

  TH1::AddDirectory(kFALSE);

  logger->Info("Playing timeout=%d branches=%s...", timeout, branches.c_str());

  if (!fTree) {
    logger->Error("Tree is nullptr !!!");
    return;
  }

  if (fTree->GetEntries() == 0) {
    logger->Warning("No entries in tree !!! Printing all points ...");
    GetAxes().Print();
    Int_t *  point    = new Int_t[GetNdimensions()];
    Long64_t bytessum = 0;
    Long64_t nEntries = 0;
    for (Long64_t i = 0; i < GetNbins(); i++) {
      GetBinContent(i, point);

      std::string axes_path = GetAxes().GetPath(point);
      std::string path      = fPrefix + "/" + axes_path + "/" + fPostfix;
      logger->Debug("Path: %s", path.c_str());

      logger->Debug("Branches: %s", branches.c_str());
      HnSparseTree * hnst = HnSparseTree::Load(path, "ndh", branches);
      // return;
      if (!hnst) continue;
      // hnst->GetTree()->SetBranchStatus("*", 0);
      // hnst->GetTree()->SetBranchStatus("unlikepm", 1);
      //
      // hnst->Print();
      // Print number of entries and number of bins filled
      logger->Info("Entries in tree: %lld hist: %lld", hnst->GetTree()->GetEntries(), hnst->GetNbins());
      // loop over all entries
      for (Long64_t j = 0; j < hnst->GetTree()->GetEntries(); j++) {
        // for (Long64_t j = 0; j < hnst->GetNbins(); j++) {
        bytessum += hnst->GetEntry(j);
        for (auto & kv : hnst->GetBranchesMap()) {
          THnSparse * s = (THnSparse *)kv.second.GetObject();
          if (s && s->GetNdimensions() > 0) {
            // Ndmspc::Utils::SetAxisRanges(s, {{1, (int)j, (int)j}});
            TH1D * h = s->Projection(0);
            h->Print();
            delete h;
          }
        }
        nEntries++;
      }
      delete hnst;

      if (timeout > 0) {
        gSystem->Sleep(timeout);
      }
    }
    logger->Info("Bytes read: %.3f MB", (double)bytessum / (1024 * 1024));
    logger->Info("Entries read: %lld", nEntries);
  }
  else {
    logger->Info("Entries in tree: %lld", fTree->GetEntries());
  }
}

std::vector<std::string> HnSparseTree::GetBranchesMapKeys()
{
  std::vector<std::string> keys;
  for (auto & kv : fBranchesMap) {
    keys.push_back(kv.first);
  }
  return keys;
}
} // namespace Ndmspc
