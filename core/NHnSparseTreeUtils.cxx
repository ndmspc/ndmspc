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

#include "NBinning.h"
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
  // hnstIn->Print("PU");
  // hnstIn->Print("P");
  auto binningIn = hnstIn->GetBinning();

  int nBinsFilled = 0;

  // 1,3,1,15 (data)
  binningIn->AddBinning(1, {1, 1, 1}, 1);
  binningIn->AddBinning(2, {1, 1, 3}, 1);
  binningIn->AddBinning(3, {1, 1, 1}, 1);
  // binningIn->AddBinning(4, {1, 1, 11}, 1);
  binningIn->AddBinning(4, {1, 1, 15}, 1);
  std::vector<Long64_t> entries;
  nBinsFilled += binningIn->FillAll(entries);
  // binningIn->PrintContent();
  // return false;

  // return false;

  // binningIn->GetMap()->Reset();

  // 1,1,5,14 (mc)
  std::vector<Long64_t> entriesMc;
  binningIn->AddBinning(1, {1, 1, 1}, 1);
  binningIn->AddBinning(2, {1, 1, 1}, 1);
  binningIn->AddBinning(3, {1, 1, 5}, 1);
  binningIn->AddBinning(4, {1, 1, 14}, 1);
  nBinsFilled += binningIn->FillAll(entriesMc);

  NLogger::Info("Filled %d bins", nBinsFilled);

  // binningIn->GetContent()->Print();
  // NUtils::SetAxisRanges(binningIn->GetContent(), {});
  // NUtils::SetAxisRanges(binningIn->GetContent(), {{5, 2, 2}, {8, 2, 2}, {11, 2, 2}});
  // binningIn->GetContent()->Projection(5, 8, 11, "O")->Draw();
  // binningIn->GetContent()->Projection(3, 4, 5, "O")->Draw();
  // binningIn->GetContent()->Projection(5, 8, 2, "O")->Draw();

  binningIn->PrintContent();
  // return true;

  NHnSparseTree * hnstOut = new NHnSparseTreeC(hnstFileNameOut.c_str());
  if (!hnstOut) {
    NLogger::Error("Cannot create NHnSparseTree via file '%s'", hnstFileNameOut.c_str());
    return false;
  }

  // hsntOut->Import(hnstIn, true /*copy axes*/, false /*copy content*/);

  // TODO: Create binningIn and loop over all axes via GetNdimensionalExecutor
  //
  THnSparse * cSparse = binningIn->GetContent();
  Int_t *     cCoords = new Int_t[cSparse->GetNdimensions()];
  Long64_t    linBin  = 0;
  // cSparse->GetAxis(3)->SetRange(11, 11);
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iterOne{cSparse->CreateIter(true /*use axis range*/)};
  linBin = iterOne->Next();

  Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
  Long64_t         idx           = cSparse->GetBin(cCoords);
  std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
  std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
  // NLogger::Debug("Bin %lld: %s", linBin, binCoordsStr.c_str());
  std::vector<std::vector<int>> coordsRange = binningIn->GetCoordsRange(cCoordsVector);
  // NUtils::PrintPointSafe(coordsRange[1], -1); // mins
  // NUtils::PrintPointSafe(coordsRange[2], -1); // maxs
  // Print linear index in Hnsparse
  Int_t p[hnstIn->GetNdimensions()];
  NUtils::VectorToArray(coordsRange[1], p);

  // print full path
  NLogger::Debug("Bin %lld: %s", hnstIn->GetBin(p), hnstIn->GetFullPath(coordsRange[1]).c_str());
  std::string fileName = hnstIn->GetFullPath(coordsRange[1]);
  std::string cachedir = TFile::GetCacheFileDir();
  TFile *     fOne     = NUtils::OpenFile(fileName.c_str(), cachedir.empty() ? "READ" : "CACHEREAD");
  if (fOne == nullptr) {
    NLogger::Error("Cannot open file '%s'", fileName.c_str());
    return false;
  }
  hnstOut->Print();
  // loop over objNames and print content
  for (size_t i = 0; i < objNames.size(); i++) {
    std::string objName = directory;
    if (!directory.empty()) {
      objName += "/";
    }
    objName += objNames[i];
    if (!hnstOut->GetNdimensions()) {
      NLogger::Debug("Preparing branch %s from object '%s' ...", objNames[i].c_str(), objName.c_str());
      NLogger::Info("Initializing 'hnstOut' from %s", objName.c_str());
      TObjArray * axesNew = (TObjArray *)hnstIn->GetListOfAxes()->Clone();
      THnSparse * hns     = (THnSparse *)fOne->Get(objName.c_str());
      if (hns == nullptr) {
        NLogger::Warning("Cannot find object '%s' in file '%s'!!! Skipping ...", objName.c_str(), fileName.c_str());
        continue;
      }
      // loop over all axes in hns and get axis
      for (int j = 0; j < hns->GetNdimensions(); j++) {
        TAxis * aIn = hns->GetAxis(j);
        axesNew->Add(aIn->Clone());
        std::string axisName = aIn->GetName();
        if (axisName == "axis1-pt") {
          NLogger::Info("Found axis1-pt axis_id=%d", axesNew->GetEntries() - 1);
        }
      }
      // axesNew->Print();
      hnstOut->InitAxes(axesNew, hnstIn->GetNdimensions());
      // hnstOut->Print("P");
      // return false;
    }
    hnstOut->AddBranch(objNames[i], nullptr, "THnSparseD");
    // hnstOut->GetBranch(objNames[i])->SetAddress(hns);
    // hnstOut->Print("");
  }
  fOne->Close();

  hnstOut->Print();
  NBinning *                                      binningOut = (NBinning *)hnstOut->GetBinning()->Clone();
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
    Long64_t         idx           = cSparse->GetBin(cCoords);
    std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
    std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
    NLogger::Debug("Bin %lld: %s", linBin, binCoordsStr.c_str());
    std::vector<std::vector<int>> coordsRange = binningIn->GetCoordsRange(cCoordsVector);
    // NUtils::PrintPointSafe(coordsRange[1], -1); // mins
    // NUtils::PrintPointSafe(coordsRange[2], -1); // maxs
    // Print linear index in Hnsparse
    Int_t p[hnstIn->GetNdimensions()];
    NUtils::VectorToArray(coordsRange[0], p);

    // for (int i = 0; i < binningIn->GetAxes().size(); i++) {
    int index = 0;
    int i     = 0;
    for (auto & iAxis : binningOut->GetAxes()) {

      NLogger::Debug("Axis %d [%s]: %d %s", i, iAxis->GetName(), p[index],
                     binningOut->GetBinningType(i) == Binning::kSingle ? "single" : "multiple");
      if (binningOut->GetBinningType(i) == Binning::kSingle) {
        if (i < hnstIn->GetNdimensions()) {

          // NLogger::Debug("p= [%d,%d,%d,%d]", i + 1, 1, 1, p[index]);
          binningOut->AddBinning(i + 1, {1, 1, p[index]}, 1);
        }
        else {
          binningOut->AddBinning(i + 1, {iAxis->GetNbins(), 1, 1}, 1);
        }
        index++;
      }
      else if (binningOut->GetBinningType(i) == Binning::kMultiple) {
        if (i < hnstIn->GetNdimensions()) {
          binningOut->AddBinning(i + 1, {p[index], p[index + 1], p[index + 1]}, 1);
        }
        else {
          std::string axisName = iAxis->GetName();
          if (axisName == "axis1-pt") {
            // if (axisName == "axis1-pt" || axisName == "axis2-ce" || axisName == "axis5-eta") {
            std::vector<std::vector<int>> widths = {{5}, {1, 14}, {2, 20}, {5, 10}, {10, 20}};
            std::vector<int>              mins;
            mins.push_back(1);
            for (auto & w : widths) {
              int width   = w[0];
              int nWidths = w.size() > 1 ? w[1] : 1;
              for (int iWidth = 0; iWidth < nWidths; iWidth++) {
                mins.push_back(mins[mins.size() - 1] + width);
              }
            }

            binningOut->AddBinningVariable(i + 1, mins);
            // binningOut->AddBinningVariable(i + 1, {1, 11, 21, 22, 23, 24, 25, 26, 200});
            // for (int j = 0; j < iAxis->GetNbins(); j++) {
            //   binningOut->AddBinning(i + 1, {1, 1, j + 1}, 1);
            // }
          }
          else {
            binningOut->AddBinning(i + 1, {iAxis->GetNbins(), 1, 1}, 1);
          }
        }
        index += 3;
      }
      i++;
      // binningOut->GetMap()->Reset();
    }
    std::vector<Long64_t> entries;
    binningOut->FillAll(entries);
  }
  binningOut->Print();

  auto pContentAll = binningOut->GetContent()->Projection(1, 2, 9, "O");
  pContentAll->SetMinimum(0);
  pContentAll->Draw("colz");
  hnstOut->Print();
  return false;

  // // loop over all selected bins via ROOT iterarot for THnSparse
  // THnSparse * cSparse = binningIn->GetContent();
  // cSparse->GetAxis(3)->SetRange(11, 11);
  // Int_t *                                         cCoords = new Int_t[cSparse->GetNdimensions()];
  // Long64_t                                        linBin  = 0;
  // std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  // while ((linBin = iter->Next()) >= 0) {
  //   Double_t         v             = cSparse->GetBinContent(linBin, cCoords);
  //   Long64_t         idx           = cSparse->GetBin(cCoords);
  //   std::vector<int> cCoordsVector = NUtils::ArrayToVector(cCoords, cSparse->GetNdimensions());
  //   std::string      binCoordsStr  = NUtils::GetCoordsString(cCoordsVector, -1);
  //   // NLogger::Debug("Bin %lld: %s", linBin, binCoordsStr.c_str());
  //   std::vector<std::vector<int>> coordsRange = binningIn->GetCoordsRange(cCoordsVector);
  //   // NUtils::PrintPointSafe(coordsRange[1], -1); // mins
  //   // NUtils::PrintPointSafe(coordsRange[2], -1); // maxs
  //   // Print linear index in Hnsparse
  //   Int_t p[hnstIn->GetNdimensions()];
  //   NUtils::VectorToArray(coordsRange[1], p);
  //
  //   // print full path
  //   NLogger::Debug("Bin %lld: %s", hnstIn->GetBin(p), hnstIn->GetFullPath(coordsRange[1]).c_str());
  //   std::string fileName = hnstIn->GetFullPath(coordsRange[1]);
  //   std::string cachedir = TFile::GetCacheFileDir();
  //   TFile *     f        = NUtils::OpenFile(fileName.c_str(), cachedir.empty() ? "READ" : "CACHEREAD");
  //   if (f == nullptr) {
  //     NLogger::Error("Cannot open file '%s'", fileName.c_str());
  //     continue;
  //   }
  //   // loop over objNames and print content
  //   for (size_t i = 0; i < objNames.size(); i++) {
  //     std::string objName = directory;
  //     if (!directory.empty()) {
  //       objName += "/";
  //     }
  //     objName += objNames[i];
  //     NLogger::Debug("Preparing branch %s from object '%s' ...", objNames[i].c_str(), objName.c_str());
  //     THnSparse * hns = (THnSparse *)f->Get(objName.c_str());
  //     if (hns == nullptr) {
  //       NLogger::Warning("Cannot find object '%s' in file '%s'!!! Skipping ...", objName.c_str(), fileName.c_str());
  //       continue;
  //     }
  //     if (!hnstOut->GetNdimensions()) {
  //       NLogger::Info("Initializing 'hnstOut' from %s", objName.c_str());
  //       TObjArray * axesNew = (TObjArray *)hnstIn->GetListOfAxes()->Clone();
  //       // loop over all axes in hns and get axis
  //       for (int j = 0; j < hns->GetNdimensions(); j++) {
  //         TAxis * aIn = hns->GetAxis(j);
  //         axesNew->Add(aIn->Clone());
  //         std::string axisName = aIn->GetName();
  //         if (axisName == "axis1-pt") {
  //           NLogger::Info("Found axis1-pt axis_id=%d", axesNew->GetEntries() - 1);
  //         }
  //       }
  //       // axesNew->Print();
  //       hnstOut->InitAxes(axesNew, hnstIn->GetNdimensions());
  //       // hnstOut->Print("P");
  //       // return false;
  //     }
  //     hnstOut->AddBranch(objNames[i], nullptr, "THnSparseD");
  //     hnstOut->GetBranch(objNames[i])->SetAddress(hns);
  //     // hnstOut->Print("");
  //   }
  //   // Added all points to hnstOut
  //   // TODO: This has to be changed and port it to binningIn content THnSparse
  //   // 1. One THnSparse with original axes for data,mc,... nad one integrated bing for imported object
  //   // 2. Binning content THnSparse with all axes for current binningIn
  //   std::vector<int> coordsOut(hnstOut->GetNdimensions(), 1);
  //   for (int i = 0; i < hnstIn->GetNdimensions(); i++) {
  //     coordsOut[i] = coordsRange[1][i];
  //   }
  //   hnstOut->SetPoint(coordsOut);
  //   auto binningOut = hnstOut->GetBinning();
  //   binningOut->GetMap()->Reset();
  //   for (int i = 0; i < hnstIn->GetNdimensions(); i++) {
  //     binningOut->AddBinning({i + 1, 1, 1, coordsRange[1][i]}, 1);
  //   }
  //
  //   // add binningIn for reset of axes
  //   for (int i = hnstIn->GetNdimensions(); i < hnstOut->GetNdimensions(); i++) {
  //     // print axis
  //     TAxis * axis = hnstOut->GetAxis(i);
  //     binningOut->AddBinning({i + 1, axis->GetNbins(), 1, 1}, 1);
  //   }
  //   // binningOut->GetMap()->Projection(0, 1, 3)->Draw();
  //   // return false;
  //
  //   binningOut->FillAll();
  //   // binningOut->Print();
  //
  //   hnstOut->FillTree();
  //   f->Close();
  // }
  // auto binningOut = hnstOut->GetBinning();
  // binningOut->Print();
  // delete[] cCoords;
  // hnstOut->Close(true);
  //
  // // return true;
  // NLogger::Info("%s", NUtils::GetCoordsString(objNames, -1).c_str());
  //
  // // NUtils::SetAxisRanges(binningIn->GetMap(), {});
  // TCanvas * c = new TCanvas("c", "c", 800, 600);
  // c->Divide(2, 2);
  // c->cd(1);
  // hnstIn->Projection(1, 2, 3, "O")->Draw();
  // c->cd(2);
  // hnstIn->Projection(1, 2, 0, "O")->Draw();
  // c->cd(3);
  // binningOut->GetMap()->Projection(0, 1, 3, "O")->Draw();
  // c->cd(4);
  // auto pContentAll = binningOut->GetContent()->Projection(1, 2, 3, "O");
  // pContentAll->SetMinimum(0);
  // pContentAll->Draw();
  // return true;
}
} // namespace Ndmspc
