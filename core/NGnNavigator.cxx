#include <fstream>
#include <iostream>
#include <vector>
#include <TSystem.h>
#include <TMath.h>
#include <TROOT.h>
#include <TVirtualPad.h>
#include <TPad.h>
#include <TCanvas.h>
#include <TLatex.h>
#include <TH1.h>
#include <TBufferJSON.h>

#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NGnTree.h"
#include "NLogger.h"
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
    NLogger::Error("NGnNavigator::Reshape: Binning definition is null !!!");
    return nullptr;
  }

  int              nAxes = 0;
  std::vector<int> axes;
  for (auto & l : levels) {
    nAxes += l.size();
    for (auto & a : l) {
      if (std::find(axes.begin(), axes.end(), a) == axes.end()) {
        axes.push_back(a);
      }
    }
  }
  std::vector<int> axesSorted = axes;
  std::sort(axesSorted.begin(), axesSorted.end());
  std::vector<int> axesVariavble = binningDef->GetVariableAxes();
  std::sort(axesVariavble.begin(), axesVariavble.end());

  // remove all duplicates from axesSorted
  for (size_t i = 1; i < axesSorted.size(); i++) {
    if (axesSorted[i] == axesSorted[i - 1]) {
      axesSorted.erase(axesSorted.begin() + i);
      i--;
    }
  }
  if (axesSorted != axesVariavble) {
    NLogger::Error(
        "NGnNavigator::Reshape: Axes in levels '%s' do not match variable axes in binning definition '%s' !!!",
        NUtils::GetCoordsString(axesSorted, -1).c_str(), NUtils::GetCoordsString(axesVariavble, -1).c_str());
    return nullptr;
  }

  // NLogger::Debug("NGnNavigator::Reshape: Number of axes in levels = %d GetVariableAxes=%zu", nAxes,
  //                binningDef->GetVariableAxes().size());
  // return nullptr;

  if (nAxes != binningDef->GetVariableAxes().size()) {
    NLogger::Error("NGnNavigator::Reshape: Number of axes in levels (%d) does not match number of axes in binning "
                   "definition (%d) !!! Available axes indices: %s",
                   nAxes, binningDef->GetVariableAxes().size(),
                   NUtils::GetCoordsString(binningDef->GetVariableAxes(), -1).c_str());

    return nullptr;
  }

  NLogger::Info("NGnNavigator::Reshape: Reshaping navigator for level %d/%zu with binning '%s' ...", level,
                levels.size(), binningName.c_str());
  return Reshape(binningDef, levels, level, ranges, rangesBase);
}
NGnNavigator * NGnNavigator::Reshape(NBinningDef * binningDef, std::vector<std::vector<int>> levels, int level,
                                     std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase,
                                     NGnNavigator * parent)
{
  ///
  /// Reshape the navigator
  ///

  NLogger::Trace("NGnNavigator::Reshape: Reshaping navigator for level=%d levels=%zu", level, levels.size());
  TH1::AddDirectory(kFALSE);

  fNLevels = levels.size();
  fLevel   = level;
  // NBinningDef * binningDef = fGnTree->GetBinning()->GetDefinition(binningName);

  NGnNavigator * current = parent;
  if (current == nullptr) {
    NLogger::Debug("NGnNavigator::Reshape: Creating root navigator %d/%zu...", level, levels.size());
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
    NLogger::Trace("NGnNavigator::Reshape: levels[%d]=%s...", level, NUtils::GetCoordsString(levels[level]).c_str());

    // Generate projection histogram

    // loop for every bin in the current level

    std::vector<int> minsBin;
    std::vector<int> maxsBin;
    for (auto & idx : levels[level]) {
      NLogger::Trace("NGnNavigator::Reshape: [B%d] Axis %d: %s", level, idx,
                     binningDef->GetContent()->GetAxis(idx)->GetName());
      // int minBase = 0, maxBase = 0;
      // NUtils::GetAxisRangeInBase(GetAxis(idx), 1, GetAxis(idx)->GetNbins(), fBinning->GetAxes()[idx], minBase,
      // maxBase); ranges[idx] = {minBase, maxBase};            // Set the ranges for the axis
      minsBin.push_back(1);                                                  // Get the minimum bin edge);
      maxsBin.push_back(binningDef->GetContent()->GetAxis(idx)->GetNbins()); // Get the maximum bin edge);
      NLogger::Trace("NGnNavigator::Reshape: [B%d] Axis %d: %s bins=[%d,%d]", level, idx,
                     binningDef->GetContent()->GetAxis(idx)->GetName(), minsBin.back(), maxsBin.back());
    }

    NDimensionalExecutor executorBin(minsBin, maxsBin);
    auto                 loop_task_bin = [this, current, binningDef, levels, level, ranges,
                          rangesBase](const std::vector<int> & coords) {
      NLogger::Trace("NGnNavigator::Reshape: [B%d] Processing coordinates: coords=%s levels=%s", level,
                                     NUtils::GetCoordsString(coords, -1).c_str(), NUtils::GetCoordsString(levels[level]).c_str());
      NLogger::Trace("NGnNavigator::Reshape: [L%d] Generating %zuD histogram %s with ranges: %s", level,
                                     levels[level].size(), NUtils::GetCoordsString(levels[level]).c_str(),
                     ranges.size() == 0 ? "[]" : "");

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
        NLogger::Error("NGnNavigator::Reshape: Projection failed for level %d !!!", level);
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
        // NLogger::Debug("XX Axis '%s' range: [%d, %d]", GetAxis(axisId)->GetName(), range[0], range[1]);
        TAxis * a = hnsIn->GetAxis(axisId);
        title += TString::Format("%s[%.2f,%.2f]", a->GetName(), a->GetBinLowEdge(range[0]), a->GetBinUpEdge(range[1]));
      }
      hns->SetTitle(title.c_str());
      // hns->Print();

      // TODO: Handle it via NGnSparseObject
      // if (obj->GetHnSparse() == nullptr) {
      //   NLogger::Debug("NGnNavigator::Reshape: Setting histogram '%s' ...", hns->GetTitle());
      //   obj->SetHnSparse(hns);
      // }

      int   indexInProj = -1;
      TH1 * hProj       = nullptr;
      // if (level < 3) {
      if (nDims == 1) {
        hProj = hnsIn->Projection(axesIds[0]);
        // set name from hnsIn
        hProj->GetXaxis()->SetName(hnsIn->GetAxis(axesIds[0])->GetName());
        // apply lables from hnsIn to hProj
        for (int b = 1; b <= hProj->GetNbinsX(); b++) {
          hProj->GetXaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[0])->GetBinLabel(b));
        }
        indexInProj = hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]));
      }
      else if (nDims == 2) {
        hProj = hnsIn->Projection(axesIds[1], axesIds[0]);
        hProj->GetXaxis()->SetName(hnsIn->GetAxis(axesIds[0])->GetName());
        hProj->GetYaxis()->SetName(hnsIn->GetAxis(axesIds[1])->GetName());
        // apply lables from hnsIn to hProj
        for (int b = 1; b <= hProj->GetNbinsX(); b++) {
          hProj->GetXaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[0])->GetBinLabel(b));
        }

        for (int b = 1; b <= hProj->GetNbinsY(); b++) {
          hProj->GetYaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[1])->GetBinLabel(b));
        }
        indexInProj =
            hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]), hProj->GetYaxis()->GetBinCenter(coords[1]));
      }
      else if (nDims == 3) {
        hProj = hnsIn->Projection(axesIds[0], axesIds[1], axesIds[2]);
        hProj->GetXaxis()->SetName(hnsIn->GetAxis(axesIds[0])->GetName());
        hProj->GetYaxis()->SetName(hnsIn->GetAxis(axesIds[1])->GetName());
        hProj->GetZaxis()->SetName(hnsIn->GetAxis(axesIds[2])->GetName());
        for (int b = 1; b <= hProj->GetNbinsX(); b++) {
          hProj->GetXaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[0])->GetBinLabel(b));
        }

        for (int b = 1; b <= hProj->GetNbinsY(); b++) {
          hProj->GetYaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[1])->GetBinLabel(b));
        }
        for (int b = 1; b <= hProj->GetNbinsZ(); b++) {
          hProj->GetZaxis()->SetBinLabel(b, hnsIn->GetAxis(axesIds[2])->GetBinLabel(b));
        }
        indexInProj =
            hProj->FindFixBin(hProj->GetXaxis()->GetBinCenter(coords[0]), hProj->GetYaxis()->GetBinCenter(coords[1]),
                                              hProj->GetZaxis()->GetBinCenter(coords[2]));
      }
      if (!hProj) {
        NLogger::Error("NGnNavigator::Reshape: Projection failed for level %d !!!", level);
        return;
      }

      hProj->SetName(name.c_str());
      hProj->SetTitle(title.c_str());
      NLogger::Trace("NGnNavigator::Reshape: [L%d] Projection histogram '%s' for coords=%s index=%d", level,
                                     hProj->GetTitle(), NUtils::GetCoordsString(coords, -1).c_str(), indexInProj);
      //
      // hProj->SetMinimum(0);
      // hProj->SetStats(kFALSE);
      // hProj->Draw();
      // gPad->ModifiedUpdate();
      // gSystem->Sleep(1000);
      // }
      // hProj->Print();
      // hProj->Draw();
      current->SetProjection(hProj);
      //////// End of projection histogram ////////

      std::map<int, std::vector<int>> rangesTmp     = ranges;
      std::map<int, std::vector<int>> rangesBaseTmp = rangesBase;
      for (auto & kv : rangesBaseTmp) {
        std::vector<int> range = rangesTmp[kv.first];
        NLogger::Trace("NGnNavigator::Reshape: [L%d]   Axis %d[%s]: rangeBase=%s range=%s", level, kv.first,
                                       hnsIn->GetAxis(kv.first)->GetName(), NUtils::GetCoordsString(kv.second).c_str(),
                                       NUtils::GetCoordsString(range).c_str());
      }
      int minBase = 0, maxBase = 0;
      int i = 0;
      for (auto & c : coords) {
        // NLogger::Debug("Coordinate: %d v=%d axis=%d", i, coords[i], axes[i]);
        NUtils::GetAxisRangeInBase(hnsIn->GetAxis(axesIds[i]), c, c, binningDef->GetBinning()->GetAxes()[axesIds[i]],
                                                   minBase, maxBase);
        NLogger::Trace("NGnNavigator::Reshape: Axis %d: minBase=%d maxBase=%d", axesIds[i], minBase, maxBase);
        rangesTmp[axesIds[i]]     = {c, c};             // Set the range for the first axis
        rangesBaseTmp[axesIds[i]] = {minBase, maxBase}; // Set the range for the first axis
        i++;
      }

      if (hProj == nullptr) {
        NLogger::Error("NGnNavigator::Reshape: Projection histogram is null for level %d !!!", level);
        return;
      }

      int nCells = hProj->GetNcells();

      // NGnNavigator * o = fParent->GetChild(indexInProj);
      NGnNavigator * currentChild = current->GetChild(indexInProj);
      if (currentChild == nullptr) {
        NLogger::Trace("NGnNavigator::Reshape: [L%d] Creating new child for index %d nCells=%d ...", level, indexInProj,
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
        NLogger::Error("NGnNavigator::Reshape: [L%d] Using existing child for index %d [NOT OK] ...", level,
                                       indexInProj);
      }

      // currentChild->Print();
      // return;

      if (level == levels.size() - 1) {
        NLogger::Trace("NGnNavigator::Reshape: [L%d] Filling projections from all branches %s for ranges:", level,
                                       NUtils::GetCoordsString(levels[level]).c_str());
        for (auto & kv : rangesBaseTmp) {
          std::vector<int> range = rangesTmp[kv.first];
          NLogger::Trace("NGnNavigator::Reshape: [L%d]   Axis %d ['%s']: rangeBase=%s range=%s", level, kv.first,
                                         binningDef->GetContent()->GetAxis(kv.first)->GetName(),
                                         NUtils::GetCoordsString(kv.second).c_str(), NUtils::GetCoordsString(range).c_str());
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
          NLogger::Trace("NGnNavigator::Reshape: [L%d]   Found bin %lld", level, linBin);
          linBins.push_back(linBin);
        }
        if (linBins.empty()) {
          NLogger::Trace("NGnNavigator::Reshape: [L%d] No bins found for the given ranges, skipping ...", level);
          // continue;
          return; // No bins found, nothing to process
        }
        //     // bool skipBin = false; // Skip bin if no bins are found
        NLogger::Trace("NGnNavigator::Reshape: Branch object Point coordinates: %s",
                                       NUtils::GetCoordsString(linBins, -1).c_str());
        for (auto & [key, val] : fGnTree->GetStorageTree()->GetBranchesMap()) {
          if (val.GetBranchStatus() == 0) {
            NLogger::Trace("NGnNavigator::Reshape: [L%d] Branch '%s' is disabled, skipping ...", level, key.c_str());
            continue; // Skip disabled branches
          }
          NLogger::Trace("NGnNavigator::Reshape: [L%d] Processing branch '%s' with %zu objects to loop ...", level,
                                         key.c_str(), linBins.size());

          // if (obj->GetParent()->GetObjectContentMap()[key].size() != nCells)
          //   obj->GetParent()->ResizeObjectContentMap(key, nCells);

          //           if (skipBin) continue; // Skip processing if the previous bin was skipped

          ///
          /// Handle THnSparse branch object
          ///
          // obj->GetParent()->SetNCells(nCells);
          // fParent->SetNCells(nCells);
          current->SetNCells(nCells);

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
            //     // NLogger::Debug("AAAA %.0f", hProjTmp ? hProjTmp->GetEntries() : 0);
            //   }
            //   else {
            //     TH1 * temp = ProjectionFromObject(key, projectionAxis, rangesBaseTmp);
            //     // NLogger::Debug("BBBB %.0f", temp ? temp->GetEntries() : 0);
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
            // NLogger::Trace("[L%d] Projection histogram '%s' for branch '%s' storing with indexInProj=%d,
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
            NLogger::Trace("[L%d] Branch '%s' is a TList, getting object at index %d ...", level, key.c_str(),
                                           indexInProj);
            for (int lb : linBins) {
              fGnTree->GetEntry(lb);
            }
            TList * list = dynamic_cast<TList *>(val.GetObject());
            // list->Print();
            // get list of object names
            std::vector<std::string> objNames;
            for (int i = 0; i < list->GetEntries(); i++) {
              TObject * o = list->At(i);
              objNames.push_back(o->GetName());
            }
            // remove "results" histogram
            objNames.erase(std::remove(objNames.begin(), objNames.end(), "results"), objNames.end());

            NLogger::Trace("[L%d] Branch '%s' TList contains %d objects: %s", level, key.c_str(), list->GetEntries(),
                                           NUtils::GetCoordsString(objNames).c_str());

            // std::vector<std::string> possibleNames = {"hPeak", "hBgNorm", "unlikepm_proj_0"};

            // current->SetObjectNames(objNames);
            bool isValid = true;
            for (auto & name : objNames) {
              TH1 * hProjTmp = dynamic_cast<TH1 *>(list->FindObject(name.c_str()));
              if (hProjTmp == nullptr) {
                NLogger::Warning("NGnNavigator::Reshape: Branch '%s' TList does not contain '%s' !!!", key.c_str(),
                                                 name.c_str());
                isValid = false;

                continue;
              }
              if (TMath::IsNaN(hProjTmp->GetEntries()) || TMath::IsNaN(hProjTmp->GetSumOfWeights())) {
                NLogger::Warning("NGnNavigator::Reshape: Branch '%s' '%s' histogram is nan !!!", key.c_str(),

                                                 name.c_str());
                isValid = false;
                continue;
              }
              NLogger::Trace(
                  "[L%d] Histogram name='%s' title='%s' for branch '%s' storing with indexInProj=%d, entries=%.0f",
                  level, name.c_str(), hProjTmp->GetTitle(), key.c_str(), indexInProj, hProjTmp->GetEntries());
              // if (obj->GetParent()->GetObjectContentMap()[name].size() != nCells)
              //   obj->GetParent()->ResizeObjectContentMap(name, nCells);
              // // obj->GetParent()->SetObject(key, hProjTmp, indexInProj);
              // obj->GetParent()->SetObject(name, hProjTmp, indexInProj);
              // if (fParent->GetObjectContentMap()[name].size() != nCells) fParent->ResizeObjectContentMap(name,
              // nCells); fParent->SetObject(name, hProjTmp, indexInProj);
              if (current->GetObjectContentMap()[name].size() != nCells) current->ResizeObjectContentMap(name, nCells);
              current->SetObject(name, hProjTmp, indexInProj);
            }
            if (isValid == false) {
              NLogger::Warning("NGnNavigator::Reshape: Branch '%s' TList does not contain any valid histograms !!!",
                                               key.c_str());
              continue;
            }
            TH1 * hResults = dynamic_cast<TH1 *>(list->FindObject("results"));
            if (hResults) {
              NLogger::Trace("[L%d] Branch '%s' TList contains 'results' histogram with %.0f entries ...", level,
                                             key.c_str(), hResults->GetEntries());
              // loop over bin labels
              for (int b = 1; b <= hResults->GetNbinsX(); b++) {

                std::string binLabel = hResults->GetXaxis()->GetBinLabel(b);
                double      binValue = hResults->GetBinContent(b);
                NLogger::Trace("[L%d]   Bin %d: %s = %e", level, b, binLabel.c_str(), binValue);
                // // check if binlabel is "mass"
                // if (binLabel.compare("mass") == 0) {
                //   NLogger::Info("[L%d]   Checking bin 'mass' = %f ...", level, binValue);
                //   if (binValue < 1.015 || binValue > 1.025) {
                //     NLogger::Info("[L%d]   Skipping bin 'mass' with value %f ...", level, binValue);
                //     continue;
                //   }
                // }
                // obj->GetParent()->SetParameter(binLabel, binValue, indexInProj);
                // fParent->SetParameter(binLabel, binValue, indexInProj);
                current->SetParameter(binLabel, binValue, indexInProj);
              }
            }
          }
          else {
            NLogger::Warning("NGnNavigator::Reshape: Branch '%s' has unsupported class '%s' !!! Skipping ...",
                                             key.c_str(), className.Data());
          }
        }

        // Reshape(binningName, levels, level + 1, rangesTmp, rangesBaseTmp, currentChild);
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
    NLogger::Trace("NGnNavigator::Reshape: Reached the end of levels, level=%d", level);
    return current;
  }

  NLogger::Trace("NGnNavigator::Reshape: =========== Reshaping navigator for level %d DONE ================", level);

  if (level == 0) {
    NLogger::Info("NGnNavigator::Reshape: Reshaping navigator DONE.");
    // print exported axes from indexes from levels
    for (size_t l = 0; l < levels.size(); l++) {
      std::string axesStr = "";
      for (auto & a : levels[l]) {
        TAxis * axis = binningDef->GetContent()->GetAxis(a);
        axesStr += TString::Format("%d('%s') ", a, axis->GetName()).Data();
      }
      NLogger::Info("  Level %zu axes: %s", l, axesStr.c_str());
    }
  }

  // current->Print("");

  return current;
}

