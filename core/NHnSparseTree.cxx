#include <map>
#include <THnSparse.h>
#include <TSystem.h>
#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
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

  InitBinnings();
}

NHnSparseTree::NHnSparseTree(const std::string & filename, const std::string & treename) : THnSparse()
{
  ///
  /// Constructor
  ///
  InitTree(filename, treename);
  fPrefix  = gSystem->GetDirName(filename.c_str());
  fPostfix = gSystem->BaseName(filename.c_str());
}

NHnSparseTree::~NHnSparseTree()
{
  ///
  /// Destructor
  ///
}
NHnSparseTree * NHnSparseTree::Open(const std::string & filename, const std::string & treename,
                                    const std::string & branches)
{
  ///
  /// Load
  ///
  NLogger::Info("Loading '%s' ...", filename.c_str());

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
  if (!hnst->SetFileTree(file, tree)) return nullptr;
  if (!hnst->InitBinnings()) return nullptr;
  // hnst->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    hnst->EnableBranches(enabledBranches);
  }

  /// TODO:: Set Axes correctlry (was commented) needed ???
  // hnst->SetBranchAddresses();

  // hnst->Print();

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
      NLogger::Info("  name='%s' title='%s' nbins=%d labels=[%s]", a->GetName(), a->GetTitle(), a->GetNbins(),
                    labels.c_str());
    }
    else {
      NLogger::Info("  name='%s' title='%s' nbins=%d min=%.3f max=%.3f", a->GetName(), a->GetTitle(), a->GetNbins(),
                    a->GetXmin(), a->GetXmax());
    }
  }
  NLogger::Info("TTree:");
  NLogger::Info("  filename='%s'", fFileName.c_str());
  NLogger::Info("  tree name='%s'", fTree ? fTree->GetName() : "n/a");
  NLogger::Info("  tree entries=%lld", fTree ? fTree->GetEntries() : -1);
  NLogger::Info("  prefix='%s'", fPrefix.c_str());
  NLogger::Info("  postfix='%s'", fPostfix.c_str());
  if (!fBranchesMap.empty()) {
    NLogger::Info("  branches:");
    for (auto & kv : fBranchesMap) {
      kv.second.Print();
    }
  }

  std::string pointStr;
  if (fPoint == nullptr) {
    pointStr = "n/a";
  }
  else {
    // Print current point
    for (int i = 0; i < GetNdimensions(); i++) {
      pointStr += std::to_string(fPoint[i]) + " ";
    }
  }
  NLogger::Info("Current point: %s", pointStr.c_str());

  if (opt.Contains("P")) {
    NLogger::Info("Printing content ...");
    //   std::vector<int> minBounds;
    //   std::vector<int> maxBounds;
    //   std::vector<int> ids;
    //   std::string      header = "| ";
    //   for (int i = 0; i < GetNdimensions(); i++) {
    //     minBounds.push_back(1);
    //     maxBounds.push_back(GetAxis(i)->GetNbins());
    //     ids.push_back(i);
    //     header += GetAxis(i)->GetName();
    //     header += " | ";
    //   }
    //   header += "content |";
    //   bool allBins   = opt.Contains("A");
    //   bool urlike    = opt.Contains("U");
    //   bool forceBins = opt.Contains("B");

    //   if (!urlike) NLogger::Info("%s", header.c_str());
    //   Ndmspc::HnSparseTreeUtils::IterateNDimensionalSpace(
    //       minBounds, maxBounds, [logger, this, &allBins, &urlike, &forceBins](const std::vector<int> & point) {
    //         // build string from point coordinates
    //         Int_t p[GetNdimensions()];
    //         // loop over point and set p
    //         for (int i = 0; i < GetNdimensions(); i++) {
    //           p[i] = point[i];
    //         }

    //         double content = GetBinContent(p);
    //         if (urlike) {
    //           if (allBins || GetBinContent(p) > 0) {

    //             std::string pointStr = fPrefix;
    //             std::string binStr   = "{";
    //             if (!fPrefix.empty()) pointStr += "/";
    //             for (int i = 0; i < GetNdimensions(); i++) {
    //               TAxis * a = GetAxis(i);
    //               if (a->IsAlphanumeric() && !forceBins) {
    //                 pointStr += a->GetBinLabel(point[i]);
    //               }
    //               else {
    //                 pointStr += std::to_string(point[i]);
    //               }
    //               pointStr += "/";
    //               binStr += std::to_string(point[i]) + ",";
    //             }
    //             // check if pointStr has space and remove it
    //             if (pointStr.back() == '/') {
    //               pointStr.pop_back();
    //             }
    //             if (binStr.back() == ',') {
    //               binStr.pop_back();
    //             }
    //             binStr += "}";
    //             pointStr += "/" + fPostfix;
    //             NLogger::Info("[%1s] %s\t%s", content > 0 ? "*" : " ", binStr.c_str(), pointStr.c_str());
    //           }
    //         }
    //         else {
    //           if (allBins || GetBinContent(p) > 0) {
    //             // Print current point
    //             std::string pointStr = "| ";
    //             for (int i = 0; i < GetNdimensions(); i++) {
    //               TAxis * a = GetAxis(i);
    //               if (a->IsAlphanumeric()) {
    //                 pointStr += a->GetBinLabel(point[i]);
    //                 pointStr += " | ";
    //               }
    //               else {
    //                 pointStr += std::to_string(point[i]);
    //               }
    //             }
    //             // check if pointStr has space and remove it
    //             if (pointStr.back() == ' ') {
    //               pointStr.pop_back();
    //             }
    //             NLogger::Info("%s %.0f |", pointStr.c_str(), content);
    //           }
    //         }
    //       });
  }
}

