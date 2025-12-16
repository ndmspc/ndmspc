#include <string>
#include <vector>
#include "TAxis.h"
#include "THnSparse.h"
#include <TSystem.h>
#include <TMath.h>
#include <TROOT.h>
#include <TVirtualPad.h>
#include <TPad.h>
#include <TCanvas.h>
#include <TLatex.h>
#include <TH1.h>
#include <THStack.h>
#include <TBufferJSON.h>
#include <TGClient.h>
#include <TPaveText.h>

#include "Buttons.h"
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NGnTree.h"
#include "NLogger.h"
#include "NParameters.h"
#include "NUtils.h"
#include "NWsClient.h"

#include "NGnNavigator.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnNavigator);
/// \endcond

namespace Ndmspc {
NGnNavigator::NGnNavigator(const char * name, const char * title, std::vector<std::string> objectTypes)
    : TNamed(name, title), fObjectTypes(objectTypes)
{
}
NGnNavigator::~NGnNavigator() {}

NGnNavigator * NGnNavigator::Reshape(std::string binningName, std::vector<std::vector<int>> levels, int level,
                                     std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Reshape the navigator
  ///
  NBinningDef * binningDef = fGnTree->GetBinning()->GetDefinition(binningName);
  if (!binningDef) {
    NLogError("NGnNavigator::Reshape: Binning definition is null !!!");
    return nullptr;
  }
  std::vector<int> axes;
  int              nAxes = 0;
  if (levels.empty()) {

    size_t nVarAxes = binningDef->GetVariableAxes().size();
    if (nVarAxes == 0) {
      NLogError("NGnNavigator::Reshape: Binning definition has no variable axes !!!");
      return nullptr;
    }
    if (nVarAxes > 3) {
      NLogError("NGnNavigator::Reshape: Binning definition has more than 3 variable axes (%zu) !!!", nVarAxes);
      return nullptr;
    }

    NLogTrace("========== NGnNavigator::Reshape: Levels are empty, using all variable axes...");
    levels.resize(1);
    for (size_t i = 0; i < nVarAxes; i++) {
      levels[0].push_back(i);
    }
    // set levels[0] in reverse order
    for (size_t i = 0; i < nVarAxes / 2; i++) {
      std::swap(levels[0][i], levels[0][nVarAxes - i - 1]);
    }
  }

  for (auto & l : levels) {
    nAxes += l.size();
    for (auto & a : l) {
      if (std::find(axes.begin(), axes.end(), a) == axes.end()) {
        axes.push_back(a);
      }
    }
  }
  NLogTrace("============= NGnNavigator::Reshape: Number of axes in levels = %s",
            NUtils::GetCoordsString(axes, -1).c_str());
  std::vector<int> axesSorted = axes;
  std::sort(axesSorted.begin(), axesSorted.end());
  std::vector<int> axesVariavble = binningDef->GetVariableAxes();
  std::sort(axesVariavble.begin(), axesVariavble.end());

  NLogTrace("NGnNavigator::Reshape: Axes in levels before removing duplicates: %s",
            NUtils::GetCoordsString(axesSorted, -1).c_str());

  // remove all duplicates from axesSorted
  for (size_t i = 1; i < axesSorted.size(); i++) {
    if (axesSorted[i] == axesSorted[i - 1]) {
      axesSorted.erase(axesSorted.begin() + i);
      i--;
    }
  }

  if (axesSorted != axesVariavble) {
    NLogError("NGnNavigator::Reshape: Axes in levels '%s' do not match variable axes in binning definition '%s' !!!",
              NUtils::GetCoordsString(axesSorted, -1).c_str(), NUtils::GetCoordsString(axesVariavble, -1).c_str());
    return nullptr;
  }

  // NLogDebug("NGnNavigator::Reshape: Number of axes in levels = %d GetVariableAxes=%zu", nAxes,
  //                binningDef->GetVariableAxes().size());
  // return nullptr;

  if (nAxes != binningDef->GetVariableAxes().size()) {
    NLogError("NGnNavigator::Reshape: Number of axes in levels (%d) does not match number of axes in binning "
              "definition (%d) !!! Available axes indices: %s",
              nAxes, binningDef->GetVariableAxes().size(),
              NUtils::GetCoordsString(binningDef->GetVariableAxes(), -1).c_str());

    return nullptr;
  }

  NLogInfo("NGnNavigator::Reshape: Reshaping navigator for level %d/%zu with binning '%s' ...", level, levels.size(),
           binningName.c_str());
  return Reshape(binningDef, levels, level, ranges, rangesBase);
}
NGnNavigator * NGnNavigator::Reshape(NBinningDef * binningDef, std::vector<std::vector<int>> levels, int level,
                                     std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase,
                                     NGnNavigator * parent)
{
  ///
  /// Reshape the navigator
  ///

  NLogTrace("NGnNavigator::Reshape: Reshaping navigator for level=%d levels=%zu", level, levels.size());
  TH1::AddDirectory(kFALSE);

  fNLevels = levels.size();
  fLevel   = level;
  // NBinningDef * binningDef = fGnTree->GetBinning()->GetDefinition(binningName);

  NGnNavigator * current = parent;
  if (current == nullptr) {
    NLogDebug("NGnNavigator::Reshape: Creating root navigator %d/%zu...", level, levels.size());
    current = new NGnNavigator(TString::Format("%s_L%d", GetName(), level).Data(),
                               TString::Format("%s_L%d", GetTitle(), level).Data());
    // current->SetParent(this);
    current->SetLevel(fLevel);
    current->SetNLevels(fNLevels);
    current->SetGnTree(fGnTree);
  }
  // current->Print();
  // return current;

  if (level < levels.size()) {
    NLogTrace("NGnNavigator::Reshape: levels[%d]=%s...", level, NUtils::GetCoordsString(levels[level]).c_str());

    // Generate projection histogram

    // loop for every bin in the current level

    std::vector<int> minsBin;
    std::vector<int> maxsBin;
    for (auto & idx : levels[level]) {
      NLogTrace("NGnNavigator::Reshape: [B%d] Axis %d: %s", level, idx,
                binningDef->GetContent()->GetAxis(idx)->GetName());
      // int minBase = 0, maxBase = 0;
      // NUtils::GetAxisRangeInBase(GetAxis(idx), 1, GetAxis(idx)->GetNbins(), fBinning->GetAxes()[idx], minBase,
      // maxBase); ranges[idx] = {minBase, maxBase};            // Set the ranges for the axis
      minsBin.push_back(1);                                                  // Get the minimum bin edge);
      maxsBin.push_back(binningDef->GetContent()->GetAxis(idx)->GetNbins()); // Get the maximum bin edge);
      NLogTrace("NGnNavigator::Reshape: [B%d] Axis %d: %s bins=[%d,%d]", level, idx,
                binningDef->GetContent()->GetAxis(idx)->GetName(), minsBin.back(), maxsBin.back());
    }

    NDimensionalExecutor executorBin(minsBin, maxsBin);
    auto                 loop_task_bin = [this, current, binningDef, levels, level, ranges,
                          rangesBase](const std::vector<int> & coords) {
      NLogTrace("NGnNavigator::Reshape: [B%d] Processing coordinates: coords=%s levels=%s", level,
                                NUtils::GetCoordsString(coords, -1).c_str(), NUtils::GetCoordsString(levels[level]).c_str());
      NLogTrace("NGnNavigator::Reshape: [L%d] Generating %zuD histogram %s with ranges: %s", level,
                                levels[level].size(), NUtils::GetCoordsString(levels[level]).c_str(), ranges.size() == 0 ? "[]" : "");

      std::vector<int> axesIds = levels[level];
      ///////// Make projection histogram /////////
      THnSparse * hns   = nullptr;
      Int_t       nDims = axesIds.size();
      Int_t       dims[nDims];
      for (int i = 0; i < nDims; i++) {
        dims[i] = axesIds[i];
      }
      THnSparse * hnsIn = binningDef->GetContent();

      NUtils::SetAxisRanges(hnsIn, ranges); // Set the ranges for the axes
      hns = static_cast<THnSparse *>(hnsIn->ProjectionND(axesIds.size(), dims, "O"));
      if (!hns) {
        NLogError("NGnNavigator::Reshape: Projection failed for level %d !!!", level);
        return;
      }

      //   // int nCells = h->GetNcells();
      //   // h->SetMinimum(0);
      //   // h->SetStats(kFALSE);
      std::string name  = "";
      std::string title = "";
      for (auto & axisId : axesIds) {
        TAxis * a = hnsIn->GetAxis(axisId);
        title += std::string(a->GetName()) + " vs ";
        name += TString::Format("%s-", a->GetName()).Data();
      }
      name = name.substr(0, name.size() - 1);    // Remove last "_"
      if (name.empty()) name = "hns_proj";       // default name
      title = title.substr(0, title.size() - 4); // Remove last " vs "
      if (ranges.size() > 0) title += " for ranges: ";
      for (const auto & [axisId, range] : rangesBase) {
        // NLogDebug("XX Axis '%s' range: [%d, %d]", GetAxis(axisId)->GetName(), range[0], range[1]);
        TAxis * a = hnsIn->GetAxis(axisId);
        title += TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(range[0]), a->GetBinUpEdge(range[1]));
      }
      hns->SetTitle(title.c_str());
      // hns->Print();

      // TODO: Handle it via NGnSparseObject
      // if (obj->GetHnSparse() == nullptr) {
      //   NLogDebug("NGnNavigator::Reshape: Setting histogram '%s' ...", hns->GetTitle());
      //   obj->SetHnSparse(hns);
      // }

      int   indexInProj = -1;
      TH1 * hProj       = nullptr;
      // if (level < 3) {
      if (nDims == 1) {
        hProj = hnsIn->Projection(axesIds[0]);
        // set name from hnsIn
        TAxis * axisIn0   = hnsIn->GetAxis(axesIds[0]);
        TAxis * axisProjX = hProj->GetXaxis();
        axisProjX->SetName(axisIn0->GetName());
        // apply lables from hnsIn to hProj
        if (axisIn0->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsX(); b++) {
            axisProjX->SetBinLabel(b, axisIn0->GetBinLabel(b));
          }
        }
        indexInProj = hProj->FindFixBin(axisProjX->GetBinCenter(coords[0]));
      }
      else if (nDims == 2) {
        // TODO: Check the order of axes is really correct
        hProj             = hnsIn->Projection(axesIds[1], axesIds[0]);
        TAxis * axisIn1   = hnsIn->GetAxis(axesIds[0]);
        TAxis * axisIn0   = hnsIn->GetAxis(axesIds[1]);
        TAxis * axisProjX = hProj->GetXaxis();
        TAxis * axisProjY = hProj->GetYaxis();
        axisProjX->SetName(axisIn1->GetName());
        axisProjY->SetName(axisIn0->GetName());
        // apply lables from hnsIn to hProj
        if (axisIn1->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsX(); b++) {
            axisProjX->SetBinLabel(b, axisIn1->GetBinLabel(b));
          }
        }

        if (axisIn0->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsY(); b++) {
            axisProjY->SetBinLabel(b, axisIn0->GetBinLabel(b));
          }
        }
        indexInProj = hProj->FindFixBin(axisProjX->GetBinCenter(coords[0]), axisProjY->GetBinCenter(coords[1]));
      }
      else if (nDims == 3) {
        hProj             = hnsIn->Projection(axesIds[0], axesIds[1], axesIds[2]);
        TAxis * axisIn0   = hnsIn->GetAxis(axesIds[0]);
        TAxis * axisIn1   = hnsIn->GetAxis(axesIds[1]);
        TAxis * axisIn2   = hnsIn->GetAxis(axesIds[2]);
        TAxis * axisProjX = hProj->GetXaxis();
        TAxis * axisProjY = hProj->GetYaxis();
        TAxis * axisProjZ = hProj->GetZaxis();
        axisProjX->SetName(axisIn0->GetName());
        axisProjY->SetName(axisIn1->GetName());
        axisProjZ->SetName(axisIn2->GetName());
        // apply lables from hnsIn to hProj
        if (axisIn0->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsX(); b++) {
            axisProjX->SetBinLabel(b, axisIn0->GetBinLabel(b));
          }
        }

        if (axisIn1->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsY(); b++) {
            axisProjY->SetBinLabel(b, axisIn1->GetBinLabel(b));
          }
        }
        if (axisIn2->IsAlphanumeric()) {
          for (int b = 1; b <= hProj->GetNbinsZ(); b++) {
            axisProjZ->SetBinLabel(b, axisIn2->GetBinLabel(b));
          }
        }
        indexInProj = hProj->FindFixBin(axisProjX->GetBinCenter(coords[0]), axisProjY->GetBinCenter(coords[1]),
                                                        axisProjZ->GetBinCenter(coords[2]));
      }
      else {
        NLogError("NGnNavigator::Reshape: Cannot project THnSparse with %d dimensions", nDims);
        return;
      }
      if (!hProj) {
        NLogError("NGnNavigator::Reshape: Projection failed for level %d !!!", level);
        return;
      }

      hProj->SetName(name.c_str());
      hProj->SetTitle(title.c_str());
      NLogTrace("NGnNavigator::Reshape: [L%d] Projection histogram '%s' for coords=%s index=%d", level,
                                hProj->GetTitle(), NUtils::GetCoordsString(coords, -1).c_str(), indexInProj);
      //
      // hProj->SetMinimum(0);
      // hProj->SetStats(kFALSE);
      // hProj->Draw();
      // gPad->ModifiedUpdate();
      // gSystem->Sleep(1000);
      // }
      // hProj->Print();
      // hProj->Draw("colz text");
      current->SetProjection(hProj);
      //////// End of projection histogram ////////

      std::map<int, std::vector<int>> rangesTmp     = ranges;
      std::map<int, std::vector<int>> rangesBaseTmp = rangesBase;
      for (auto & kv : rangesBaseTmp) {
        std::vector<int> range = rangesTmp[kv.first];
        NLogTrace("NGnNavigator::Reshape: [L%d]   Axis %d[%s]: rangeBase=%s range=%s", level, kv.first,
                                  hnsIn->GetAxis(kv.first)->GetName(), NUtils::GetCoordsString(kv.second).c_str(),
                                  NUtils::GetCoordsString(range).c_str());
      }
      int minBase = 0, maxBase = 0;
      int i = 0;
      for (auto & c : coords) {
        // NLogDebug("Coordinate: %d v=%d axis=%d", i, coords[i], axes[i]);
        NUtils::GetAxisRangeInBase(hnsIn->GetAxis(axesIds[i]), c, c, binningDef->GetBinning()->GetAxes()[axesIds[i]],
                                                   minBase, maxBase);
        NLogTrace("NGnNavigator::Reshape: Axis %d: minBase=%d maxBase=%d", axesIds[i], minBase, maxBase);
        rangesTmp[axesIds[i]]     = {c, c};             // Set the range for the first axis
        rangesBaseTmp[axesIds[i]] = {minBase, maxBase}; // Set the range for the first axis
        i++;
      }

      if (hProj == nullptr) {
        NLogError("NGnNavigator::Reshape: Projection histogram is null for level %d !!!", level);
        return;
      }

      int nCells = hProj->GetNcells();

      // NGnNavigator * o = fParent->GetChild(indexInProj);
      NGnNavigator * currentChild = current->GetChild(indexInProj);
      if (currentChild == nullptr) {
        NLogTrace("NGnNavigator::Reshape: [L%d] Creating new child for index %d nCells=%d ...", level, indexInProj,
                                  nCells);
        std::string childName = TString::Format("%s_L%d_C%d", GetName(), level + 1, indexInProj).Data();
        currentChild          = new NGnNavigator(childName.c_str(), childName.c_str());
        currentChild->SetLevel(level + 1);
        currentChild->SetNLevels(levels.size());
        currentChild->SetParent(current);
        currentChild->SetGnTree(fGnTree);
        // o = new NGnNavigator(hns->GetListOfAxes());
        // if (fParent->GetChildren().size() != nCells) fParent->SetChildrenSize(nCells);
        // fParent->SetChild(o, indexInProj); // Set the child at the index
        // fParent = o;
        // INFO: it was moved down
        // if (current->GetChildren().size() != nCells) current->SetChildrenSize(nCells);
        // current->SetChild(currentChild, indexInProj); // Set the child at the index
        // currentChild->Print();
      }
      else {
        NLogError("NGnNavigator::Reshape: [L%d] Using existing child for index %d [NOT OK] ...", level, indexInProj);
      }

      // currentChild->Print();
      // return;

      if (level == levels.size() - 1) {
        NLogTrace("NGnNavigator::Reshape: [L%d] Filling projections from all branches %s for ranges:", level,
                                  NUtils::GetCoordsString(levels[level]).c_str());
        for (auto & kv : rangesBaseTmp) {
          std::vector<int> range = rangesTmp[kv.first];
          NLogTrace("NGnNavigator::Reshape: [L%d]   Axis %d ['%s']: rangeBase=%s range=%s", level, kv.first,
                                    binningDef->GetContent()->GetAxis(kv.first)->GetName(), NUtils::GetCoordsString(kv.second).c_str(),
                                    NUtils::GetCoordsString(range).c_str());
          // rangesTmp[kv.first] = range;
        }
        // Get projectiosn histograms from all branches

        Int_t *  cCoords = new Int_t[hns->GetNdimensions()];
        Long64_t linBin  = 0;

        // hns->Print("all");
        NUtils::SetAxisRanges(hnsIn, rangesTmp); // Set the ranges for the axes
        std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsIn->CreateIter(true /*use axis range*/)};
        std::vector<int>                                linBins;

        // loop over all bins in the sparse

        while ((linBin = iter->Next()) >= 0) {
          NLogTrace("NGnNavigator::Reshape: [L%d]   Found bin %lld", level, linBin);
          linBins.push_back(linBin);
        }
        if (linBins.empty()) {
          NLogTrace("NGnNavigator::Reshape: [L%d] No bins found for the given ranges, skipping ...", level);
          // continue;
          return; // No bins found, nothing to process
        }
        //     // bool skipBin = false; // Skip bin if no bins are found
        NLogTrace("NGnNavigator::Reshape: Branch object Point coordinates: %s",
                                  NUtils::GetCoordsString(linBins, -1).c_str());
        current->SetNCells(nCells);

        for (int lb : linBins) {
          fGnTree->GetEntry(lb);
          for (auto & [key, val] : fGnTree->GetStorageTree()->GetBranchesMap()) {
            if (val.GetBranchStatus() == 0) {
              NLogTrace("NGnNavigator::Reshape: [L%d] Branch '%s' is disabled, skipping ...", level, key.c_str());
              continue; // Skip disabled branches
            }
            NLogTrace("NGnNavigator::Reshape: [L%d] Processing branch '%s' with %zu objects to loop ...", level,
                                      key.c_str(), linBins.size());

            // if (obj->GetParent()->GetObjectContentMap()[key].size() != nCells)
            //   obj->GetParent()->ResizeObjectContentMap(key, nCells);

            //           if (skipBin) continue; // Skip processing if the previous bin was skipped

            ///
            /// Handle THnSparse branch object
            ///
            // obj->GetParent()->SetNCells(nCells);
            // fParent->SetNCells(nCells);
            TString className = val.GetObjectClassName();
            if (className.BeginsWith("THnSparse")) {
              // // if (obj->GetParent()->GetObjectContentMap()[key].size() != nCells)
              // //   obj->GetParent()->ResizeObjectContentMap(key, nCells);
              // // TODO: Make it configurable
              // int projectionAxis = 0;
              //
              // TH1 * hProjTmp = nullptr;
              // // loop over all linBins
              // for (int lb : linBins) {
              //   fGnTree->GetEntry(lb);
              //   if (hProjTmp == nullptr) {
              //     hProjTmp = ProjectionFromObject(key, projectionAxis, rangesBaseTmp);
              //     // NLogDebug("AAAA %.0f", hProjTmp ? hProjTmp->GetEntries() : 0);
              //   }
              //   else {
              //     TH1 * temp = ProjectionFromObject(key, projectionAxis, rangesBaseTmp);
              //     // NLogDebug("BBBB %.0f", temp ? temp->GetEntries() : 0);
              //     if (temp) {
              //       hProjTmp->Add(temp);
              //       delete temp; // Delete the temporary histogram to avoid memory leaks
              //     }
              //   }
              // }
              // if (!hProjTmp) {
              //   // skipBin = true; // Skip bin if no histogram is created
              //   continue;
              // }
              // // generate histogram title from axis base ranges
              // std::string title = "Projection of " + key + " ";
              // for (const auto & [axis, range] : rangesBaseTmp) {
              //   TAxis * a = fBinning->GetAxes()[axis];
              //
              //   title +=
              //       TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(range[0]),
              //       a->GetBinUpEdge(range[1]));
              // }
              // hProjTmp->SetTitle(title.c_str());
              // NLogTrace("[L%d] Projection histogram '%s' for branch '%s' storing with indexInProj=%d,
              //                entries = % .0f ",
              //                          level,
              //                hProjTmp->GetTitle(), key.c_str(), indexInProj, hProjTmp->GetEntries());
              // obj->GetParent()->SetObject(key, hProjTmp, indexInProj);
              // // hProjTmp->Draw();
              // // gPad->Update();
              // // gPad->Modified();
              // // gSystem->ProcessEvents();
              // // TODO: We may to set entries from projection histogram to the bin content of mapping file
            }
            else if (className.BeginsWith("TList")) {
              NLogTrace("[L%d] Branch '%s' is a TList, getting object at index %d ...", level, key.c_str(),
                                        indexInProj);
              TList * list = dynamic_cast<TList *>(val.GetObject());
              // list->Print();
              // get list of object names
              std::vector<std::string> objNames;
              for (int i = 0; i < list->GetEntries(); i++) {
                TObject * o = list->At(i);
                objNames.push_back(o->GetName());
              }
              // remove "results" histogram
              // objNames.erase(std::remove(objNames.begin(), objNames.end(), "_params"), objNames.end());

              NLogTrace("[L%d] Branch '%s' TList contains %d objects: %s", level, key.c_str(), list->GetEntries(),
                                        NUtils::GetCoordsString(objNames).c_str());

              // std::vector<std::string> possibleNames = {"hPeak", "hBgNorm", "unlikepm_proj_0"};

              // current->SetObjectNames(objNames);
              bool isValid = true;
              for (auto & name : objNames) {
                TH1 * hProjTmp = dynamic_cast<TH1 *>(list->FindObject(name.c_str()));
                if (hProjTmp == nullptr) {
                  NLogWarning("NGnNavigator::Reshape: Branch '%s' TList does not contain '%s' !!!", key.c_str(),
                                              name.c_str());
                  isValid = false;

                  continue;
                }
                if (TMath::IsNaN(hProjTmp->GetEntries()) || TMath::IsNaN(hProjTmp->GetSumOfWeights())) {
                  NLogWarning("NGnNavigator::Reshape: Branch '%s' '%s' histogram is nan !!!", key.c_str(),

                                              name.c_str());
                  isValid = false;
                  continue;
                }
                NLogTrace(
                    "[L%d] Histogram name='%s' title='%s' for branch '%s' storing with indexInProj=%d, entries=%.0f",
                    level, name.c_str(), hProjTmp->GetTitle(), key.c_str(), indexInProj, hProjTmp->GetEntries());
                // if (obj->GetParent()->GetObjectContentMap()[name].size() != nCells)
                //   obj->GetParent()->ResizeObjectContentMap(name, nCells);
                // // obj->GetParent()->SetObject(key, hProjTmp, indexInProj);
                // obj->GetParent()->SetObject(name, hProjTmp, indexInProj);
                // if (fParent->GetObjectContentMap()[name].size() != nCells) fParent->ResizeObjectContentMap(name,
                // nCells); fParent->SetObject(name, hProjTmp, indexInProj);
                if (current->GetObjectContentMap()[name].size() != nCells)
                  current->ResizeObjectContentMap(name, nCells);
                current->SetObject(name, hProjTmp, indexInProj);
              }
              if (isValid == false) {
                NLogWarning("NGnNavigator::Reshape: Branch '%s' TList does not contain any valid histograms !!!",
                                            key.c_str());
                continue;
              }
            }
            else if (className.BeginsWith("Ndmspc::NParameters")) {

              NParameters * parameters = dynamic_cast<NParameters *>(val.GetObject());
              if (parameters) {
                TH1 * hParams = parameters->GetHisto();
                if (hParams) {
                  // hParams->Print("all");
                  NLogTrace("[L%d] Branch '%s' Point contains '_params' histogram with %.0f entries ...", level,
                                            key.c_str(), hParams->GetEntries());
                  // loop over bin labels
                  for (int b = 1; b <= hParams->GetNbinsX(); b++) {

                    std::string binLabel = hParams->GetXaxis()->GetBinLabel(b);
                    double      binValue = hParams->GetBinContent(b);
                    double      binError = hParams->GetBinError(b);
                    NLogTrace("[L%d]   Bin %d[%s] = %e indexInProj=%d", level, b, binLabel.c_str(), binValue,
                                              indexInProj);
                    // // check if binlabel is "mass"
                    // if (binLabel.compare("mass") == 0) {
                    //   NLogInfo("[L%d]   Checking bin 'mass' = %f ...", level, binValue);
                    //   if (binValue < 1.015 || binValue > 1.025) {
                    //     NLogInfo("[L%d]   Skipping bin 'mass' with value %f ...", level, binValue);
                    //     continue;
                    //   }
                    // }
                    // obj->GetParent()->SetParameter(binLabel, binValue, indexInProj);
                    // fParent->SetParameter(binLabel, binValue, indexInProj);
                    current->SetParameter(binLabel, binValue, indexInProj);
                    current->SetParameterError(binLabel, binError, indexInProj);
                    NLogTrace("[L%d]   Stored parameter '%s' = %e +/- %e at indexInProj=%d", level, binLabel.c_str(),
                                              binValue, binError, indexInProj);
                  }
                }
              }
              else {
                NLogWarning("NGnNavigator::Reshape: Branch '%s' Point parameters object is null !!!", key.c_str());
              }
            }
            else {
              NLogWarning("NGnNavigator::Reshape: Branch '%s' has unsupported class '%s' !!! Skipping ...", key.c_str(),
                                          className.Data());
            }
          }
        }

        // Reshape(binningDef, levels, level + 1, rangesTmp, rangesBaseTmp, currentChild);
        // return;
      }
      if (current->GetChildren().size() != nCells) current->SetChildrenSize(nCells);
      current->SetChild(currentChild, indexInProj); // Set the child at the index
      // execute next child
      Reshape(binningDef, levels, level + 1, rangesTmp, rangesBaseTmp, currentChild);
    };
    executorBin.Execute(loop_task_bin);
  }
  else {
    NLogTrace("NGnNavigator::Reshape: Reached the end of levels, level=%d", level);
    return current;
  }

  NLogTrace("NGnNavigator::Reshape: =========== Reshaping navigator for level %d DONE ================", level);

  if (level == 0) {
    NLogInfo("NGnNavigator::Reshape: Reshaping navigator DONE.");
    // print exported axes from indexes from levels
    for (size_t l = 0; l < levels.size(); l++) {
      std::string axesStr = "";
      for (auto & a : levels[l]) {
        TAxis * axis = binningDef->GetContent()->GetAxis(a);
        axesStr += TString::Format("%d('%s') ", a, axis->GetName()).Data();
      }
      NLogInfo("  Level %zu axes: %s", l, axesStr.c_str());
    }
  }

  // current->Print("");

  return current;
}