void NGnNavigator::Export(const std::string & filename, std::vector<std::string> objectNames, const std::string & wsUrl)
{
  ///
  /// Export object to file
  ///
  NLogger::Info("Exporting NGnNavigator to file: %s", filename.c_str());

  json objJson;

  // if filename ends with .root, remove it
  if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".root") {
    NLogger::Info("Exporting NGnNavigator to ROOT file: %s", filename.c_str());
    TFile * file = TFile::Open(filename.c_str(), "RECREATE");
    if (!file || file->IsZombie()) {
      NLogger::Error("Failed to open file: %s", filename.c_str());
      return;
    }
    file->cd();
    this->Write();
    file->Close();
    delete file;
  }
  else if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".json") {
    NLogger::Info("Exporting NGnNavigator to JSON file: %s", filename.c_str());
    NGnNavigator * obj = const_cast<NGnNavigator *>(this);
    ExportToJson(objJson, obj, objectNames);
    // std::cout << objJson.dump(2) << std::endl;
    // TODO: Use TFile::Open
    std::ofstream outFile(filename);
    outFile << objJson.dump();
    outFile.close();
  }
  else {
    NLogger::Error("Unsupported file format for export: %s", filename.c_str());
    return;
  }
  if (!wsUrl.empty()) {
    NLogger::Info("Uploading exported file to web socket: %s", wsUrl.c_str());
    // NUtils::UploadFileToWebService(filename, wsUrl);

    std::string       message = objJson.dump();
    Ndmspc::NWsClient client;
    if (!message.empty()) {
      NLogger::Info("Connecting to web socket: %s", wsUrl.c_str());
      if (!client.Connect(wsUrl)) {
        Ndmspc::NLogger::Error("Failed to connect to '%s' !!!", wsUrl.c_str());
      }
      else {

        if (!client.Send(objJson.dump())) {
          Ndmspc::NLogger::Error("Failed to send message `%s`", message.c_str());
        }
        else {
          Ndmspc::NLogger::Info("Successfully sent message to '%s'", wsUrl.c_str());
        }
      }
      Ndmspc::NLogger::Info("Disconnecting from '%s' ...", wsUrl.c_str());
      gSystem->Sleep(1000); // wait for a while to ensure message is sent
      client.Disconnect();
    }

    Ndmspc::NLogger::Info("Sent: %s", message.c_str());
  }

  NLogger::Info("Exported NGnNavigator to file: %s", filename.c_str());
}