bool NHnSparseTree::SetFileTree(TFile * file, TTree * tree)
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
  if (fPrefix.empty()) fPrefix = gSystem->GetDirName(file->GetName());
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
    fFileName = filename;
  }

  NLogger::Info("Initializing tree '%s' using filename '%s' ...", treename.c_str(), filename.c_str());

  // Open file
  fFile = NUtils::OpenFile(fFileName.c_str(), "RECREATE");
  if (!fFile) {
    NLogger::Error("Cannot open file '%s'", fFileName.c_str());
    return false;
  }

  fTree = new TTree(treename.c_str(), "hnst tree");
  if (!fTree) {
    NLogger::Error("Cannot create tree '%s' using file '%s' !!!", treename.c_str(), filename.c_str());
    return false;
  }
  if (fPrefix.empty()) fPrefix = gSystem->GetDirName(filename.c_str());
  if (fPostfix.empty()) fPostfix = gSystem->BaseName(filename.c_str());

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

  SetBinContent(fPoint, 1);

  fFile->cd();
  return fTree->Fill();
}

Long64_t NHnSparseTree::GetEntry(Long64_t entry)
{
  ///
  /// Get entry
  ///

  NLogger::Debug("Getting entry=%lld nbranches=%d ...", entry, fBranchesMap.size());
  // fTree->Print();

  Long64_t bytessum = 0;

  for (auto & kv : fBranchesMap) {
    NLogger::Debug("Getting content from '%s' branch ...", kv.first.c_str());
    bytessum += kv.second.GetEntry(fTree, entry);
  }
  if (fPoint == nullptr) {
    fPoint = new Int_t[GetNdimensions()];
  }
  // GetBinContent(entry, fPoint);
  // for (int i = 0; i < GetNdimensions(); i++) {
  //   NLogger::Info("Point[%d]=%d", i, fPoint[i]);
  // }

  // Print byte sum
  NLogger::Debug("Bytes read : %.3f MB", (double)bytessum / (1024 * 1024));
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

void NHnSparseTree::SetBranchAddresses()
{
  ///
  /// Set branch addresses
  ///

  if (!fTree) {
    NLogger::Error("Tree is nullptr !!!");
    return;
  }

  // NLogger::Info("Setting branch addresses ...");

  // fTree->SetBranchStatus("*", 0);
  for (auto & kv : fBranchesMap) {

    kv.second.SetBranchAddress(fTree);
  }
}

void NHnSparseTree::EnableBranches(std::vector<std::string> branches)
{
  ///
  /// Enable branches
  ///

  if (branches.empty()) {
    NLogger::Debug("Enabling all branches ...");
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
      NLogger::Debug("Enabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(1);
      // fTree->SetBranchStatus(kv.first.c_str(), 1);
    }
    else {
      NLogger::Debug("Disabling branch %s ...", kv.first.c_str());
      kv.second.SetBranchStatus(0);
      // fTree->SetBranchStatus(kv.first.c_str(), 0);
    }
  }
}

bool NHnSparseTree::InitBinnings()
{
  ///
  /// Init binnings
  ///

  int                  dim = GetNdimensions();
  std::vector<TAxis *> axes;
  for (int i = 0; i < dim; i++) {
    axes.push_back(GetAxis(i));
  }
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