void NGnNavigator::Export(const std::string & filename, std::vector<std::string> objectNames, const std::string & wsUrl,
                          int timeoutMs)
{
  ///
  /// Export object to file
  ///
  NLogInfo("Exporting NGnNavigator to file: %s", filename.c_str());

  json objJson;

  // if filename ends with .root, remove it
  if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".root") {
    NLogInfo("Exporting NGnNavigator to ROOT file: %s", filename.c_str());
    TFile * file = TFile::Open(filename.c_str(), "RECREATE");
    if (!file || file->IsZombie()) {
      NLogError("Failed to open file: %s", filename.c_str());
      return;
    }
    file->cd();
    this->Write();
    file->Close();
    delete file;
  }
  else if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".json") {
    NLogInfo("Exporting NGnNavigator to JSON file: %s", filename.c_str());
    NGnNavigator * obj = const_cast<NGnNavigator *>(this);
    ExportToJson(objJson, obj, objectNames);
    // std::cout << objJson.dump(2) << std::endl;
    bool rc = NUtils::SaveRawFile(filename, objJson.dump());
    if (rc == false) {
      NLogError("Failed to save JSON file: %s", filename.c_str());
      return;
    }
  }
  else {
    NLogError("Unsupported file format for export: %s", filename.c_str());
    return;
  }
  if (!wsUrl.empty()) {
    NLogInfo("Uploading exported file to web socket: %s", wsUrl.c_str());
    // NUtils::UploadFileToWebService(filename, wsUrl);

    std::string       message = objJson.dump();
    Ndmspc::NWsClient client;
    if (!message.empty()) {
      NLogInfo("Connecting to web socket: %s", wsUrl.c_str());
      if (!client.Connect(wsUrl)) {
        NLogError("Failed to connect to '%s' !!!", wsUrl.c_str());
      }
      else {

        if (!client.Send(objJson.dump())) {
          NLogError("Failed to send message `%s`", message.c_str());
        }
        else {
          NLogInfo("Successfully sent message to '%s'", wsUrl.c_str());
        }
      }
      if (timeoutMs > 0) {
        NLogInfo("Waiting %d ms before disconnecting ...", timeoutMs);
        gSystem->Sleep(timeoutMs); // wait for a while to ensure message is sent
      }
      NLogInfo("Disconnecting from '%s' ...", wsUrl.c_str());
      client.Disconnect();
    }

    NLogInfo("Sent: %s", message.c_str());
  }

  NLogInfo("Exported NGnNavigator to file: %s", filename.c_str());
}