void NGnNavigator::ExportToJson(json & j, NGnNavigator * obj, std::vector<std::string> objectNames)
{
  ///
  /// Export NGnNavigator to JSON object
  ///

  if (obj == nullptr) {
    NLogger::Error("NGnNavigator::ExportJson: Object is nullptr !!!");
    return;
  }

  // THnSparse * hns = obj->GetProjection();
  // if (hns == nullptr) {
  //   // NLogger::Error("NGnNavigator::ExportJson: HnSparse is nullptr !!!");
  //   return;
  // }

  // std::string name        = hns->GetName();
  // std::string title       = hns->GetTitle();
  // int         nDimensions = hns->GetNdimensions();
  // NLogger::Debug("ExportJson : Exporting '%s' [%dD] (might take some time) ...", title.c_str(), nDimensions);

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
  //   NLogger::Error("NGnNavigator::ExportJson: Unsupported number of dimensions: %d", nDimensions);
  //   return;
  // }

  if (h == nullptr) {
    NLogger::Error("NGnNavigator::ExportJson: Projection is nullptr !!!");
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
    // NLogger::Debug("NGnNavigator::ExportJson: Exporting all objects ...");
    // loop over all keys and add them to objectNames
    bool isValid = false;
    for (const auto & [key, val] : obj->GetObjectContentMap()) {
      isValid = false;
      for (size_t i = 0; i < val.size(); i++) {
        TObject * objContent = val[i];
        // NLogger::Debug("NGnNavigator::ExportJson: Processing object '%s' at index %zu ...", key.c_str(), i);
        if (objContent) {
          // check if object type is inherited from list of names in objectTypes
          std::string className = objContent ? objContent->ClassName() : "";
          if (className.empty()) {
            NLogger::Warning("NGnNavigator::ExportJson: Object %s has empty class name", key.c_str());
            continue;
          }
          // shrink className string to 3 characters if it is longer than 3
          className = className.substr(0, 3);
          // NLogger::Debug("NGnNavigator::ExportJson: Object %s has class '%s'", key.c_str(), className.c_str());
          if (std::find(NGnNavigator::fObjectTypes.begin(), NGnNavigator::fObjectTypes.end(), className) !=
              NGnNavigator::fObjectTypes.end()) {
            // NLogger::Warning(
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
    NLogger::Debug("NGnNavigator::ExportJson: Exporting selected objects: %s",
                   NUtils::GetCoordsString(objectNames).c_str());
  }

  // Print all included object names
  for (const auto & name : objectNames) {
    NLogger::Trace("NGnNavigator::ExportJson: Included object name: '%s'", name.c_str());
  }

  for (const auto & [key, val] : obj->GetObjectContentMap()) {

    // Filter by objectNames
    if (std::find(objectNames.begin(), objectNames.end(), key) == objectNames.end()) {
      NLogger::Debug("NGnNavigator::ExportJson: Skipping object '%s' ...", key.c_str());
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
        // NLogger::Debug("NGnNavigator::ExportJson: Object %s has min=%f, max=%f", objContent->GetName(), objMin,
        //                objMax);

        min     = TMath::Min(min, objMin);
        max     = TMath::Max(max, objMax);
        entries = ((TH1 *)objContent)->GetEntries();
        // NLogger::Debug("NGnNavigator::ExportJson: Adding object %s with min=%f, max=%f", objContent->GetName(),
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
    // NLogger::Debug("NGnNavigator::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
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
        // NLogger::Debug("NGnNavigator::ExportJson: Adding parameter %s with value=%f", key.c_str(), param);
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
    // NLogger::Debug("NGnNavigator::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
  }

  double              min = std::numeric_limits<double>::max();  // Initialize with largest possible double
  double              max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
  std::vector<double> tmpContent;
  for (const auto & child : obj->GetChildren()) {
    // if (child == nullptr) {
    //   NLogger::Error("NGnNavigator::ExportJson: Child is nullptr !!!");
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
        NLogger::Trace("NGnNavigator::ExportJson: Child %s has min=%f, max=%f", childProjection->GetName(), objMin,
                       objMax);
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
    // NLogger::Debug("NGnNavigator::ExportJson: LLLLLLLLLLLLLLLLLLLLLLast");
    int i = -1;
    for (const auto & child : j["children"]["content"]) {
      i++;
      if (child == nullptr) {
        // NLogger::Error("NGnNavigator::ExportJson: Child is nullptr !!!");
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
    NLogger::Info("NGnNavigator: name='%s' title='%s' level=%d levels=%d projection='%s' title='%s'", GetName(),
                  GetTitle(), fLevel, fNLevels, fProjection->GetName(), fProjection->GetTitle());
    // fProjection->Print(option);
  }
  else {
    NLogger::Info("NGnNavigator: name='%s' title='%s' level=%d levels=%d projection=nullptr", GetName(), GetTitle(),
                  fLevel, fNLevels);
  }
  // for (int i = 0; i < fChildren.size(); i++) {
  //   NLogger::Info("NGnNavigator: Child %d/%d: %s", i + 1, fChildren.size(),
  //                 fChildren[i] ? fChildren[i]->GetName() : "nullptr");
  // }
  // print children
  std::vector<int> childIndices;
  for (int i = 0; i < fChildren.size(); i++) {
    NGnNavigator * child = fChildren[i];
    if (child) {
      childIndices.push_back(i);
      // NLogger::Info("NGnNavigator: Child %d/%d:", i + 1, fChildren.size());
      // child->Print(option);
    }
  }
  NLogger::Info("NGnNavigator: %zu children with indices: %s", childIndices.size(),
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

  // if (fGnTree == nullptr) {
  //   NLogger::Error("NGnNavigator::Draw: NGnTree is nullptr !!!");
  //   return;
  // }

  // std::string name;
  if (!gPad) {
    // NLogger::Info("NGnNavigator::Draw: Making default canvas ...");
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
    NLogger::Debug("NGnNavigator::Draw: Drawing level %d", level);
    TVirtualPad * pad = originalPad->cd(level + 1);
    if (pad) {
      NLogger::Debug("NGnNavigator::Draw: Clearing pad %d", level + 1);
      pad->Clear();
      gPad = pad; // Set the current pad to the level + 1 pad
      if (level == 0) {
        obj = this; // For the first level, use the current object
        NLogger::Debug("NGnNavigator::Draw: Using current object at level %d: %s", level, obj->GetName());
      }
      else {

        // obj = nullptr; // Reset the object for the next level
        for (int i = 0; i < obj->GetChildren().size(); i++) {
          NGnNavigator * child = obj->GetChild(i);
          if (child) {
            NLogger::Debug("NGnNavigator::Draw: Found child at level %d: %s", level,
                           child->GetProjection()->GetTitle());
            obj = child; // Get the child object at the current level
            break;
          }
        }
        NLogger::Debug("NGnNavigator::Draw: Using child object at level %d: %s", level,
                       obj ? obj->GetName() : "nullptr");
      }
      if (obj == nullptr) {
        NLogger::Error("NGnNavigator::Draw: Child object at level %d is nullptr !!!", level);
        continue; // Skip to the next level if the child is nullptr
      }
      TH1 * projection = obj->GetProjection();
      if (projection) {
        NLogger::Debug("NGnNavigator::Draw: Drawing projection at level %d: %s", level, projection->GetTitle());
        projection->SetMinimum(0);
        projection->SetStats(kFALSE); // Turn off stats box for clarity
        // gPad->cd();                   // Change to the current pad
        // projection->Draw(option); // Draw the projection histogram
        // obj->Draw(option);      // Draw the object at the current level
        obj->AppendPad(option); // Append the pad to the current pad stack
      }
      else {
        NLogger::Error("NGnNavigator::Draw: Projection at level %d is nullptr !!!", level);
      }
    }
    else {
      NLogger::Error("NGnNavigator::Draw: Pad %d is nullptr !!!", level + 1);
    }
  }
  gPad = originalPad; // Restore the original pad
}

void NGnNavigator::Paint(Option_t * option)
{
  ///
  /// Paint object
  ///
  NLogger::Info("NGnNavigator::Paint: Painting object ...");
  if (fProjection) {
    NLogger::Debug("NGnNavigator::Paint: Painting to pad=%d projection name=%s title=%s ...", fLevel + 1,
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

  // NLogger::Info("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d", event, px, py);

  if (!fProjection || !gPad) return;

  // gPad = gPad->GetMother();
  // NLogger::Debug("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d, gPad=%s title=%s", event, px, py,
  //                gPad->GetName(), gPad->GetTitle());
  // NLogger::Debug("NGnNavigator::ExecuteEvent: event=%d, px=%d, py=%d", event, px, py);

  // Step 1: Convert absolute pixel coordinates to the pad's normalized coordinates (0-1 range)
  Double_t x_pad = gPad->AbsPixeltoX(px);
  Double_t y_pad = gPad->AbsPixeltoY(py);

  // Step 2: Convert the pad's normalized coordinates to the user's coordinate system (the histogram axes)
  Double_t x_user = gPad->PadtoX(x_pad);
  Double_t y_user = gPad->PadtoY(y_pad);

  Int_t bin = fProjection->FindBin(x_user, y_user);

  // --- MOUSE HOVER LOGIC ---
  if (event == kMouseMotion) {
    if (bin != fLastHoverBin) {
      // Check if the cursor is inside a bin with content
      if (fProjection->GetBinContent(bin) > 0) {
        Int_t binx, biny, binz;
        fProjection->GetBinXYZ(bin, binx, biny, binz);
        NLogger::Debug("[%s] Mouse hover on bin[%d, %d] at px[%f, %f] level=%d nLevels=%d", gPad->GetName(), binx, biny,
                       x_user, y_user, fLevel, fNLevels);
      }
      fLastHoverBin = bin;
      NLogger::Debug("[%s] Setting point for level %d %s", gPad->GetName(), fLevel, fProjection->GetTitle());
    }
  }

  TVirtualPad * originalPad = gPad; // Save the original pad
  // --- MOUSE CLICK LOGIC ---
  if (event == kButton1Down) {
    Int_t binx, biny, binz;
    fProjection->GetBinXYZ(bin, binx, biny, binz);
    Double_t content = fProjection->GetBinContent(bin);
    NLogger::Info("NGnNavigator::ExecuteEvent: [%s] Mouse click on bin=[%d, %d] at px=[%f, %f] with content: %f  "
                  "level=%d nLevels=%d",
                  gPad->GetName(), binx, biny, x_user, y_user, content, fLevel, fNLevels);

    int nDimensions = fGnTree->GetBinning()->GetDefinition()->GetContent()->GetNdimensions();

    Int_t index = fProjection->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx),
                                          fProjection->GetYaxis()->GetBinCenter(biny));
    if (nDimensions == 1) {
      // For 1D histograms, we need to find the index correctly
      index = fProjection->GetXaxis()->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx));
    }
    NLogger::Debug("NGnNavigator::ExecuteEvent: Index in histogram: %d level=%d", index, fLevel);
    NGnNavigator * child   = GetChild(index);
    TCanvas *      cObject = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("cObject");
    if (child && child->GetProjection()) {
      NLogger::Debug("NGnNavigator::ExecuteEvent: [%s]Child object '%p' found at index %d", gPad->GetName(),
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
      NLogger::Debug("NGnNavigator::ExecuteEvent: %d", child->GetLastIndexSelected());
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
      NLogger::Warning("NGnNavigator::ExecuteEvent: No child object found at index %d", index);
      std::string objName = fObjectNames.empty() ? "unlikepm" : fObjectNames[0];
      TH1 *       hProj   = (TH1 *)GetObject(objName, index);
      if (hProj == nullptr) {
        NLogger::Error("NGnNavigator::ExecuteEvent: No histogram found for index %d", index);
        return;
      }
      hProj->Print();
      if (cObject == nullptr) {
        cObject = new TCanvas("cObject", "cObject", 800, 600);
      }
      cObject->cd();
      // gPad = cObject->GetPad(0); // Get the current pad
      // hProj->SetTitle(Form("Projection of %s at index %d", fProjection->GetName(), index));
      hProj->Draw(); // Draw the projection histogram
    }
    // else {
    // }
    gPad->ModifiedUpdate(); // Force pad to redraw
  }
  gPad = originalPad; // Restore the original pad
}

NGnNavigator * NGnNavigator::GetChild(int index) const
{
  ///
  /// Returns child object at given index
  ///
  // NLogger::Debug("NGnNavigator::GetChild: index=%d, size=%zu", index, fChildren.size());
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

NGnNavigator * NGnNavigator::Open(TTree * tree, const std::string & branches, TFile * file)
{
  ///
  /// Open NGnNavigator from TTree
  ///

  NGnTree * ngt = NGnTree::Open(tree, branches, file);

  NGnNavigator * navigator = new NGnNavigator();
  if (!navigator) {
    return nullptr;
  }
  navigator->SetGnTree(ngt);

  return navigator;
}

NGnNavigator * NGnNavigator::Open(const std::string & filename, const std::string & branches,
                                  const std::string & treename)
{
  ///
  /// Open NGnNavigator from file
  ///
  NGnTree * ngt = NGnTree::Open(filename, branches, treename);

  NGnNavigator * navigator = new NGnNavigator();
  if (!navigator) {
    return nullptr;
  }
  navigator->SetGnTree(ngt);

  return navigator;
}
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
    NLogger::Trace("NGnNavigator::SetObject: name=%s, obj=%p, index=%d", name.c_str(), obj, index,
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
      NLogger::Trace("NGnNavigator::SetParameter: Resizing parameter content map for '%s' to %d", name.c_str(),
                     fNCells);
      ResizeParameterContentMap(name, fNCells);
    }
    NLogger::Trace("NGnNavigator::SetParameter: name=%s, value=%f, index=%d", name.c_str(), value, index,
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

} // namespace Ndmspc
