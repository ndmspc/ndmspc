#include <fstream>
#include <cmath>
#include "TMathBase.h"
#include "TString.h"
#include <TMath.h>
#include <TROOT.h>
#include <TVirtualPad.h>
#include <TPad.h>
#include <TCanvas.h>
#include <TLatex.h>
#include <TH1.h>
#include <TBufferJSON.h>
#include "NGnTree.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseTreePoint.h"
#include "NHnSparseTree.h"
#include "NHnSparseObject.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseObject);
/// \endcond

namespace Ndmspc {
NHnSparseObject::NHnSparseObject(std::vector<TAxis *> axes) : NGnTree(axes) {}
NHnSparseObject::NHnSparseObject(TObjArray * axes) : NGnTree(axes) {}
NHnSparseObject::~NHnSparseObject()
{
  ///
  /// Destructor
  ///

  for (auto & child : fChildren) {
    if (child) {
      child->SetParent(nullptr); // Clear parent pointer in child
      delete child;              // Delete child object
    }
  }

  SafeDelete(fBinning);
  SafeDelete(fHnSparse);
  SafeDelete(fProjection);
}
void NHnSparseObject::Print(Option_t * option) const
{
  ///
  /// Print object
  ///
  NLogInfo("NHnSparseObject:");
  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogError("Binning is not initialized in NHnSparseObject !!!");
  }
}