void NGnNavigator::ExportToJson(json & j, NGnNavigator * obj, std::vector<std::string> objectNames)
{
  ///
  /// Export NGnNavigator to JSON object
  ///

  if (obj == nullptr) {
    NLogError("NGnNavigator::ExportJson: Object is nullptr !!!");
    return;
  }

  // THnSparse * hns = obj->GetProjection();
  // if (hns == nullptr) {
  //   // NLogError("NGnNavigator::ExportJson: HnSparse is nullptr !!!");
  //   return;
  // }

  // std::string name        = hns->GetName();
  // std::string title       = hns->GetTitle();
  // int         nDimensions = hns->GetNdimensions();
  // NLogDebug("ExportJson : Exporting '%s' [%dD] (might take some time) ...", title.c_str(), nDimensions);

  if (obj->GetChildren().empty()) {
    return;
  }

  TH1 * h = obj->GetProjection();
  // if (nDimensions == 1) {
  //   TAxis * xAxis = hns->GetAxis(0);
  //   TH1 *   temp  = hns->Projection(0);
  //   h             = new TH2D(name.c_str(), title.c_str(), xAxis->GetNbins(), xAxis->GetXmin(), xAxis->GetXmax(), 1,
  //   0,
  //                            1); // Create a dummy 2D histogram for 1D projection
  //   h->GetXaxis()->SetName(xAxis->GetName());
  //   h->GetXaxis()->SetTitle(xAxis->GetTitle());
  //   for (int i = 1; i <= temp->GetNbinsX(); ++i) {
  //     h->SetBinContent(i, 1, temp->GetBinContent(i));
  //     h->SetBinError(i, 1, temp->GetBinError(i));
  //   }
  //   obj->SetProjection(h);
  //
  //   h = hns->Projection(0);
  //
  //   // obj->SetProjection(h);
  // }
  // else if (nDimensions == 2) {
  //   h = hns->Projection(1, 0);
  //   obj->SetProjection(h);
  // }
  // else if (nDimensions == 3) {
  //   h = hns->Projection(0, 1, 2);
  //   obj->SetProjection(h);
  // }
  // else {
  //   NLogError("NGnNavigator::ExportJson: Unsupported number of dimensions: %d", nDimensions);
  //   return;
  // }

  if (h == nullptr) {
    NLogError("NGnNavigator::ExportJson: Projection is nullptr !!!");
    return;
  }

  // h->SetNameTitle(name.c_str(), title.c_str());
  h->SetMinimum(0);
  // After the projection, force update statistics

  h->SetStats(kFALSE); // Turn off stats box for clarity
  // h->SetDirectory(nullptr); // Avoid ROOT trying to save the histogram in a file

  j = json::parse(TBufferJSON::ConvertToJSON(h).Data());
  // loop over content map and add objects
  double entries = 0.0;
  // int    idx     = 0;
  if (objectNames.empty()) {
    // NLogDebug("NGnNavigator::ExportJson: Exporting all objects ...");
    // loop over all keys and add them to objectNames
    bool isValid = false;
    for (const auto & [key, val] : obj->GetObjectContentMap()) {
      isValid = false;
      for (size_t i = 0; i < val.size(); i++) {
        TObject * objContent = val[i];
        // NLogDebug("NGnNavigator::ExportJson: Processing object '%s' at index %zu ...", key.c_str(), i);
        if (objContent) {
          // check if object type is inherited from list of names in objectTypes
          std::string className = objContent ? objContent->ClassName() : "";
          if (className.empty()) {
            NLogWarning("NGnNavigator::ExportJson: Object %s has empty class name", key.c_str());
            continue;
          }
          // shrink className string to 3 characters if it is longer than 3
          className = className.substr(0, 3);
          // NLogDebug("NGnNavigator::ExportJson: Object %s has class '%s'", key.c_str(), className.c_str());
          if (std::find(NGnNavigator::fObjectTypes.begin(), NGnNavigator::fObjectTypes.end(), className) !=
              NGnNavigator::fObjectTypes.end()) {
            // NLogWarning(
            //     "NGnNavigator::ExportJson: Skipping unsupported object type '%s' for object '%s' at index %zu",
            //     className.c_str(), key.c_str(), i);
            isValid = true;
            break;
          }
        }
      }
      if (isValid) objectNames.push_back(key);
    }
  }
  else {
    NLogDebug("NGnNavigator::ExportJson: Exporting selected objects: %s", NUtils::GetCoordsString(objectNames).c_str());
  }

  // Print all included object names
  for (const auto & name : objectNames) {
    NLogTrace("NGnNavigator::ExportJson: Included object name: '%s'", name.c_str());
  }

  for (const auto & [key, val] : obj->GetObjectContentMap()) {

    // Filter by objectNames
    if (std::find(objectNames.begin(), objectNames.end(), key) == objectNames.end()) {
      NLogDebug("NGnNavigator::ExportJson: Skipping object '%s' ...", key.c_str());
      continue;
    }

    double min = std::numeric_limits<double>::max();  // Initialize with largest possible double
    double max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
    entries    = 0.0;                                 // Reset entries for each key

    for (size_t i = 0; i < val.size(); i++) {
      TObject * objContent = val[i];
      // if (objContent) objContent->Print();

      json objJson = json::parse(TBufferJSON::ConvertToJSON(objContent).Data());

      if (objContent) {
        double objMin, objMax;
        NUtils::GetTrueHistogramMinMax((TH1 *)objContent, objMin, objMax, false);
        // NLogDebug("NGnNavigator::ExportJson: Object %s has min=%f, max=%f", objContent->GetName(), objMin,
        //                objMax);

        min     = TMath::Min(min, objMin);
        max     = TMath::Max(max, objMax);
        entries = ((TH1 *)objContent)->GetEntries();
        // NLogDebug("NGnNavigator::ExportJson: Adding object %s with min=%f, max=%f", objContent->GetName(),
        // min,
        //                max);
        // j["fArray"][i] = entries / val.size(); // Store the average entries for this object
        j["fArray"][i] = entries;
        // j["fArrays"]["mean"]["values"][i]   = (double)((TH1 *)objContent)->GetMean();
        // j["fArrays"]["stddev"]["values"][i] = (double)((TH1 *)objContent)->GetStdDev();
      }
      else {
        entries = 0.0;
        // j["fArrays"]["mean"]["values"][i]   = 0.0;
        // j["fArrays"]["stddev"]["values"][i] = 0.0;
      }
      // j["fArrays"]["mean"]["min"]       = 1.00;
      // j["fArrays"]["mean"]["max"]       = 2.00;
      // j["fArrays"]["mean"]["outside"]   = false;
      // j["fArrays"]["stddev"]["min"]     = 0.030;
      // j["fArrays"]["stddev"]["max"]     = 0.032;
      // j["fArrays"]["stddev"]["outside"] = true;

      if (entries > 0) {
        j["children"][key].push_back(objJson);
      }
      else {
        j["children"][key].push_back(nullptr);
      }
    }
    j["ndmspc"][key]["fMinimum"] = min;
    j["ndmspc"][key]["fMaximum"] = max;
    // j["ndmspc"][key]["fEntries"] = entries;
    // NLogDebug("NGnNavigator::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
    // idx++;
  }

  for (auto & [key, val] : obj->GetParameterContentMap()) {
    double min = std::numeric_limits<double>::max();  // Initialize with largest possible double
    double max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
    entries    = 0.0;                                 // Reset entries for each key

    for (size_t i = 0; i < val.size(); i++) {
      double param = val[i];
      if (!std::isnan(param) && std::fabs(param) > 1e-12) {
        min                            = TMath::Min(min, param);
        max                            = TMath::Max(max, param);
        j["fArrays"][key]["values"][i] = param;
        // NLogDebug("NGnNavigator::ExportJson: Adding parameter %s with value=%f", key.c_str(), param);
        // entries += 1.0;
      }
      else {
        j["fArrays"][key]["values"][i] = 0.0;
      }
    }

    if (key.compare("mass") == 0) {
      // for chi2, ndf and pvalue set min and max to 0 and 1
      min = 1.018;
      max = 1.023;
    }
    else {

      j["fArrays"][key]["min"] = min;
      j["fArrays"][key]["max"] = max;
    }
    // j["ndmspc"][key]["fEntries"] = entries;
    // NLogDebug("NGnNavigator::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
  }

  double              min = std::numeric_limits<double>::max();  // Initialize with largest possible double
  double              max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
  std::vector<double> tmpContent;
  for (const auto & child : obj->GetChildren()) {
    // if (child == nullptr) {
    //   NLogError("NGnNavigator::ExportJson: Child is nullptr !!!");
    //   continue;
    // }
    json   childJson;
    TH1 *  childProjection = nullptr;
    double objMin, objMax;
    double entries = 0.0; // Reset entries for each child
    if (child != nullptr) {
      ExportToJson(childJson, child, objectNames);

      childProjection = (TH1 *)TBufferJSON::ConvertFromJSON(childJson.dump().c_str());
      if (childProjection) {
        // childProjection->Draw("colz text");
        NUtils::GetTrueHistogramMinMax((TH1 *)childProjection, objMin, objMax, false);
        // min = TMath::Min(min, objMin);
        min     = 0;
        max     = TMath::Max(max, objMax);
        entries = childProjection->GetEntries();
        NLogTrace("NGnNavigator::ExportJson: Child %s has min=%f, max=%f", childProjection->GetName(), objMin, objMax);
      }
    }
    j["children"]["content"].push_back(childJson);
    // j["fArray"][j["children"]["content"].size() - 1] = 5;
  }
  // loop over j["children"]["content"] and remove empty objects"
  bool hasContent = false;
  for (auto & c : j["children"]["content"]) {
    if (c != nullptr) {
      hasContent = true;
      break;
    }
  }

  if (!hasContent) {
    j["children"].erase("content");
    obj->GetChildren().clear(); // Clear children if no content is present
    // j["ndmspc"]["content"]["fMinimum"] = min;
    // j["ndmspc"]["content"]["fMaximum"] = max;
  }
  else {
    j["ndmspc"]["content"]["fMinimum"] = min;
    j["ndmspc"]["content"]["fMaximum"] = max;
  }

  if (obj->GetParent() == nullptr) {
    // NLogDebug("NGnNavigator::ExportJson: LLLLLLLLLLLLLLLLLLLLLLast");
    int i = -1;
    for (const auto & child : j["children"]["content"]) {
      i++;
      if (child == nullptr) {
        // NLogError("NGnNavigator::ExportJson: Child is nullptr !!!");
        j["fArray"][i] = 0; // Store the maximum value for the content
        continue;
      }
      // std::cout << child["fTitle"].dump() << std::endl;
      double min = std::numeric_limits<double>::max();  // Initialize with largest possible double
      double max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
      // loop over all keys in "ndmspc"
      for (auto & [key, value] : child["ndmspc"].items()) {
        if (value.is_object()) {
          // min = TMath::Min(min, value["fMinimum"].get<double>());
          min = 0;
          max = TMath::Max(max, value["fMaximum"].get<double>());
        }
      }
      j["fArray"][i] = max; // Store the maximum value for the content
    }
    if (j["children"]["content"].is_null()) j["children"].erase("content");
  }
}

