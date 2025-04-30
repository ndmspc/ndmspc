#include <cstddef>
#include <set>
#include <vector>
#include <string>
#include "TAxis.h"
#include "THnSparse.h"
#include "TObjArray.h"
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
bool NHnSparseTreeUtils::Reshape(std::string hnstFileNameIn, std::vector<std::string> objNames, std::string directory,
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
  hnstIn->Print("PU");
  hnstIn->Print("P");
  auto binning = hnstIn->GetBinning();

  int nBinsFilled = 0;

  // 1,3,1,15 (data)
  binning->AddBinning({1, 1, 1, 1}, 1);
  binning->AddBinning({2, 1, 1, 3}, 1);
  binning->AddBinning({3, 1, 1, 1}, 1);
  binning->AddBinning({4, 1, 1, 11}, 1);
  binning->AddBinning({4, 1, 1, 15}, 1);
  nBinsFilled += binning->FillAll();
  // binning->PrintContent();
  // return false;

  // return false;

  // binning->GetMap()->Reset();

  // 1,1,5,14 (mc)
  binning->AddBinning({1, 1, 1, 1}, 1);
  binning->AddBinning({2, 1, 1, 1}, 1);
  binning->AddBinning({3, 1, 1, 5}, 1);
  binning->AddBinning({4, 1, 1, 14}, 1);
  nBinsFilled += binning->FillAll();

  NLogger::Info("Filled %d bins", nBinsFilled);

  // binning->GetContent()->Print();
  // NUtils::SetAxisRanges(binning->GetContent(), {});
  // NUtils::SetAxisRanges(binning->GetContent(), {{5, 2, 2}, {8, 2, 2}, {11, 2, 2}});
  // binning->GetContent()->Projection(5, 8, 11, "O")->Draw();
  // binning->GetContent()->Projection(3, 4, 5, "O")->Draw();
  // binning->GetContent()->Projection(5, 8, 2, "O")->Draw();

  binning->PrintContent();
  //

  NHnSparseTree * hnstOut = new NHnSparseTreeC(hnstFileNameOut.c_str());
  if (!hnstOut) {
    NLogger::Error("Cannot create NHnSparseTree via file '%s'", hnstFileNameOut.c_str());
    return false;
  }
  hnstOut->Print();

  // loop over all selected bins via ROOT iterarot for THnSparse
  THnSparse *                                     cSparse = binning->GetContent();
  Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogger::Debug("Bin %lld: %s", linBin, binCoordsStr.c_str());
    std::vector<std::vector<int>> coordsRange = binning->GetCoordsRange(cCoordsVector);
    NUtils::PrintPointSafe(coordsRange[0], -1); // mins
    NUtils::PrintPointSafe(coordsRange[1], -1); // maxs
    // Print linear index in Hnsparse
    Int_t p[hnstIn->GetNdimensions()];
    NUtils::VectorToArray(coordsRange[0], p);

    // print full path
    NLogger::Debug("Bin %lld: %s", hnstIn->GetBin(p), hnstIn->GetFullPath(coordsRange[0]).c_str());
    std::string fileName = hnstIn->GetFullPath(coordsRange[0]);
    TFile *     f        = NUtils::OpenFile(fileName.c_str());
    if (f == nullptr) {
      NLogger::Error("Cannot open file '%s'", fileName.c_str());
      continue;
    }
    // loop over objNames and print content
    for (size_t i = 0; i < objNames.size(); i++) {
      NLogger::Debug("%s", objNames[i].c_str());
      std::string objName = directory;
      if (!directory.empty()) {
        objName += "/";
      }
      objName += objNames[i];
      THnSparse * hns = (THnSparse *)f->Get(objName.c_str());
      if (hns == nullptr) {
        NLogger::Error("Cannot find object '%s' in file '%s'", objName.c_str(), fileName.c_str());
        continue;
      }
      if (!hnstOut->GetNdimensions()) {
        NLogger::Info("Initializing 'hnstOut' from %s", objName.c_str());
        TObjArray * axes = (TObjArray *)hnstIn->GetListOfAxes()->Clone();
        // loop over all axes in hns and get axis
        for (int j = 0; j < hns->GetNdimensions(); j++) {
          axes->Add(hns->GetAxis(j)->Clone());
        }
        hnstOut->InitAxes(axes, hnstIn->GetNdimensions());
      }
      hnstOut->AddBranch(objNames[i], nullptr, "THnSparseD");
      hnstOut->GetBranch(objNames[i])->SetAddress(hns);
      // hnstOut->Print("");
    }
    // Added all points to hnstOut
    // TODO: This has to be changed and port it to binning content THnSparse
    // 1. One THnSparse with original axes for data,mc,... nad one integrated bing for imported object
    // 2. Binning content THnSparse with all axes for current binning
    std::vector<int> coordsOut(hnstOut->GetNdimensions(), 1);
    for (size_t i = 0; i < hnstIn->GetNdimensions(); i++) {
      coordsOut[i] = coordsRange[0][i];
    }

    hnstOut->SetPoint(coordsOut);

    hnstOut->FillTree();
    f->Close();
  }
  delete[] cCoords;
  hnstOut->Close(true);

  return true;
  NLogger::Info("%s", NUtils::GetCoordsString(objNames, -1).c_str());

  // NUtils::SetAxisRanges(binning->GetMap(), {});
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
  return true;
}
} // namespace Ndmspc