int NHnSparseObject::Fill(TH1 * h, NHnSparseTreePoint * p)
{
  ///
  /// Fill
  ///
  if (!fBinning) {
    NLogError("NHnSparseObject::Fill: Binning is not initialized in NHnSparseObject !!!");
    return -1;
  }
  if (!h) {
    NLogError("NHnSparseObject::Fill: Histogram is nullptr !!!");
    return -1;
  }
  if (!p) {
    NLogError("NHnSparseObject::Fill: Point is nullptr !!!");
    return -1;
  }

  std::vector<int> varAxes = p->GetVariableAxisIndexes();
  NLogDebug("Variable axes: %s", Ndmspc::NUtils::GetCoordsString(varAxes, -1).c_str());
  std::vector<std::vector<int>> axisRanges;
  for (size_t i = 0; i < varAxes.size(); i++) {
    std::vector<int> varAxesPoint = {varAxes[i]};
    std::vector<int> axisCoords = p->GetHnSparseTree()->GetBinning()->GetAxisBinning(varAxes[i], p->GetPointContent());
    varAxesPoint.insert(varAxesPoint.end(), axisCoords.begin(), axisCoords.end());
    axisRanges.push_back(varAxesPoint);
    NLogDebug("Variable axis: %s", Ndmspc::NUtils::GetCoordsString(axisRanges[i], -1).c_str());
  }

  // loop over axisRanges
  std::vector<int> coords;
  for (const auto & range : axisRanges) {
    // NLogDebug("Axis range: %s", Ndmspc::NUtils::GetCoordsString(range, -1).c_str());
    coords.push_back(range[1]);
    coords.push_back(range[2]);
    coords.push_back(range[3]);
  }
  // NLogDebug("Point content: %s", Ndmspc::NUtils::GetCoordsString(coords, -1).c_str());

  Int_t coordsInt[coords.size()];

  for (int iVarAxis = 0; iVarAxis < axisRanges.size(); iVarAxis++) {

    int              idAxis           = axisRanges[iVarAxis][0];
    std::vector<int> currentCoords    = coords;
    int              currentFillIndex = coords[iVarAxis * 3 + 2];
    // NLogDebug("Current fill index for axis %d: %d", idAxis, currentFillIndex);
    currentCoords[iVarAxis * 3 + 2] = 0;
    h->Print();
    for (int i = 0; i < h->GetNbinsX(); i++) {
      std::string binLabel = h->GetXaxis()->GetBinLabel(i + 1);
      // NLogDebug("Bin %d: %s", i + 1, binLabel.c_str());

      NUtils::VectorToArray(currentCoords, coordsInt);
      if (fBinning->GetContent()->GetBin(coordsInt, kFALSE) < 0) {
        fBinning->GetContent()->SetBinContent(coordsInt, 1);
      }
      Int_t indexInVector = fBinning->GetContent()->GetBin(coordsInt);

      // if (i == 0) {
      //   NLogDebug("Setting bin content for coords %s: index=%d",
      //                  NUtils::GetCoordsString(currentCoords, -1).c_str(), indexInVector);
      // }
      // NLogDebug("AAAA %d %d", fObjectContentMap[binLabel].size(), indexInVector);

      TObject * obj  = fObjectContentMap[binLabel].empty() ? nullptr
                       : fObjectContentMap[binLabel].size() <= indexInVector
                           ? nullptr
                           : fObjectContentMap[binLabel][indexInVector];
      TH1 *     hist = dynamic_cast<TH1 *>(obj);
      if (obj == nullptr) {
        if (i == 0) NLogDebug("Creating new object for bin %d: %s", i + 1, binLabel.c_str());
        // Create object based on the histogram
        // TODO: Make it more generic (TObject)
        hist = new TH1D();
        hist->Sumw2();
        std::string name = binLabel;

        name += "_" + std::to_string(idAxis) + "_" + std::to_string(currentCoords[iVarAxis * 3 + 0]) + "_" +
                std::to_string(currentCoords[iVarAxis * 3 + 1]) + "_" + std::to_string(currentCoords[iVarAxis * 3 + 2]);
        if (axisRanges.size() > 1) {
          int iVarAxisCut = iVarAxis == 0 ? 1 : 0; // if there is only one variable axis, use the first one
          int idAxisCut   = iVarAxis == 0 ? axisRanges[1][0]
                                          : axisRanges[0][0]; // if there is only one variable axis, use the first one
          name += " (" + std::to_string(idAxisCut) + "_" + std::to_string(currentCoords[iVarAxisCut * 3 + 0]) + "_" +
                  std::to_string(currentCoords[iVarAxisCut * 3 + 1]) + "_" +
                  std::to_string(currentCoords[iVarAxisCut * 3 + 2]) + ")";
        }
        name = binLabel;
        name += NUtils::GetCoordsString(currentCoords, -1);

        // double min, max;
        // fBinning->GetAxisRange(2, min, max, {p[projIds[2] * 3], p[projIds[2] * 3 + 1], p[projIds[2] * 3 + 2]});
        // stackTitle += projNames.size() > 2 ? " for " + projNames[2] + " " + Form(" [%f,%f]", min, max) : "";

        std::string title = name;
        hist->SetNameTitle(name.c_str(), title.c_str());

        // if (i == 0) NLogDebug("fBinning->GetAxes()[idAxis=%d]", iVarAxis);
        TAxis * a = fBinning->GetAxes()[iVarAxis];
        if (a == nullptr) {
          NLogError("NHnSparseObject::Fill: Axis %d is nullptr !!!", iVarAxis);
          return -1;
        }
        int currentRebin = a->GetNbins() / currentCoords[iVarAxis * 3 + 0];
        hist->SetBins(currentRebin, a->GetXmin(), a->GetXmax());
        // if (i == 0)
        //   NLogDebug("Setting histogram %s bins: %d, [%f,%f]", hist->GetName(), currentRebin, a->GetXmin(),
        //                  a->GetXmax());
        // if (a->GetXbins()->GetSize() > 0) {
        //   // If the axis has variable bins, set the binning
        //   hist->SetBins(a->GetNbins(), a->GetXbins()->GetArray());
        // }
        // else {
        //   // If the axis has fixed bins, set the binning
        //   hist->SetBins(a->GetNbins(), a->GetXmin(), a->GetXmax());
        //   // int currentRebin = currentCoords[iVarAxis * 3 + 0];
        //   // hist->SetBins(currentRebin, a->GetXmin(), a->GetXmax());
        // }
        // hist->Print();

        obj = hist;
        fObjectContentMap[binLabel].push_back(obj);
      }
      else {
        if (i == 0) NLogDebug("Adding content to existing object for bin %d: %s", i + 1, binLabel.c_str());
      }

      // check if obj is TH1
      // Fill the histogram with the content from the point
      //
      //

      // p->Print();
      //
      hist->SetBinContent(currentFillIndex, h->GetBinContent(i + 1));
      hist->SetBinError(currentFillIndex, h->GetBinError(i + 1));
      if (i == 0)
        NLogDebug("2 Setting bin content for coords %s: index=%d, value=%e error=%e",
                  NUtils::GetCoordsString(currentCoords, -1).c_str(), indexInVector,
                  hist->GetBinContent(currentFillIndex), hist->GetBinError(currentFillIndex));
    }
  }

  fBinning->GetContent()->Print();
  return 0;
}
std::vector<TObject *> NHnSparseObject::GetObjects(const std::string & name) const
{
  /// Returns points for given name
  auto it = fObjectContentMap.find(name);
  if (it != fObjectContentMap.end()) {
    return it->second;
  }
  return {};
}