void NGnNavigator::Print(Option_t * option) const
{
  ///
  /// Print NGnNavigator information
  ///
  TString opt = option;
  opt.ToUpper();

  if (opt.Contains("A") && fGnTree) fGnTree->Print(option);
  if (fProjection) {
    NLogInfo("NGnNavigator: name='%s' title='%s' level=%d levels=%d projection='%s' title='%s'", GetName(), GetTitle(),
             fLevel, fNLevels, fProjection->GetName(), fProjection->GetTitle());
    // fProjection->Print(option);
  }
  else {
    NLogInfo("NGnNavigator: name='%s' title='%s' level=%d levels=%d projection=nullptr", GetName(), GetTitle(), fLevel,
             fNLevels);
  }

  // Print only list of parameters
  NLogInfo("NGnNavigator: Parameters : %s", NUtils::GetCoordsString(GetParameterNames()).c_str());
  NLogInfo("NGnNavigator: Objects    : %s", NUtils::GetCoordsString(GetObjectNames()).c_str());

  // print children
  std::vector<int> childIndices;
  for (int i = 0; i < fChildren.size(); i++) {
    NGnNavigator * child = fChildren[i];
    if (child) {
      childIndices.push_back(i);
      // NLogInfo("NGnNavigator: Child %d/%d:", i + 1, fChildren.size());
      // child->Print(option);
    }
  }
  NLogTrace("NGnNavigator: %zu children with indices: %s", childIndices.size(),
            NUtils::GetCoordsString(childIndices, -1).c_str());
  for (int i = 0; i < fChildren.size(); i++) {
    NGnNavigator * child = fChildren[i];
    if (child && child->GetChildren().empty() == false) {
      child->Print(option);
    }
  }
}

