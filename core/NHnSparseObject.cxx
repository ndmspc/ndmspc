#include <TH1.h>
#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseTreePoint.h"
#include "NHnSparseTree.h"
#include "RtypesCore.h"
#include "NHnSparseObject.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseObject);
/// \endcond

namespace Ndmspc {
NHnSparseObject::NHnSparseObject(std::vector<TAxis *> axes) : TObject(), fBinning(new NBinning(axes)) {}
NHnSparseObject::NHnSparseObject(TObjArray * axes) : TObject(), fBinning(new NBinning(axes)) {}
NHnSparseObject::~NHnSparseObject()
{
  ///
  /// Destructor
  ///

  SafeDelete(fBinning);
}
void NHnSparseObject::Print(Option_t * option) const
{
  ///
  /// Print object
  ///
  NLogger::Info("NHnSparseObject:");
  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogger::Error("Binning is not initialized in NHnSparseObject !!!");
  }
}

int NHnSparseObject::Fill(TH1 * h, NHnSparseTreePoint * p)
{
  ///
  /// Fill
  ///
  if (!fBinning) {
    NLogger::Error("NHnSparseObject::Fill: Binning is not initialized in NHnSparseObject !!!");
    return -1;
  }
  if (!h) {
    NLogger::Error("NHnSparseObject::Fill: Histogram is nullptr !!!");
    return -1;
  }
  if (!p) {
    NLogger::Error("NHnSparseObject::Fill: Point is nullptr !!!");
    return -1;
  }

  std::vector<int> varAxes = p->GetVariableAxisIndexes();
  Ndmspc::NLogger::Debug("Variable axes: %s", Ndmspc::NUtils::GetCoordsString(varAxes, -1).c_str());
  std::vector<std::vector<int>> axisRanges;
  for (size_t i = 0; i < varAxes.size(); i++) {
    std::vector<int> varAxesPoint = {varAxes[i]};
    std::vector<int> axisCoords = p->GetHnSparseTree()->GetBinning()->GetAxisBinning(varAxes[i], p->GetPointContent());
    varAxesPoint.insert(varAxesPoint.end(), axisCoords.begin(), axisCoords.end());
    axisRanges.push_back(varAxesPoint);
    Ndmspc::NLogger::Debug("Variable axis: %s", Ndmspc::NUtils::GetCoordsString(axisRanges[i], -1).c_str());
  }

  // loop over axisRanges
  std::vector<int> coords;
  for (const auto & range : axisRanges) {
    // Ndmspc::NLogger::Debug("Axis range: %s", Ndmspc::NUtils::GetCoordsString(range, -1).c_str());
    coords.push_back(range[1]);
    coords.push_back(range[2]);
    coords.push_back(range[3]);
  }
  // Ndmspc::NLogger::Debug("Point content: %s", Ndmspc::NUtils::GetCoordsString(coords, -1).c_str());

  Int_t coordsInt[coords.size()];

  for (int iVarAxis = 0; iVarAxis < axisRanges.size(); iVarAxis++) {

    int              idAxis           = axisRanges[iVarAxis][0];
    std::vector<int> currentCoords    = coords;
    int              currentFillIndex = coords[iVarAxis * 3 + 2];
    // NLogger::Debug("Current fill index for axis %d: %d", idAxis, currentFillIndex);
    currentCoords[iVarAxis * 3 + 2] = 0;
    // NLogger::Info("Current coords for axis %d: %s", idAxis, NUtils::GetCoordsString(currentCoords, -1).c_str());
    h->Print();
    for (int i = 0; i < h->GetNbinsX(); i++) {
      std::string binLabel = h->GetXaxis()->GetBinLabel(i + 1);
      // NLogger::Debug("Bin %d: %s", i + 1, binLabel.c_str());

      NUtils::VectorToArray(currentCoords, coordsInt);
      if (fBinning->GetContent()->GetBin(coordsInt, kFALSE) < 0) {
        fBinning->GetContent()->SetBinContent(coordsInt, 1);
      }
      Int_t indexInVector = fBinning->GetContent()->GetBin(coordsInt);

      // if (i == 0) {
      //   NLogger::Debug("Setting bin content for coords %s: index=%d",
      //                  NUtils::GetCoordsString(currentCoords, -1).c_str(), indexInVector);
      // }
      // NLogger::Debug("AAAA %d %d", fObjectContentMap[binLabel].size(), indexInVector);

      TObject * obj  = fObjectContentMap[binLabel].empty() ? nullptr
                       : fObjectContentMap[binLabel].size() <= indexInVector
                           ? nullptr
                           : fObjectContentMap[binLabel][indexInVector];
      TH1 *     hist = dynamic_cast<TH1 *>(obj);
      if (obj == nullptr) {
        if (i == 0) NLogger::Debug("Creating new object for bin %d: %s", i + 1, binLabel.c_str());
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
        name              = binLabel + NUtils::GetCoordsString(currentCoords, -1);
        std::string title = "AAA" + name;
        hist->SetNameTitle(name.c_str(), title.c_str());

        // if (i == 0) NLogger::Debug("fBinning->GetAxes()[idAxis=%d]", iVarAxis);
        TAxis * a = fBinning->GetAxes()[iVarAxis];
        if (a == nullptr) {
          NLogger::Error("NHnSparseObject::Fill: Axis %d is nullptr !!!", iVarAxis);
          return -1;
        }
        int currentRebin = a->GetNbins() / currentCoords[iVarAxis * 3 + 0];
        hist->SetBins(currentRebin, a->GetXmin(), a->GetXmax());
        // if (i == 0)
        //   NLogger::Debug("Setting histogram %s bins: %d, [%f,%f]", hist->GetName(), currentRebin, a->GetXmin(),
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
        if (i == 0) NLogger::Debug("Adding content to existing object for bin %d: %s", i + 1, binLabel.c_str());
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
        NLogger::Debug("2 Setting bin content for coords %s: index=%d, value=%e error=%e",
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
} // namespace Ndmspc
