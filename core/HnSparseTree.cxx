#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include <TH2D.h>
#include <THnSparse.h>
#include <TObjArray.h>
#include <string>
#include <vector>
#include "TBranch.h"
#include "TStopwatch.h"
#include <TObjArray.h>
#include <TObjString.h>
#include <TObject.h>
#include <TSystem.h>
#include <TCanvas.h>
#include <TStyle.h>
#include "Logger.h"
#include "RtypesCore.h"
#include "Utils.h"
#include "HnSparseTree.h"
#include "HnSparseTreeInfo.h"
#include "HnSparseTreeUtils.h"

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

  /// TODO:: Set Axes correctlry
  // hnst->SetBranchAddresses();

  hnst->Print();

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
  fFile = file;
  fTree = tree;
  if (fPrefix.empty()) fPrefix = gSystem->GetDirName(file->GetName());
  if (fPostfix.empty()) fPostfix = gSystem->BaseName(file->GetName());
  // print prefix and postfix
  return true;
}
void HnSparseTree::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  // THnSparse::Print(option);
  TString opt(option);

  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("HnSparseTree name='%s' title='%s'", fName.Data(), fTitle.Data());
  logger->Info("THnSparse:");
  logger->Info("  dimensions: %d", fNdimensions);
  logger->Info("  filled bins = %lld", GetNbins());
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
      logger->Info("  name='%s' title='%s' nbins=%d labels=[%s]", a->GetName(), a->GetTitle(), a->GetNbins(),
                   labels.c_str());
    }
    else {
      logger->Info("  name='%s' title='%s' nbins=%d min=%.3f max=%.3f", a->GetName(), a->GetTitle(), a->GetNbins(),
                   a->GetXmin(), a->GetXmax());
    }
  }
  logger->Info("TTree:");
  logger->Info("  filename='%s'", fFileName.c_str());
  logger->Info("  tree name='%s'", fTree ? fTree->GetName() : "n/a");
  logger->Info("  tree entries=%lld", fTree ? fTree->GetEntries() : -1);
  logger->Info("  prefix='%s'", fPrefix.c_str());
  logger->Info("  postfix='%s'", fPostfix.c_str());
  if (!fBranchesMap.empty()) {
    logger->Info("  branches:");
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
  logger->Info("Current point: %s", pointStr.c_str());

  if (opt.Contains("P")) {
    std::vector<int> minBounds;
    std::vector<int> maxBounds;
    std::vector<int> ids;
    std::string      header = "| ";
    for (int i = 0; i < GetNdimensions(); i++) {
      minBounds.push_back(1);
      maxBounds.push_back(GetAxis(i)->GetNbins());
      ids.push_back(i);
      header += GetAxis(i)->GetName();
      header += " | ";
    }
    header += "content |";
    bool allBins   = opt.Contains("A");
    bool urlike    = opt.Contains("U");
    bool forceBins = opt.Contains("B");

    if (!urlike) logger->Info("%s", header.c_str());
    Ndmspc::HnSparseTreeUtils::IterateNDimensionalSpace(
        minBounds, maxBounds, [logger, this, &allBins, &urlike, &forceBins](const std::vector<int> & point) {
          // build string from point coordinates
          Int_t p[GetNdimensions()];
          // loop over point and set p
          for (int i = 0; i < GetNdimensions(); i++) {
            p[i] = point[i];
          }

          double content = GetBinContent(p);
          if (urlike) {
            if (allBins || GetBinContent(p) > 0) {

              std::string pointStr = fPrefix;
              if (!fPrefix.empty()) pointStr += "/";
              for (int i = 0; i < GetNdimensions(); i++) {
                TAxis * a = GetAxis(i);
                if (a->IsAlphanumeric() && !forceBins) {
                  pointStr += a->GetBinLabel(point[i]);
                }
                else {
                  pointStr += std::to_string(point[i]);
                }
                pointStr += "/";
              }
              // check if pointStr has space and remove it
              if (pointStr.back() == '/') {
                pointStr.pop_back();
              }
              pointStr += "/" + fPostfix;
              logger->Info("[%3s] %s", content > 0 ? "YES" : "NO", pointStr.c_str());
            }
          }
          else {
            if (allBins || GetBinContent(p) > 0) {
              // Print current point
              std::string pointStr = "| ";
              for (int i = 0; i < GetNdimensions(); i++) {
                TAxis * a = GetAxis(i);
                if (a->IsAlphanumeric()) {
                  pointStr += a->GetBinLabel(point[i]);
                  pointStr += " | ";
                }
                else {
                  pointStr += std::to_string(point[i]);
                }
              }
              // check if pointStr has space and remove it
              if (pointStr.back() == ' ') {
                pointStr.pop_back();
              }
              logger->Info("%s %.0f |", pointStr.c_str(), content);
            }
          }
        });
  }
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
  if (fPrefix.empty()) fPrefix = gSystem->GetDirName(filename.c_str());
  if (fPostfix.empty()) fPostfix = gSystem->BaseName(filename.c_str());

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
  if (fPoint == nullptr) {
    fPoint = new Int_t[GetNdimensions()];
  }
  GetBinContent(entry, fPoint);
  // for (int i = 0; i < GetNdimensions(); i++) {
  //   logger->Info("Point[%d]=%d", i, fPoint[i]);
  // }

  // fTree->GetEntry(entry);
  return bytessum;
}