void NGnNavigator::Draw(Option_t * option)
{
  ///
  /// Draw object
  ///
  ///

  // TODO: Handle if size od levels is greater than 2 (since ROOT cannot hover more than 2D histograms)
  TH1 * proj = GetProjection();
  for (int level = 0; level < fNLevels; level++) {
    NLogDebug("NGnNavigator::Draw: Level %d/%d", level + 1, fNLevels);
    if (fProjection->GetDimension() > 2) {
      NLogWarning("NGnNavigator::Draw: Level %d has projection with dimension %d > 2, which is not supported for "
                  "hover/click !!!",
                  level + 1, fProjection->GetDimension());
      return;
    }
    // proj = fChildren[0]->GetProjection();
  }

  // if (fGnTree == nullptr) {
  //   NLogError("NGnNavigator::Draw: NGnTree is nullptr !!!");
  //   return;
  // }
  //
  TString opt = option;
  opt.ToUpper();
  if (opt.Contains("HOVER")) {
    fTrigger = kMouseMotion;
  }
  if (opt.Contains("CLICK")) {
    fTrigger = kButton1Down;
  }

  // std::string name;
  if (!gPad) {
    // NLogInfo("NGnNavigator::Draw: Making default canvas ...");
    gROOT->MakeDefCanvas();
    // if (!gPad->IsEditable()) return;
    if (fNLevels > 1) {
      Int_t cy = TMath::Sqrt(fNLevels);
      Int_t cx = TMath::Ceil(fNLevels / (Double_t)cy);
      gPad->Divide(cy, cx); // Divide the pad into a grid based on the number of levels
    }
  }
  TVirtualPad *  originalPad = gPad; // Save the original pad
  NGnNavigator * obj         = nullptr;
  for (int level = 0; level < fNLevels; level++) {
    NLogDebug("NGnNavigator::Draw: Drawing level %d", level);
    TVirtualPad * pad = originalPad->cd(level + 1);
    if (pad) {
      NLogDebug("NGnNavigator::Draw: Clearing pad %d", level + 1);
      pad->Clear();
      gPad = pad; // Set the current pad to the level + 1 pad
      if (level == 0) {
        obj = this; // For the first level, use the current object
        NLogDebug("NGnNavigator::Draw: Using current object at level %d: %s", level, obj->GetName());
      }
      else {

        // obj = nullptr; // Reset the object for the next level
        for (int i = 0; i < obj->GetChildren().size(); i++) {
          NGnNavigator * child = obj->GetChild(i);
          if (child) {
            NLogDebug("NGnNavigator::Draw: Found child at level %d: %s", level, child->GetProjection()->GetTitle());
            obj = child; // Get the child object at the current level
            break;
          }
        }
        NLogDebug("NGnNavigator::Draw: Using child object at level %d: %s", level, obj ? obj->GetName() : "nullptr");
      }
      if (obj == nullptr) {
        NLogError("NGnNavigator::Draw: Child object at level %d is nullptr !!!", level);
        continue; // Skip to the next level if the child is nullptr
      }
      TH1 * projection = obj->GetProjection();
      if (projection) {
        NLogDebug("NGnNavigator::Draw: Drawing projection at level %d: %s", level, projection->GetTitle());
        projection->SetMinimum(0);
        projection->SetStats(kFALSE); // Turn off stats box for clarity
        // gPad->cd();                   // Change to the current pad
        // projection->Draw(option); // Draw the projection histogram
        // obj->Draw(option);      // Draw the object at the current level
        obj->AppendPad(option); // Append the pad to the current pad stack
      }
      else {
        NLogError("NGnNavigator::Draw: Projection at level %d is nullptr !!!", level);
      }
    }
    else {
      NLogError("NGnNavigator::Draw: Pad %d is nullptr !!!", level + 1);
    }
  }
  gPad = originalPad; // Restore the original pad
}

