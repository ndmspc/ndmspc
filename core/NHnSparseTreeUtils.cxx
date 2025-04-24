#include <set>
#include <vector>
#include <string>
#include "TAxis.h"
#include "THnSparse.h"
#include "TSystem.h"
#include <TStopwatch.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TCanvas.h>

#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreeUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTreeUtils);
/// \endcond

namespace Ndmspc {
NHnSparseTreeUtils::NHnSparseTreeUtils() : TObject() {}
NHnSparseTreeUtils::~NHnSparseTreeUtils() {}

NHnSparseTree * NHnSparseTreeUtils::Create(std::vector<std::vector<std::string>> points,
                                           std::vector<std::string> axisNames, std::vector<std::string> axisTitles)
{
  ///
  /// Constructor from points
  ///

  int        nPoints = points.size();
  int        nDims   = points[0].size();
  Int_t *    nbins   = new Int_t[nDims];
  Double_t * mins    = new Double_t[nDims];
  Double_t * maxs    = new Double_t[nDims];

  std::vector<std::set<std::string>> space(nDims);

  for (auto & point : points) {
    for (size_t i = 0; i < point.size(); i++) {
      space[i].insert(point[i]);
    }
  }

  // Print all sets in space
  for (size_t i = 0; i < space.size(); i++) {
    std::string s = "";
    for (auto & j : space[i]) {
      s += j + " ";
    }
    NLogger::Debug("Set %d: %s", i, s.c_str());
  }

  for (int i = 0; i < nDims; i++) {
    nbins[i] = space[i].size();
    mins[i]  = 0;
    maxs[i]  = space[i].size();
  }

  if (axisTitles.size() == 0) {
    for (size_t i = 0; i < axisNames.size(); i++) {
      axisTitles.push_back(axisNames[i]);
    }
  }

  if (axisNames.size() != axisTitles.size()) {
    NLogger::Error("Invalid size %d [axisNames] != %d [axisTitles]", axisNames.size(), axisTitles.size());
    return nullptr;
  }

  NHnSparseTree * hnst = new NHnSparseTreeC("hnst", "hnst", nDims, nbins, mins, maxs);
  if (hnst == nullptr) {
    NLogger::Error("Cannot create HnSparseTree");
    return nullptr;
  }

  // loop oper all axes and set label in each bin of axis
  for (int i = 0; i < nDims; i++) {
    int j = 1;

    // check if name is in axisNames and set name and title for this axis

    if (i < axisNames.size()) {
      hnst->GetAxis(i)->SetNameTitle(axisNames[i].c_str(), TString::Format("%s", axisTitles[i].c_str()).Data());
    }

    for (auto & b : space[i]) {
      hnst->GetAxis(i)->SetBinLabel(j, b.c_str());
      j++;
    }
  }

  // print all axes and their bin labels
  for (int i = 0; i < nDims; i++) {
    NLogger::Info("Axis %d: %s", i, hnst->GetAxis(i)->GetName());
    for (int j = 1; j <= nbins[i]; j++) {
      NLogger::Info("  Bin %d: %s", j, hnst->GetAxis(i)->GetBinLabel(j));
    }
  }

  // Fill all points
  int p[nDims];
  for (auto & point : points) {
    for (size_t i = 0; i < point.size(); i++) {
      p[i] = hnst->GetAxis(i)->FindBin(point[i].c_str());
    }
    hnst->SetBinContent(p, 1);
  }
  // hnst->Print();
  // hnst->Projection(1, 2)->Draw();

  delete[] nbins;
  delete[] mins;
  delete[] maxs;

  return hnst;
}

bool NHnSparseTreeUtils::CreateFromDir(std::string dir, std::vector<std::string> axisNames, std::string filter)
{
  ///
  /// Create HnSparseTree from directory
  ///

  // ceck if dir has slash at the end, if yes remove it
  if (dir.back() == '/') {
    dir.pop_back();
  }

  TH1::AddDirectory(kFALSE);
  TStopwatch timer;
  timer.Start();
  std::vector<std::string>              paths = NUtils::Find(dir, filter);
  std::vector<std::vector<std::string>> bins;

  for (auto & p : paths) {
    p.erase(p.find(dir), dir.length());
    if (!filter.empty()) p.erase(p.find(filter), filter.length());
    NLogger::Debug("Path: %s", p.c_str());
    std::vector<std::string> bin = NUtils::Tokenize(p, '/');

    bins.push_back(bin);
  }

  // print bins
  for (auto & b : bins) {
    std::string s = "";
    for (auto & i : b) {
      s += i + " ";
    }
    NLogger::Debug("Bin: %s", s.c_str());
  }
  NHnSparseTree * hnst = NHnSparseTreeUtils::Create(bins, axisNames);

  NLogger::Debug("postfix=%s", filter.c_str());
  hnst->SetPrefix(dir);
  hnst->SetPostfix(filter);
  hnst->InitTree(dir + "/hnst.root");
  // // hnst->FillPoints(points);
  hnst->Print();
  hnst->Close(true);
  delete hnst;
  timer.Stop();
  timer.Print();
  return true;
}
bool NHnSparseTreeUtils::Reshape(std::string hnstFileNameIn, std::vector<std::string> objNames,
                                 std::string hnstFileNameOut)
{
  ///
  /// Reshape from HnSparseTree to new HnSparseTree
  ///

  TH1::AddDirectory(kFALSE);

  NHnSparseTree * hnstIn = NHnSparseTree::Open(hnstFileNameIn.c_str());
  if (!hnstIn) {
    NLogger::Error("Cannot open file '%s'", hnstFileNameIn.c_str());
    return false;
  }
  hnstIn->Print("P");
  auto binning = hnstIn->GetBinning();

  // TODO: Add binnings
  // binning->AddRange(iAxis,rebin,nbins);
  // binning->AddRange(iAxis,min,max)
  Int_t   nDims = binning->GetMap()->GetNdimensions();
  Int_t * point = new Int_t[nDims];
  NUtils::VectorToArray({1, 1, 1, 1}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({2, 1, 1, 1}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({2, 1, 1, 2}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({2, 1, 1, 3}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({3, 1, 1, 2}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({3, 2, 1, 2}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({4, 2, 1, 1}, point);
  binning->GetMap()->SetBinContent(point, 1);
  NUtils::VectorToArray({4, 1, 1, 3}, point);
  binning->GetMap()->SetBinContent(point, 1);

  // auto                          b = binning->GetMap();
  // std::vector<std::vector<int>> points;
  // for (int i = 0; i < hnstIn->GetNdimensions(); i++) {
  //   /// loop axis nbins and push_back
  //   for (int j = 1; j <= hnstIn->GetAxis(i)->GetNbins(); j++) {
  //     // TODO: This has to be added by user
  //     points.push_back({i + 1, 1, 1, j});
  //   }
  // }
  //
  // // create point
  // Int_t * point = new Int_t[nDims];
  // for (int i = 0; i < points.size(); i++) {
  //   NUtils::VectorToArray(points[i], point);
  //   b->SetBinContent(point, 1);
  // }
  // binning->Print();

  // WARN: Think before using this
  // NUtils::SetAxisRanges(binning->GetMap(), {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});

  // binning->GetMap()->Projection(0, 1, 3, "O")->Draw();
  // return true;
  // std::vector<std::vector<int>> all_points  = binning->GetAll();
  int nBinsFilled = binning->FillAll();
  NLogger::Info("Filled %d bins", nBinsFilled);
  // return true;
  binning->GetContent()->Print();
  // NUtils::SetAxisRanges(binning->GetContent(), {});
  // NUtils::SetAxisRanges(binning->GetContent(), {{5, 2, 2}, {8, 2, 2}, {11, 2, 2}});
  // binning->GetContent()->Projection(5, 8, 11, "O")->Draw();
  // binning->GetContent()->Projection(3, 4, 5, "O")->Draw();
  // binning->GetContent()->Projection(5, 8, 2, "O")->Draw();

  binning->PrintContent();
  //
  // // loop over all selected bins via ROOT iterarot for THnSparse
  // THnSparse *                                     cSparse = binning->GetContent();
  // Int_t *                                         bins    = new Int_t[cSparse->GetNdimensions()];
  // Long64_t                                        linBin  = 0;
  // std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  // while ((linBin = iter->Next()) >= 0) {
  //   Double_t    v         = cSparse->GetBinContent(linBin, bins);
  //   Long64_t    idx       = cSparse->GetBin(bins);
  //   std::string binCoords = NUtils::GetCoordsString(NUtils::ArrayToVector(bins, cSparse->GetNdimensions()), -1);
  //   NLogger::Debug("Bin %lld: %s", linBin, binCoords.c_str());
  // }
  // delete[] bins;

  NUtils::SetAxisRanges(binning->GetMap(), {});
  TCanvas * c = new TCanvas("c", "c", 800, 600);
  c->Divide(2, 2);
  c->cd(1);
  hnstIn->Projection(1, 2, 3, "O")->Draw();
  c->cd(2);
  hnstIn->Projection(1, 2, 0, "O")->Draw();
  c->cd(3);
  binning->GetMap()->Projection(0, 1, 3, "O")->Draw();
  c->cd(4);
  auto pContentAll = binning->GetContent()->Projection(5, 8, 11, "O");
  pContentAll->SetMinimum(0);
  pContentAll->Draw();
  delete[] point;
  return true;
}
} // namespace Ndmspc