void HnSparseTree::SaveEntry(HnSparseTree * hnstIn, std::vector<std::vector<int>> ranges, bool useProjection)
{
  ///
  /// Save entry
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Trace("Saving entry in HnSparseTree ...");

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

bool HnSparseTree::Close(bool write)
{
  ///
  /// Close
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Closing file %s ...", fFileName.c_str());

  if (fFile) {
    if (write) {
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
    else {
      fFile->Close();
    }
  }
  else {
    logger->Error("File is nullptr !!!");
    return false;
  }
  return true;
}
void HnSparseTree::Play(int timeout, std::vector<std::vector<int>> ranges, std::string branches, int branchId)
{
  ///
  /// Play
  ///

  TStopwatch timer;
  timer.Start();
  auto logger = Ndmspc::Logger::getInstance("");

  TH1::AddDirectory(kFALSE);

  logger->Info("Playing timeout=%d branches=%s...", timeout, branches.c_str());

  if (!fTree) {
    logger->Error("Tree is nullptr !!!");
    return;
  }
  std::vector<std::string> enabledBranches = Ndmspc::Utils::Tokenize(branches, ',');
  if (branches.empty()) {
    enabledBranches.clear();
    for (auto & kv : fBranchesMap) {
      logger->Info("Enabling branch %s ...", kv.first.c_str());
      enabledBranches.push_back(kv.first);
    }
    if (branchId < 0 && enabledBranches.size() > 0) {
      branchId = 0;
    }
  }
  if (branchId < 0 || branchId >= enabledBranches.size()) {
    logger->Error("Branch id %d is out of range [0, %d]", branchId, (int)enabledBranches.size());
    return;
  }
  for (auto & kv : fBranchesMap) {
    logger->Info("Available branch %s ...", kv.first.c_str());
  }
  // print info what branches are enabled
  for (auto & kv : enabledBranches) {
    logger->Info("Branch %s is enabled", kv.c_str());
  }

  Long64_t bytessum = 0;
  Long64_t nEntries = 0;
  if (fTree->GetEntries() == 0) {
    logger->Warning("No entries in tree !!! Printing all points ...");
    GetAxes().Print();
    Int_t * point = new Int_t[GetNdimensions()];
    for (Long64_t i = 0; i < GetNbins(); i++) {
      GetBinContent(i, point);

      std::string axes_path = GetAxes().GetPath(point);
      std::string path      = fPrefix + "/" + axes_path + "/" + fPostfix;

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
    fAxes.Print();
    // Print();
    // SetBranchAddresses();

    TCanvas * c = new TCanvas("cTest", "cTest", 800, 600);
    Ndmspc::Utils::SetAxisRanges(this, ranges);
    // Ndmspc::Utils::SetAxisRanges(this, {{5, 100, 100}});
    AddProjectionIndexes();
    // Print ll indexes
    for (auto & kv : fIndexes) {
      logger->Debug("Index: %lld", kv);
    }

    /// Get List of indexes
    std::vector<Long64_t> indexes = GetProjectionIndexes();

    TH1 * h     = nullptr;
    TH2 * hStat = Projection(6, 5, "O");
    hStat->SetNameTitle("hStat", "Stat");
    hStat->Reset();

    Long64_t sum = 0;
    // Int_t    point[GetNdimensions()];
    for (Long64_t j = 0; j < indexes.size(); j++) {
      // for (Long64_t j = 0; j < fTree->GetEntries(); j++) {
      // bytessum += GetEntry(j);
      logger->Debug("Getting entry %lld ...", indexes[j]);
      bytessum += GetEntry(indexes[j]);
      // Print full fPoint for debugging
      // for (int i = 0; i < GetNdimensions(); i++) {
      //   logger->Trace("fPoint[%d]=%d", i, fPoint[i]);
      // }

      int idx = 0;
      for (auto & kv : GetBranchesMap()) {
        THnSparse * s = (THnSparse *)kv.second.GetObject();
        if (s && s->GetNdimensions() > 0) {
          // s->GetBinContent(0, point);
          // Ndmspc::Utils::SetAxisRanges(s, {{1, (int)j, (int)j}});
          Ndmspc::Utils::SetAxisRanges(s, {});
          if (kv.first == enabledBranches[branchId]) {
            TH1 * hh = s->Projection(0);
            if (h == nullptr)
              h = (TH1 *)hh->Clone();
            else {
              h->Add(hh);
            }
            // Print fPoint for debugging
            // for (int i = 5; i <= 6; i++) {
            //   logger->Info("xxx fPoint[%d]=%d", i, fPoint[i]);
            // }
            // print integral for debugging
            // logger->Info("Integral=%.0f", hh->Integral());
            double integral = hh->Integral();
            if (integral > 0) {
              double binContent = integral + hStat->GetBinContent(fPoint[5], fPoint[6]);
              hStat->SetBinContent(fPoint[5], fPoint[6], binContent);
              sum += hh->Integral();
            }

            if (timeout > 0) {
              hh->Print();
              hh->DrawCopy("hist");
              // h->Draw();
              c->Update();
              c->Modified();
              gSystem->Sleep(timeout);
            }
            delete hh;
          }

          // h->Print();
          // delete h;
        }
        idx++;
      }
      nEntries++;
    }
    if (h) {
      h->Print();
      logger->Info("Sum: %lld", sum);
      c->Clear();
      c->Divide(2, 1);
      c->cd(1);
      h->DrawCopy("hist");
      // c->cd(2)->SetLogy();
      c->cd(2);
      gStyle->SetOptStat(1000111);
      hStat->DrawCopy("colz");
    }
    logger->Info("Bytes read: %.3f MB entries=%lld", (double)bytessum / (1024 * 1024), nEntries);
  }
  timer.Stop();
  timer.Print();
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