void NGnNavigator::Paint(Option_t * option)
{
  ///
  /// Paint object
  ///
  NLogInfo("NGnNavigator::Paint: Painting object ...");
  if (fProjection) {
    NLogDebug("NGnNavigator::Paint: Painting to pad=%d projection name=%s title=%s ...", fLevel + 1,
              fProjection->GetName(), fProjection->GetTitle());
    // fProjection->Paint(option);
    fProjection->Paint("colz text");
  }
}

Int_t NGnNavigator::DistancetoPrimitive(Int_t px, Int_t py)
{
  ///
  /// This method tells the pad if the mouse is "on" our object.
  ///

  if (!fProjection) return 9999; // Not drawn, so we are infinitely far.

  return fProjection->DistancetoPrimitive(px, py);
}

void NGnNavigator::ExecuteEvent(Int_t event, Int_t px, Int_t py)
{
  ///
  /// Execute event
  ///
  ///

  // NLogInfo("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d", event, px, py);

  if (!fProjection || !gPad) return;

  // gPad = gPad->GetMother();
  // NLogDebug("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d, gPad=%s title=%s", event, px, py,
  //                gPad->GetName(), gPad->GetTitle());
  // NLogDebug("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d", event, px, py);

  // Step 1: Convert absolute pixel coordinates to the pad's normalized coordinates (0-1 range)
  Double_t x_pad = gPad->AbsPixeltoX(px);
  Double_t y_pad = gPad->AbsPixeltoY(py);

  // Step 2: Convert the pad's normalized coordinates to the user's coordinate system (the histogram axes)
  Double_t x_user = gPad->PadtoX(x_pad);
  Double_t y_user = gPad->PadtoY(y_pad);

  Int_t bin = fProjection->FindBin(x_user, y_user);

  TVirtualPad * originalPad  = gPad; // Save the original pad
  bool          isBinChanged = (bin != fLastHoverBin);
  // --- MOUSE CLICK LOGIC ---
  bool isActionTriggered = (event == fTrigger);

  // trigger action only if hover bin changed or event is click
  isActionTriggered = isActionTriggered && (isBinChanged || event == kButton1Down);

  if (isActionTriggered) {
    Int_t binx, biny, binz;
    fProjection->GetBinXYZ(bin, binx, biny, binz);
    Double_t content = fProjection->GetBinContent(bin);
    NLogDebug("NGnNavigator::ExecuteEvent: [%s] Mouse trigger on bin=[%d, %d] at px=[%f, %f] with content: %f  "
              "level=%d nLevels=%d",
              gPad->GetName(), binx, biny, x_user, y_user, content, fLevel, fNLevels);

    int nDimensions = fGnTree->GetBinning()->GetDefinition()->GetContent()->GetNdimensions();

    Int_t index = fProjection->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx),
                                          fProjection->GetYaxis()->GetBinCenter(biny));
    if (nDimensions == 1) {
      // For 1D histograms, we need to find the index correctly
      index = fProjection->GetXaxis()->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx));
    }
    NLogTrace("NGnNavigator::ExecuteEvent: Index in histogram: %d level=%d", index, fLevel);
    NGnNavigator * child   = GetChild(index);
    TCanvas *      cObject = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("cObject");
    if (child && child->GetProjection()) {
      NLogTrace("NGnNavigator::ExecuteEvent: [%s]Child object '%p' found at index %d", gPad->GetName(),
                child->GetProjection(), index);
      // originalPad->Clear();               // Clear the original pad
      gPad              = originalPad->GetMother(); // Get the mother pad to avoid clearing the current pad
      TVirtualPad * pad = gPad->cd(fLevel + 1 + 1); // Ensure we are in the correct pad
      pad->Clear();                                 // Clear the pad for the child object

      SetLastIndexSelected(index);
      TH1 * hProj = child->GetProjection();
      hProj->SetStats(kFALSE);  // Turn off stats box for clarity
      hProj->SetMinimum(0);     // Set minimum to 0 for better visibility
      hProj->Draw("text colz"); // Draw the projection histogram of the child
      child->AppendPad();
      NLogTrace("NGnNavigator::ExecuteEvent: %d", child->GetLastIndexSelected());
      if (cObject) {
        cObject->Clear(); // Clear the existing canvas if it exists
        cObject->cd();    // Set the current canvas to cObject
                          // Add text with note about the projection

        TLatex latex;
        latex.SetNDC();
        latex.SetTextAlign(22); // center
        latex.SetTextSize(0.05);
        latex.DrawLatex(0.5, 0.5, "Select bottom pad to see projection");
        // delete cObject; // Delete the existing canvas if it exists
      }
    }
    else {

      // TH1 * projection = child->GetProjection();
      // index            = projection->GetXaxis()->FindFixBin(projection->GetXaxis()->GetBinCenter(binx));
      NLogTrace("NGnNavigator::ExecuteEvent: No child object found at index %d", index);
      std::string objName = fObjectNames.empty() ? "resource_monitor" : fObjectNames[0];
      TH1 *       hProj   = (TH1 *)GetObject(objName, index);
      if (hProj == nullptr) {
        NLogError("NGnNavigator::ExecuteEvent: No histogram found for index %d", index);
        return;
      }
      // hProj->Print("all");
      if (cObject == nullptr) {
        cObject = new TCanvas("cObject", "cObject", 800, 600);
      }
      cObject->cd();
      // gPad = cObject->GetPad(0); // Get the current pad
      // hProj->SetTitle(Form("Projection of %s at index %d", fProjection->GetName(), index));
      if (hProj->GetXaxis()->IsAlphanumeric()) {
        TPaveText * pt = new TPaveText(0.15, 0.15, 0.85, 0.85);
        for (Int_t binx = 1; binx <= hProj->GetNbinsX(); ++binx) {
          std::string name  = hProj->GetXaxis()->GetBinLabel(binx);
          std::string value = TString::Format("%.3f", hProj->GetBinContent(binx)).Data();
          std::string t     = TString::Format("%s: %s", name.c_str(), value.c_str()).Data();

          pt->AddText(t.c_str());
        }
        pt->Draw();
      }
      else {
        hProj->Draw(); // Draw the projection histogram
      }
    }
    // else {
    // }
    gPad->ModifiedUpdate(); // Force pad to redraw
  }
  // --- MOUSE HOVER LOGIC ---
  if (event == kMouseMotion) {
    if (isBinChanged) {
      // Check if the cursor is inside a bin with content
      if (fProjection->GetBinContent(bin) > 0) {
        Int_t binx, biny, binz;
        fProjection->GetBinXYZ(bin, binx, biny, binz);
        NLogTrace("[%s] Mouse hover on bin[%d, %d] at px[%f, %f] level=%d nLevels=%d", gPad->GetName(), binx, biny,
                  x_user, y_user, fLevel, fNLevels);
      }
      fLastHoverBin = bin;
      NLogTrace("[%s] Setting point for level %d %s", gPad->GetName(), fLevel, fProjection->GetTitle());
    }
  }
  gPad = originalPad; // Restore the original pad
}

NGnNavigator * NGnNavigator::GetChild(int index) const
{
  ///
  /// Returns child object at given index
  ///
  // NLogDebug("NGnNavigator::GetChild: index=%d, size=%zu", index, fChildren.size());
  return (index >= 0 && index < fChildren.size()) ? fChildren[index] : nullptr;
}
void NGnNavigator::SetChild(NGnNavigator * child, int index)
{
  ///
  /// Set child object at given index
  ///
  if (child) {
    fChildren[index < 0 ? fChildren.size() : index] = child;
    child->SetParent(this);
  }
}