TObject * NHnSparseObject::GetObject(const std::string & name, int index) const
{
  /// Returns point for given name and index
  auto it = fObjectContentMap.find(name);
  if (it != fObjectContentMap.end() && index < (int)it->second.size()) {
    return it->second[index];
  }
  return nullptr;
}

void NHnSparseObject::SetObject(const std::string & name, TObject * obj, int index)
{
  ///
  /// Set object for given name and index
  ///
  if (obj) {
    if (fObjectContentMap.find(name) == fObjectContentMap.end()) {
      ResizeObjectContentMap(name, fNCells);
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

std::vector<double> NHnSparseObject::GetParameters(const std::string & name) const
{
  /// Returns parameters for given name
  auto it = fParameterContentMap.find(name);
  if (it != fParameterContentMap.end()) {
    return it->second;
  }
  return {};
}

double NHnSparseObject::GetParameter(const std::string & name, int index) const
{
  /// Returns parameter for given name and index
  auto it = fParameterContentMap.find(name);
  if (it != fParameterContentMap.end() && index < (int)it->second.size()) {
    return it->second[index];
  }
  return TMath::QuietNaN();
}

void NHnSparseObject::SetParameter(const std::string & name, double value, int index)
{
  ///
  /// Set parameter for given name and index
  ///
  if (!std::isnan(value)) {
    if (fParameterContentMap.find(name) == fParameterContentMap.end() || fParameterContentMap[name].size() < fNCells) {
      NLogTrace("NHnSparseObject::SetParameter: Resizing parameter content map for '%s' to %d", name.c_str(), fNCells);
      ResizeParameterContentMap(name, fNCells);
    }
    NLogTrace("NHnSparseObject::SetParameter: name=%s, value=%f, index=%d", name.c_str(), value, index,
              fParameterContentMap[name].size());
    if (index < 0) {
      fParameterContentMap[name].push_back(value);
    }
    else {
      fParameterContentMap[name][index] = value;
    }
  }
}

NHnSparseObject * NHnSparseObject::GetChild(int index) const
{
  ///
  /// Returns child object at given index
  ///
  // NLogDebug("NHnSparseObject::GetChild: index=%d, size=%zu", index, fChildren.size());
  return (index >= 0 && index < fChildren.size()) ? fChildren[index] : nullptr;
}
void NHnSparseObject::SetChild(NHnSparseObject * child, int index)
{
  ///
  /// Set child object at given index
  ///
  if (child) {
    fChildren[index < 0 ? fChildren.size() : index] = child;
    child->SetParent(this);
  }
}

void NHnSparseObject::Export(std::string filename)
{
  ///
  /// Export object to file
  ///
  NLogInfo("Exporting NHnSparseObject to file: %s", filename.c_str());
  // if filename ends with .root, remove it
  if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".root") {
    NLogInfo("Exporting NHnSparseObject to ROOT file: %s", filename.c_str());
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
    NLogInfo("Exporting NHnSparseObject to JSON file: %s", filename.c_str());
    json              objJson;
    NHnSparseObject * obj = const_cast<NHnSparseObject *>(this);
    ExportJson(objJson, obj);
    // std::cout << objJson.dump(2) << std::endl;
    // TODO: Use TFile::Open
    std::ofstream outFile(filename);
    outFile << objJson.dump();
    outFile.close();
  }
  else {
    NLogError("Unsupported file format for export: %s", filename.c_str());
    return;
  }

  NLogInfo("Exported NHnSparseObject to file: %s", filename.c_str());
}

void NHnSparseObject::ExportJson(json & j, NHnSparseObject * obj)
{
  ///
  /// Export object to JSON
  ///

  if (obj == nullptr) {
    NLogError("NHnSparseObject::ExportJson: Object is nullptr !!!");
    return;
  }

  THnSparse * hns = obj->GetHnSparse();
  if (hns == nullptr) {
    // NLogError("NHnSparseObject::ExportJson: HnSparse is nullptr !!!");
    return;
  }

  std::string name        = hns->GetName();
  std::string title       = hns->GetTitle();
  int         nDimensions = hns->GetNdimensions();
  NLogDebug("ExportJson : Exporting '%s' [%dD] (might take some time) ...", title.c_str(), nDimensions);

  if (obj->GetChildren().empty()) {
    return;
  }

  TH1 * h;
  if (nDimensions == 1) {
    TAxis * xAxis = hns->GetAxis(0);
    TH1 *   temp  = hns->Projection(0);
    h             = new TH2D(name.c_str(), title.c_str(), xAxis->GetNbins(), xAxis->GetXmin(), xAxis->GetXmax(), 1, 0,
                             1); // Create a dummy 2D histogram for 1D projection
    h->GetXaxis()->SetName(xAxis->GetName());
    h->GetXaxis()->SetTitle(xAxis->GetTitle());
    for (int i = 1; i <= temp->GetNbinsX(); ++i) {
      h->SetBinContent(i, 1, temp->GetBinContent(i));
      h->SetBinError(i, 1, temp->GetBinError(i));
    }
    obj->SetProjection(h);

    h = hns->Projection(0);

    // obj->SetProjection(h);
  }
  else if (nDimensions == 2) {
    h = hns->Projection(1, 0);
    obj->SetProjection(h);
  }
  else if (nDimensions == 3) {
    h = hns->Projection(0, 1, 2);
    obj->SetProjection(h);
  }
  else {
    NLogError("NHnSparseObject::ExportJson: Unsupported number of dimensions: %d", nDimensions);
    return;
  }

  if (h == nullptr) {
    NLogError("NHnSparseObject::ExportJson: Projection is nullptr !!!");
    return;
  }

  h->SetNameTitle(name.c_str(), title.c_str());
  h->SetMinimum(0);
  // After the projection, force update statistics

  h->SetStats(kFALSE); // Turn off stats box for clarity
  // h->SetDirectory(nullptr); // Avoid ROOT trying to save the histogram in a file

  j = json::parse(TBufferJSON::ConvertToJSON(h).Data());
  // loop over content map and add objects
  double entries = 0.0;
  // int    idx     = 0;
  for (const auto & [key, val] : obj->GetObjectContentMap()) {
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
        // NLogDebug("NHnSparseObject::ExportJson: Object %s has min=%f, max=%f", objContent->GetName(), objMin,
        //                objMax);

        min     = TMath::Min(min, objMin);
        max     = TMath::Max(max, objMax);
        entries = ((TH1 *)objContent)->GetEntries();
        // NLogDebug("NHnSparseObject::ExportJson: Adding object %s with min=%f, max=%f", objContent->GetName(),
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
    // NLogDebug("NHnSparseObject::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
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
        // NLogDebug("NHnSparseObject::ExportJson: Adding parameter %s with value=%f", key.c_str(), param);
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
    // NLogDebug("NHnSparseObject::ExportJson: key=%s Min=%f, Max=%f", key.c_str(), min, max);
  }

  double              min = std::numeric_limits<double>::max();  // Initialize with largest possible double
  double              max = -std::numeric_limits<double>::max(); // Initialize with smallest possible double
  std::vector<double> tmpContent;
  for (const auto & child : obj->GetChildren()) {
    // if (child == nullptr) {
    //   NLogError("NHnSparseObject::ExportJson: Child is nullptr !!!");
    //   continue;
    // }
    json   childJson;
    TH1 *  childProjection = nullptr;
    double objMin, objMax;
    double entries = 0.0; // Reset entries for each child
    if (child != nullptr) {
      ExportJson(childJson, child);

      childProjection = (TH1 *)TBufferJSON::ConvertFromJSON(childJson.dump().c_str());
      if (childProjection) {
        childProjection->Draw("colz text");
        NUtils::GetTrueHistogramMinMax((TH1 *)childProjection, objMin, objMax, false);
        // min = TMath::Min(min, objMin);
        min     = 0;
        max     = TMath::Max(max, objMax);
        entries = childProjection->GetEntries();
        NLogDebug("NHnSparseObject::ExportJson: Child %s has min=%f, max=%f", childProjection->GetName(), objMin,
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
    NLogDebug("NHnSparseObject::ExportJson: XXXX max=%f", max);
  }

  if (obj->GetParent() == nullptr) {
    // NLogDebug("NHnSparseObject::ExportJson: LLLLLLLLLLLLLLLLLLLLLLast");
    int i = -1;
    for (const auto & child : j["children"]["content"]) {
      i++;
      if (child == nullptr) {
        // NLogError("NHnSparseObject::ExportJson: Child is nullptr !!!");
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

void NHnSparseObject::Draw(Option_t * option)
{
  ///
  /// Draw object
  ///
  ///

  std::string name;
  if (!gPad) {
    NLogError("NHnSparseObject::Draw: gPad is nullptr !!!");
    gROOT->MakeDefCanvas();
    if (!gPad->IsEditable()) return;
    Int_t cy = TMath::Sqrt(fNLevels);
    Int_t cx = TMath::Ceil(fNLevels / (Double_t)cy);
    gPad->Divide(cy, cx); // Divide the pad into a grid based on the number of levels
  }

  if (fHnSparse == nullptr) {
    NLogError("NHnSparseObject::Draw: HnSparse is nullptr !!!");
    return;
  }

  TVirtualPad *     originalPad = gPad; // Save the original pad
  NHnSparseObject * obj;
  for (int level = 0; level < fNLevels; level++) {
    NLogDebug("NHnSparseObject::Draw: Drawing level %d", level);
    TVirtualPad * pad = originalPad->cd(level + 1);
    if (pad) {
      // NLogDebug("NHnSparseObject::Draw: Clearing pad %d", level + 1);
      pad->Clear();
      gPad = pad; // Set the current pad to the level + 1 pad
      if (level == 0) {
        obj = this; // For the first level, use the current object
        NLogDebug("NHnSparseObject::Draw: Using current object at level %d: %s", level, obj->GetName());
      }
      else {

        // obj = nullptr; // Reset the object for the next level
        for (int i = 0; i < obj->GetChildren().size(); i++) {
          NHnSparseObject * child = obj->GetChild(i);
          if (child) {
            NLogDebug("NHnSparseObject::Draw: Found child at level %d: %s", level, child->GetProjection()->GetTitle());
            obj = child; // Get the child object at the current level
            break;
          }
        }
        NLogDebug("NHnSparseObject::Draw: Using child object at level %d: %s", level, obj ? obj->GetName() : "nullptr");
      }
      if (obj == nullptr) {
        NLogError("NHnSparseObject::Draw: Child object at level %d is nullptr !!!", level);
        continue; // Skip to the next level if the child is nullptr
      }
      obj->GetProjection()->SetMinimum(0);
      obj->GetProjection()->SetStats(kFALSE); // Turn off stats box for clarity
      obj->Draw(option);                      // Draw the object at the current level
      obj->AppendPad(option);                 // Append the pad to the current pad stack
    }
    else {
      NLogError("NHnSparseObject::Draw: Pad %d is nullptr !!!", level + 1);
    }
  }
  gPad = originalPad; // Restore the original pad
}

void NHnSparseObject::Paint(Option_t * option)
{
  ///
  /// Paint object
  ///
  // NLogInfo("NHnSparseObject::Paint: Painting object ...");
  if (fProjection) {
    NLogDebug("NHnSparseObject::Paint: Painting to pad=%d projection name=%s title=%s ...", fLevel + 1,
              fProjection->GetName(), fProjection->GetTitle());
    // fProjection->Paint(option);
    fProjection->Paint("colz text");
  }
}

Int_t NHnSparseObject::DistancetoPrimitive(Int_t px, Int_t py)
{
  ///
  /// This method tells the pad if the mouse is "on" our object.
  ///

  if (!fProjection) {
    return 9999; // Not drawn, so we are infinitely far.
  }

  return fProjection->DistancetoPrimitive(px, py);
}

void NHnSparseObject::ExecuteEvent(Int_t event, Int_t px, Int_t py)
{
  ///
  /// Execute event
  ///
  ///

  if (!fProjection || !gPad) return;

  // gPad = gPad->GetMother();
  // NLogDebug("NHnSparseObject::ExecuteEvent: event=%d, px=%d, py=%d, gPad=%s title=%s", event, px, py,
  //                gPad->GetName(), gPad->GetTitle());
  // NLogDebug("NHnSparseObject::ExecuteEvent: event=%d, px=%d, py=%d", event, px, py);

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
        NLogDebug("[%s]Mouse hover on bin[%d, %d] at px[%f, %f] level=%d nLevels=%d", gPad->GetName(), binx, biny,
                  x_user, y_user, fLevel, fNLevels);
      }
      fLastHoverBin = bin;
      NLogDebug("[%s] Setting point for level %d %s", gPad->GetName(), fLevel, fProjection->GetTitle());
    }
  }

  TVirtualPad * originalPad = gPad; // Save the original pad
  // --- MOUSE CLICK LOGIC ---
  if (event == kButton1Down) {
    Int_t binx, biny, binz;
    fProjection->GetBinXYZ(bin, binx, biny, binz);
    Double_t content = fProjection->GetBinContent(bin);
    NLogInfo("[%s]Mouse click on bin=[%d, %d] at px=[%f, %f] with content: %f  level=%d nLevels=%d", gPad->GetName(),
             binx, biny, x_user, y_user, content, fLevel, fNLevels);

    int nDimensions = fHnSparse->GetNdimensions();

    Int_t index = fProjection->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx),
                                          fProjection->GetYaxis()->GetBinCenter(biny));
    if (nDimensions == 1) {
      // For 1D histograms, we need to find the index correctly
      index = fProjection->GetXaxis()->FindFixBin(fProjection->GetXaxis()->GetBinCenter(binx));
    }
    NLogDebug("Index in histogram: %d level=%d", index, fLevel);
    NHnSparseObject * child   = GetChild(index);
    TCanvas *         cObject = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("cObject");
    if (child && child->GetProjection()) {
      NLogDebug("[%s]Child object '%p' found at index %d", gPad->GetName(), child->GetProjection(), index);
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
      NLogDebug("NHnSparseObject::ExecuteEvent: %d", child->GetLastIndexSelected());
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
      NLogWarning("No child object found at index %d", index);
      TH1 * hProj = (TH1 *)GetObject("unlikepm", index);
      if (hProj == nullptr) {
        NLogError("No histogram found for index %d", index);
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
void NHnSparseObject::SetNLevels(Int_t n)
{
  ///
  /// Set number of levels in the hierarchy
  ///
  fNLevels = n;
}
} // namespace Ndmspc
