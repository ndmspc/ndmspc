#include <TFile.h>
#include <THnSparse.h>
#include <TH1.h>
#include "Results.h"

#include "Core.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Results);
/// \endcond

namespace Ndmspc {
Results::Results() : TObject() {}
Results::~Results() {}
bool Results::LoadConfig(std::string configfilename, std::string userconfig, std::string environment,
                         std::string userConfigRaw)
{
  bool configLoaded = Core::LoadConfig(configfilename, userconfig, environment, userConfigRaw);
  if (!gCfg["ndmspc"]["data"]["histogram"]["enabled"].is_null() ||
      gCfg["ndmspc"]["data"]["histogram"]["enabled"].is_boolean()) {
    if (gCfg["ndmspc"]["data"]["histogram"]["enabled"].get<bool>()) fDataSource = DataSource::histogram;
  }
  std::string hostUrl = gCfg["ndmspc"]["output"]["host"].get<std::string>();
  std::string path;
  if (!hostUrl.empty()) path = hostUrl + "/";
  path += gCfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  for (auto & cut : gCfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    path += cut["axis"].get<std::string>() + "_";
    fCuts.push_back(cut["axis"].get<std::string>());
  }

  path[path.size() - 1] = '/';

  path += environment + "/";

  fInputFileName = path + fResultFileName;

  return configLoaded;
}
void Results::Print(Option_t * option) const
{
  ///
  /// Print results information
  ///
  // Printf("%s", gCfg.dump(2).c_str());

  int iAxis = 0;
  for (auto & an : fAxes) {

    printf("axis: %15s(%s)\t[val=%d] \t[", an.c_str(), fAxesTypes[iAxis].c_str(), fPoint[iAxis]);
    if (fAxesLabels.find(an) == fAxesLabels.end()) {
      Printf("]");
      continue;
    }
    int iLabel = 0;
    for (auto & al : fAxesLabels.at(an)) {
      if (al.empty()) continue;
      if (fAxesLabels.at(an).back() == al) {
        printf("%s(%d)", al.c_str(), iLabel + 1);
      }
      else {
        printf("%s(%d),", al.c_str(), iLabel + 1);
      }
      iLabel++;
    }

    Printf("]");
    iAxis++;
  }
}

bool Results::LoadResults()
{
  ///
  /// Load results
  ///
  Printf("Opening file '%s' ...", fInputFileName.c_str());

  fInputFile = TFile::Open(fInputFileName.c_str());
  if (!fInputFile) {

    Printf("Error: Input file '%s' was not found !!!", fInputFileName.c_str());
    return false;
  }

  fResultHnSparse = (THnSparse *)fInputFile->Get(fResultsHnSparseName.c_str());
  if (!fResultHnSparse) {
    Printf("Error: Results THnSparse histogram '%s' was not found !!!", fResultsHnSparseName.c_str());
    return false;
  }

  fMapAxesType = (TH1 *)fInputFile->Get(fMapAxesTypeName.c_str());
  if (!fMapAxesType) {
    Printf("Error: 'mapAxesType' histogram was not found !!!");
    return false;
  }

  int pointsSize = gCfg["ndmspc"]["result"]["axes"].size() + 1;
  if (fDataSource == DataSource::histogram) pointsSize += gCfg["ndmspc"]["result"]["data"]["defaults"].size() + 1;
  // int points[pointsSize];
  // int iPoint       = 0;
  // points[iPoint++] = nAxisY;

  fPoint.clear();
  fAxes.clear();
  fAxesTypes.clear();
  fAxesLabels.clear();
  fAxesBinSizes.clear();
  for (int iDim = 0; iDim < fResultHnSparse->GetNdimensions(); iDim++) {
    TAxis * a = (TAxis *)fResultHnSparse->GetAxis(iDim);
    if (a == nullptr) {
      Printf("Error: Axis 'id=%d' was not found !!!", iDim);
      return false;
    }
    fPoint.push_back(-1);
    std::string axisName = a->GetName();
    fAxes.push_back(axisName);
    if (a->IsAlphanumeric()) {
      for (int iLabel = 0; iLabel < a->GetNbins(); iLabel++) {
        fAxesLabels[axisName].push_back(a->GetBinLabel(iLabel));
      }
    }
    fAxesBinSizes[axisName] = a->GetNbins();
    fAxesTypes.push_back(fMapAxesType->GetXaxis()->GetBinLabel(iDim + 1));
  }

  if (fCurrentParameterName.empty()) {
    int idxDefault        = gCfg["ndmspc"]["result"]["parameters"]["default"].get<int>();
    fCurrentParameterName = gCfg["ndmspc"]["result"]["parameters"]["labels"][idxDefault].get<std::string>();
  }
  Printf("Paremeter: %s", fCurrentParameterName.c_str());

  TAxis * a = (TAxis *)fResultHnSparse->GetListOfAxes()->FindObject(fParametesAxisName.c_str());
  if (a == nullptr) {
    Printf("Error: Axis '%s' was not found !!!", fParametesAxisName.c_str());
    return false;
  }
  Int_t id    = fResultHnSparse->GetListOfAxes()->IndexOf(a);
  Int_t idBin = a->FindBin(fCurrentParameterName.c_str());
  if (idBin < 0) {
    Printf("Could not find bin label '%s' in '%s' axis !!!", fParametesAxisName.c_str(), fCurrentParameterName.c_str());
    return false;
  }

  Printf("Axis: %d [parameters] SetRange(%d,%d)", id, idBin, idBin);
  fPoint[id] = idBin;
  fResultHnSparse->GetAxis(id)->SetRange(idBin, idBin);

  // int iAxisStart = 1;
  // json axesArray = gCfg["ndmspc"]["result"]["axes"];
  // int  idTmp;
  // bool isDataSys = true;
  // bool hasDataMc = false;
  // for (int iAxis = iAxisStart; iAxis < fResultHnSparse->GetNdimensions(); iAxis++) {
  //   idBin                = 1;
  //   std::string axisType = fMapAxesType->GetXaxis()->GetBinLabel(iAxis + 1);
  //   // Printf("Axis: %d [%s]", iAxis, axisType.c_str());
  //   if (!hasDataMc) hasDataMc = !axisType.compare("data");
  //   if (!axisType.compare("proj")) {
  //     isDataSys = false;
  //     fProjectionAxes.push_back(iAxis);
  //   }
  //   else if (!axisType.compare("sys") || !axisType.compare("data")) {
  //     if (isDataSys) {
  //       idTmp = iAxis - iAxisStart;
  //       idBin = gCfg["ndmspc"]["result"]["data"]["defaults"][idTmp].get<int>();
  //     }
  //     else {
  //       idTmp = iAxis - iAxisStart - fNDimCuts;
  //       if (histogramEnabled) idTmp -= gCfg["ndmspc"]["result"]["data"]["defaults"].size();
  //       // Printf("%s %d", axesArray.dump().c_str(), idTmp);
  //       idBin = axesArray[idTmp]["default"].get<int>() + 1;
  //     }
  //   }
  //   a = (TAxis *)fResultHnSparse->GetAxis(iAxis);
  //   Printf("Axis: %d [%s][%s] SetRange(%d,%d)", iAxis, axisType.c_str(), a->GetName(), idBin, idBin);
  //   points[iAxis]          = a->GetNbins();
  //   fParameterPoint[iAxis] = idBin;
  //   fResultHnSparse->GetAxis(iAxis)->SetRange(idBin, idBin);
  //   std::string l = a->GetBinLabel(idBin);
  //   if (l.empty()) {
  //     fMapTitle += std::to_string(a->GetBinLowEdge(idBin));
  //   }
  //   else {
  //     fMapTitle += l;
  //   }
  //   fMapTitle += " ";
  // }
  // fMapTitle[fMapTitle.size() - 1] = ']';
  //
  // for (auto & p : fParameterPoint) {
  //   printf("%d ", p);
  // }
  // printf("\n");
  //
  // Printf("fMapTitle='%s'", fMapTitle.c_str());
  GenerateTitle();
  ApplyPoints();

  Print();
  return true;
}

bool Results::ApplyPoints()
{
  ///
  /// Apply point
  ///
  Printf("Apply points ...");

  int iAxis = 0;
  for (auto & p : fPoint) {
    if (p < 0) {
      // Printf("Warning: Point[%d] is %d !!! Setting it to 1", iAxis, p);
      p = 1;
    }
    Printf("ApplyPoint : %s [%s] SetRange(%d,%d)", fAxes[iAxis].c_str(), fAxesTypes[iAxis].c_str(), p, p);
    fResultHnSparse->GetAxis(iAxis)->SetRange(p, p);
    iAxis++;
  }

  return true;
}

void Results::GenerateTitle()
{
  ///
  /// Generate title
  ///
  fMapTitle = fCurrentParameterName + " [";

  TAxis * a;
  for (int iAxis = 1; iAxis < fResultHnSparse->GetNdimensions(); iAxis++) {
    a             = (TAxis *)fResultHnSparse->GetAxis(iAxis);
    std::string l = a->GetBinLabel(fPoint[iAxis]);
    if (l.empty()) {
      fMapTitle += std::to_string(fPoint[iAxis]);
    }
    else {
      fMapTitle += l;
    }
    fMapTitle += " ";
  }

  fMapTitle += "]";

  Printf("Map title: '%s'", fMapTitle.c_str());
}

void Results::Draw(Option_t * option)
{
  ///
  /// Draw results
  ///

  Printf("Draw results ...");
  LoadResults();
}
} // namespace Ndmspc