// NGnNavigator * NGnNavigator::Open(TTree * tree, const std::string & branches, TFile * file)
// {
//   ///
//   /// Open NGnNavigator from TTree
//   ///
//
//   NGnTree * ngt = NGnTree::Open(tree, branches, file);
//
//   NGnNavigator * navigator = new NGnNavigator();
//   if (!navigator) {
//     return nullptr;
//   }
//   navigator->SetGnTree(ngt);
//
//   return navigator;
// }
//
// NGnNavigator * NGnNavigator::Open(const std::string & filename, const std::string & branches,
//                                   const std::string & treename)
// {
//   ///
//   /// Open NGnNavigator from file
//   ///
//   NGnTree * ngt = NGnTree::Open(filename, branches, treename);
//
//   NGnNavigator * navigator = new NGnNavigator();
//   if (!navigator) {
//     return nullptr;
//   }
//   navigator->SetGnTree(ngt);
//
//   return navigator;
// }
std::vector<TObject *> NGnNavigator::GetObjects(const std::string & name) const
{
  /// Returns points for given name
  auto it = fObjectContentMap.find(name);
  if (it != fObjectContentMap.end()) {
    return it->second;
  }
  return {};
}

TObject * NGnNavigator::GetObject(const std::string & name, int index) const
{
  /// Returns point for given name and index
  auto it = fObjectContentMap.find(name);
  if (it != fObjectContentMap.end() && index < (int)it->second.size()) {
    return it->second[index];
  }
  return nullptr;
}

void NGnNavigator::SetObject(const std::string & name, TObject * obj, int index)
{
  ///
  /// Set object for given name and index
  ///
  if (obj) {
    if (fObjectContentMap.find(name) == fObjectContentMap.end()) {
      ResizeObjectContentMap(name, fNCells);
    }
    NLogTrace("NGnNavigator::SetObject: name=%s, obj=%p, index=%d", name.c_str(), obj, index,
              fObjectContentMap[name].size());

    // Add object name if missing
    if (std::find(fObjectNames.begin(), fObjectNames.end(), name) == fObjectNames.end()) {
      fObjectNames.push_back(name);
    }

    if (index < 0) {
      fObjectContentMap[name].push_back(obj);
    }
    else {
      // if (index >= (int)fObjectContentMap[name].size()) {
      //   fObjectContentMap[name].resize(index + 1, nullptr);
      // }
      fObjectContentMap[name][index] = obj;
    }
  }
}

std::vector<double> NGnNavigator::GetParameters(const std::string & name) const
{
  /// Returns parameters for given name
  auto it = fParameterContentMap.find(name);
  if (it != fParameterContentMap.end()) {
    return it->second;
  }
  return {};
}

double NGnNavigator::GetParameter(const std::string & name, int index) const
{
  /// Returns parameter for given name and index
  auto it = fParameterContentMap.find(name);
  if (it != fParameterContentMap.end() && index < (int)it->second.size()) {
    return it->second[index];
  }
  return NAN;
}

void NGnNavigator::SetParameter(const std::string & name, double value, int index)
{
  ///
  /// Set parameter for given name and index
  ///
  if (!std::isnan(value)) {
    if (fParameterContentMap.find(name) == fParameterContentMap.end() || fParameterContentMap[name].size() < fNCells) {
      NLogTrace("NGnNavigator::SetParameter: Resizing parameter content map for '%s' to %d", name.c_str(), fNCells);
      ResizeParameterContentMap(name, fNCells);
    }
    NLogTrace("NGnNavigator::SetParameter: name=%s, value=%f, index=%d", name.c_str(), value, index,
              fParameterContentMap[name].size());

    // Append parameter name if missing
    if (std::find(fParameterNames.begin(), fParameterNames.end(), name) == fParameterNames.end()) {
      fParameterNames.push_back(name);
    }

    if (index < 0) {
      fParameterContentMap[name].push_back(value);
    }
    else {
      // if (index >= (int)fParameterContentMap[name].size()) {
      //   fParameterContentMap[name].resize(index + 1, NAN);
      // }
      fParameterContentMap[name][index] = value;
    }
  }
}

std::vector<double> NGnNavigator::GetParameterErrors(const std::string & name) const
{
  /// Returns parameters for given name
  auto it = fParameterErrorContentMap.find(name);
  if (it != fParameterErrorContentMap.end()) {
    return it->second;
  }
  return {};
}

double NGnNavigator::GetParameterError(const std::string & name, int index) const
{
  /// Returns parameter for given name and index
  auto it = fParameterErrorContentMap.find(name);
  if (it != fParameterErrorContentMap.end() && index < (int)it->second.size()) {
    return it->second[index];
  }
  return NAN;
}

void NGnNavigator::SetParameterError(const std::string & name, double value, int index)
{
  ///
  /// Set parameter for given name and index
  ///
  if (!std::isnan(value)) {
    if (fParameterErrorContentMap.find(name) == fParameterErrorContentMap.end() ||
        fParameterErrorContentMap[name].size() < fNCells) {
      NLogTrace("NGnNavigator::SetParameter: Resizing parameter content map for '%s' to %d", name.c_str(), fNCells);
      ResizeParameterErrorContentMap(name, fNCells);
    }
    NLogTrace("NGnNavigator::SetParameter: name=%s, value=%f, index=%d", name.c_str(), value, index,
              fParameterErrorContentMap[name].size());

    // Append parameter name if missing
    if (std::find(fParameterNames.begin(), fParameterNames.end(), name) == fParameterNames.end()) {
      fParameterNames.push_back(name);
    }

    if (index < 0) {
      fParameterErrorContentMap[name].push_back(value);
    }
    else {
      // if (index >= (int)fParameterContentMap[name].size()) {
      //   fParameterContentMap[name].resize(index + 1, NAN);
      // }
      fParameterErrorContentMap[name][index] = value;
    }
  }
}
void NGnNavigator::DrawSpectraAll(std::string parameterName, Option_t * option) const
{
  ///
  /// Draws the NGnProjection object for all projection IDs
  ///
  std::vector<int> projIds;
  DrawSpectra(parameterName, projIds, option);
}

void NGnNavigator::DrawSpectraByName(std::string parameterName, std::vector<std::string> projAxes,
                                     Option_t * option) const
{
  ///
  /// Draws the NGnProjection object for all projection IDs
  ///

  std::vector<int> projIds;
  // lopp over fProjection axes and find the indices of projAxes then fill projIds and remove from projAxes
  for (const auto & axisName : projAxes) {
    TAxis * axis = nullptr;
    for (int i = 0; i < fProjection->GetDimension(); i++) {
      if (i == 0)
        axis = fProjection->GetXaxis();
      else if (i == 1)
        axis = fProjection->GetYaxis();
      else if (i == 2)
        axis = fProjection->GetZaxis();

      if (axis && axis->GetName() == axisName) {
        projIds.push_back(i);
        break;
      }
    }
  }
  if (projIds.empty()) {
    NLogError("NGnNavigator::DrawSpectra: No projection axes found for names: %s",
              NUtils::GetCoordsString(projAxes, -1).c_str());
    return;
  }

  if (projIds.size() > 3) {
    NLogError("NGnNavigator::DrawSpectra: Too many projection dimensions: %zu (max 3)", projIds.size());
    return;
  }

  if (projIds.size() != projAxes.size()) {
    NLogError("NGnNavigator::DrawSpectra: Not all projection axes found: %s",
              NUtils::GetCoordsString(projAxes, -1).c_str());
    return;
  }

  DrawSpectra(parameterName, projIds, option);
}

void NGnNavigator::DrawSpectra(std::string parameterName, std::vector<int> projIds, Option_t * option) const
{
  ///
  /// Draws the NGnProjection object with the specified projection IDs
  ///

  if (parameterName.empty()) {
    NLogError("NGnNavigator::DrawSpectra: Parameter name is empty");
    return;
  }

  // check if parameterName exists in fParameterContentMap
  if (fParameterContentMap.find(parameterName) == fParameterContentMap.end()) {
    NLogError("NGnNavigator::DrawSpectra: Parameter name '%s' not found in fParameterContentMap",
              parameterName.c_str());
    return;
  }
  Int_t screenWidth  = gClient->GetDisplayWidth();
  Int_t screenHeight = gClient->GetDisplayHeight();

  int       padCounter = 0;
  TCanvas * c          = nullptr;
  // Create a canvas that is, for example, 40% of the screen width and height
  constexpr double canvasScale  = 0.4;
  Int_t            canvasWidth  = static_cast<Int_t>(screenWidth * canvasScale);
  Int_t            canvasHeight = static_cast<Int_t>(screenHeight * canvasScale);

  NLogTrace("Screen size: %dx%d", screenWidth, screenHeight);

  NBinningDef * binningDef    = fGnTree->GetBinning()->GetDefinition();
  THnSparse *   hnsObjContent = binningDef->GetContent();
  // hnsObjContent->Print("all");
  std::vector<std::vector<int>> projections;
  if (projIds.empty()) {
    projIds.resize(fProjection->GetDimension());
    std::iota(projIds.begin(), projIds.end(), 0); // Fill projIds with 0, 1, 2, ..., n-1
    projections = Ndmspc::NUtils::Permutations(projIds);
  }
  else {
    projections.push_back(projIds);
  }

  if (projections.empty()) {
    NLogError("NGnNavigator::DrawSpectra: No projections found");
    return;
  }
  if (projections[0].size() > 3) {
    NLogError("NGnNavigator::DrawSpectra: Too many projection dimensions: %zu (max 3)", projections[0].size());
    return;
  }

  for (const auto & proj : projections) {

    NLogTrace("Projection IDs: %s", NUtils::GetCoordsString(projIds, -1).c_str());

    // fProjection->Draw("colz");
    TH1 * hParameterProjection = (TH1 *)fProjection->Clone("hParameterProjection");
    // hParameterProjection->Sumw2(kFALSE);

    // fill hParameterProjection from fParameterContentMap
    hParameterProjection->SetContent(GetParameters(parameterName).data());

    // NLogTrace("NGnNavigator::DrawSpectra: Setting parameter errors for '%s'", parameterName.c_str());
    // Print all errors

    hParameterProjection->SetError(GetParameterErrors(parameterName).data());
    // hParameterProjection->Draw("colz text");
    // return;

    // Create parameter THnSparse
    THnSparse * hsParam = THnSparse::CreateSparse(parameterName.c_str(), parameterName.c_str(), hParameterProjection);
    // append all labels from from fProjection to hs
    int nDimensions = hParameterProjection->GetDimension();
    for (int i = 0; i < nDimensions; i++) {
      TAxis * axis = nullptr;
      if (i == 0)
        axis = hParameterProjection->GetXaxis();
      else if (i == 1)
        axis = hParameterProjection->GetYaxis();
      else if (i == 2)
        axis = hParameterProjection->GetZaxis();

      hsParam->GetAxis(i)->SetName(axis->GetName());
      hsParam->GetAxis(i)->SetTitle(axis->GetTitle());
      // Apply all labels from fProjection to hs
      for (int j = 1; j <= axis->GetNbins(); j++) {
        std::string label = axis->GetBinLabel(j);
        // if (label.empty()) {
        //   double binLowEdge = axis->GetBinLowEdge(j);
        //   double binUpEdge  = axis->GetBinUpEdge(j);
        //   label             = Form("%.3f-%.3f", binLowEdge, binUpEdge);
        // }
        if (!label.empty()) {
          hsParam->GetAxis(i)->SetBinLabel(j, label.c_str());
        }
        // hsParam->SetBinError(j, 1e-10);
      }
    }

    delete hParameterProjection;

    std::vector<int> dims = proj;
    if (dims.size() > 3) {
      NLogError("NGnNavigator::DrawSpectra: Too many projection dimensions: %zu (max 3)", dims.size());
      return;
    }
    std::vector<std::set<int>> dimsResults(3);

    std::vector<std::vector<int>> ranges;
    for (int dim : dims) {
      int nBins = hnsObjContent->GetAxis(dim)->GetNbins();
      ranges.push_back({dim, 1, nBins}); // 1-based indexing for THnSparse
    }

    NUtils::SetAxisRanges(hnsObjContent, ranges);
    Int_t *                                         p      = new Int_t[hnsObjContent->GetNdimensions()];
    Long64_t                                        linBin = 0;
    std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsObjContent->CreateIter(true /*use axis range*/)};
    while ((linBin = iter->Next()) >= 0) {
      // NLogDebug("Linear bin: %lld", linBin);
      Double_t    v         = hnsObjContent->GetBinContent(linBin, p);
      Long64_t    idx       = hnsObjContent->GetBin(p);
      std::string binCoords = NUtils::GetCoordsString(NUtils::ArrayToVector(p, hnsObjContent->GetNdimensions()), -1);
      // NLogInfo("Bin %lld(%lld): %f %s", linBin, idx, v, binCoords.c_str());
      dimsResults[0].insert(p[dims[0]]);
      if (dims.size() > 1) dimsResults[1].insert(p[dims[1]]);
      if (dims.size() > 2) dimsResults[2].insert(p[dims[2]]);
    }

    if (!gROOT->IsBatch()) {
      // Ensure gClient is initialized if not already
      if (!gClient) {
        // This will initialize gClient if needed
        (void)gClient;
      }
    }

    // if (dims.size() <= 2) {
    //   dims.push_back(0); // Add a dummy dimension for 2D plotting
    // }
    int nPads = dims.size() > 2 ? dimsResults[2].size() : 1;
    NLogTrace("Number of pads: %d", nPads);
    std::vector<std::string> projNames;
    projNames.push_back(hsParam->GetAxis(dims[0])->GetName());
    if (dims.size() > 1) projNames.push_back(hsParam->GetAxis(dims[1])->GetName());
    if (dims.size() > 2) projNames.push_back(hsParam->GetAxis(dims[2])->GetName());

    NLogDebug("Drawing %s ...", NUtils::GetCoordsString(projNames, -1).c_str());

    std::string posfix = NUtils::Join(projNames, '-');

    std::string canvasName = Form("c_%s", posfix.c_str());
    NLogTrace("Creating canvas '%s' with size %dx%d", canvasName.c_str(), canvasWidth, canvasHeight);
    c = new TCanvas(canvasName.c_str(), canvasName.c_str(), canvasWidth, canvasHeight);
    c->DivideSquare(nPads);
    for (int iPad = 0; iPad < nPads; iPad++) {

      std::string stackName  = Form("hStack_%s_%d", posfix.c_str(), iPad);
      std::string stackTitle = parameterName + " : ";
      stackTitle += projNames[0];
      stackTitle += projNames.size() > 1 ? " vs " + projNames[1] : "";
      if (dims.size() > 2) {
        p[dims[2]] = iPad + 1; // 1-based index for the third dimension

        // print projIds[2] range
        // NLogDebug("Pad %d: Setting projection indices: %d %d %d", iPad, p[0], p[1], p[2]);
        // NLogDebug("Pad %d: Setting projection indices: %d %d %d", iPad, projIds[0], projIds[1],
        // projIds[2]);

        TAxis * aPad = hsParam->GetAxis(dims[2]);
        if (aPad->IsAlphanumeric()) {
          stackTitle += projNames.size() > 2 ? " for " + projNames[2] + " [" + aPad->GetBinLabel(p[dims[2]]) + "]" : "";
        }
        else {
          double binLowEdge = aPad->GetBinLowEdge(p[dims[2]]);
          double binUpEdge  = aPad->GetBinUpEdge(p[dims[2]]);
          stackTitle +=
              projNames.size() > 2 ? " for " + projNames[2] + " " + Form(" [%.3f,%.3f]", binLowEdge, binUpEdge) : "";
        }
      }
      NLogTrace("Creating stack '%s' with title '%s'", stackName.c_str(), stackTitle.c_str());
      //
      THStack * hStack  = new THStack(stackName.c_str(), stackTitle.c_str());
      int       nStacks = dims.size() > 1 ? dimsResults[1].size() : 1;
      for (int iStack = 0; iStack < nStacks; iStack++) {
        // c->cd(iStack + 1);
        p[dims[0]] = 0;
        if (dims.size() > 1) p[dims[1]] = iStack + 1; // 1-based index for the second dimension
        // if (dims.size() > 2) p[dims[2]] = iPad + 1; // 1-based index for the third dimension
        //
        //
        std::vector<std::vector<int>> ranges;
        if (dims.size() > 2) ranges.push_back({dims[2], p[dims[2]], p[dims[2]]});
        if (dims.size() > 1) ranges.push_back({dims[1], p[dims[1]], p[dims[1]]});

        NUtils::SetAxisRanges(hsParam, ranges, true);

        // NLogTrace("Projecting for stack %d: Setting projection dims: %d %d %d", iStack, dims[0], dims[1], dims[2]);

        TH1 * hProj = NUtils::ProjectTHnSparse(hsParam, {dims[0]}, option);
        hProj->SetMinimum(0);
        TAxis * aStack = hsParam->GetAxis(dims[1]);
        // if (aStack->IsAlphanumeric()) {
        //   std::string label = aStack->GetBinLabel(p[dims[1]]);
        //   hProj->SetTitle(Form("%s [%s]", aStack->GetName(), label.c_str()));
        // }
        // else {
        double binLowEdge = aStack->GetBinLowEdge(p[dims[1]]);
        double binUpEdge  = aStack->GetBinUpEdge(p[dims[1]]);
        hProj->SetTitle(Form("%s [%.3f,%.3f]", aStack->GetName(), binLowEdge, binUpEdge));
        // }

        // hProj->Print();
        hProj->SetMarkerStyle(20);
        hProj->SetMarkerColor(iStack + 1);
        // set all errors to zero
        // for (int iBin = 1; iBin <= hProj->GetNbinsX(); iBin++) {
        //   hProj->SetBinError(iBin, 1e-10);
        // }
        hStack->Add((TH1 *)hProj->Clone());
      }

      c->cd(iPad + 1);
      // hStack->SetMinimum(1.015);
      // hStack->SetMaximum(1.023);
      std::string drawOption = "nostack E";
      drawOption += option;
      hStack->Draw(drawOption.c_str());
      // // hStack->GetXaxis()->SetRangeUser(0.0, 8.0);
      if (dims.size() > 1) gPad->BuildLegend(0.75, 0.75, 0.95, 0.95, "");
      c->ModifiedUpdate();
      gSystem->ProcessEvents();
    }
  }
}

} // namespace Ndmspc
