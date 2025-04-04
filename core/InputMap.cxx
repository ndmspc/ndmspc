#include <TString.h>
#include <TSystem.h>
#include <string>
#include <vector>
#include <THnSparse.h>
#include "Config.h"
#include "Utils.h"
#include "InputMap.h"
#include "Logger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::InputMap);
/// \endcond

namespace Ndmspc {
InputMap::InputMap(json config)
{
  ///
  /// Constructor
  ///

  fMapFileName = config["file"].get<std::string>();
  fMapObjName  = config["obj"].get<std::string>();
  fBaseDir     = config["base"].get<std::string>();
  fFileName    = config["filename"].get<std::string>();
}
InputMap::InputMap(std::string mapFileName, std::string mapObjName, std::string baseDir, std::string fileName)
    : fMapFileName(mapFileName), fMapObjName(mapObjName), fBaseDir(baseDir), fFileName(fileName)
{
  ///
  /// Constructor
  ///
}
InputMap::~InputMap()
{
  ///
  /// Destructor
  ///
}
bool InputMap::LoadMap()
{
  Printf("Opening map file '%s' ...", fMapFileName.c_str());
  TFile * file = Utils::OpenFile(fMapFileName.c_str());
  if (!file) {
    Printf("Cannot open file '%s'", fMapFileName.c_str());
    return false;
  }
  fMap = dynamic_cast<THnSparse *>(file->Get(fMapObjName.c_str()));
  if (!fMap) {
    Printf("Cannot get object '%s' from file '%s'", fMapObjName.c_str(), fMapFileName.c_str());
    return false;
  }

  return true;
}
THnSparse * InputMap::Query(const std::string & outputPath)
{
  ///
  /// Query content of the map
  ///

  Config & c = Config::Instance();

  Printf("Querying map '%s' with filname '%s'...", fBaseDir.c_str(), fFileName.c_str());

  std::vector<std::string>      paths = Utils::Find(fBaseDir, fFileName);
  std::vector<std::vector<int>> bins;

  for (auto & p : paths) {
    p.erase(p.find(fFileName), fFileName.length());
    p.erase(p.find(fBaseDir), fBaseDir.length());
    Printf("Path: %s", p.c_str());
    std::vector<int> bin = Utils::TokenizeInt(p, '/');
    bins.push_back(bin);
  }

  THnSparse * mapFilled = (THnSparse *)fMap->Clone("mapFilled");
  if (!mapFilled) {
    Printf("Cannot clone map '%s'", fMap->GetName());
    return nullptr;
  }
  int pointSize = bins[0].size();
  int point[pointSize];
  // fMap->Reset();
  for (auto & b : bins) {
    for (int i = 0; i < pointSize; i++) {
      if (i < 2)
        point[i] = b[i];
      else
        point[i] = b[i];
    }
    mapFilled->SetBinContent(point, 1);
    fMap->SetBinContent(point, 1);
  }
  std::string binsStr = "[";
  for (int i = 0; i < mapFilled->GetNbins(); i++) {

    mapFilled->GetBinContent(i, point);
    binsStr += "[";
    for (int j = 0; j < mapFilled->GetNdimensions(); j++) {
      binsStr += std::to_string(point[j]) + ",";
    }
    binsStr.pop_back();
    binsStr += "],";
  }
  binsStr.pop_back();
  binsStr += "]";
  Printf("%s", binsStr.c_str());
  TFile * f = TFile::Open(TString::Format("%s/mapFilled.root", outputPath.c_str()).Data(), "RECREATE");
  mapFilled->Write();
  f->Close();
  Printf("File was saved at '%s' ...", f->GetName());

  // std::vector<std::string> paths = Utils::Find(path, cfg["ndmspc"]["output"]["file"].get<std::string>());
  // // TODO: Filter axis projections
  // std::set<std::string> projections = Utils::Unique(paths, 0, path);
  // // TODO: Filter binnings
  // std::set<std::string> binnings = Utils::Unique(paths, 1, path);
  //
  // fProjections.clear();
  // int i = 0;
  // for (auto & p : projections) {
  //   std::vector<std::string> tp = Utils::Tokenize(p, '_');
  //   for (auto & b : binnings) {
  //
  //     std::vector<std::string> tb = Utils::Tokenize(b, '_');
  //     if (tp.size() == tb.size()) {
  //       fProjections[p].push_back(b);
  //     }
  //   }
  //   i++;
  // }
  //
  return nullptr;
}

THnSparse * InputMap::Generate(std::vector<std::vector<int>> bins)
{
  ///
  /// Generate THnSparse from bins
  ///

  if (bins.empty()) {
    // NOTE: Running all bins
    Printf("Generating THnSparse from all bins (not yet implemented). Skipping ...");
    return nullptr;
  }
  else {
    // NOTE: Running only selected bins
  }
  return nullptr;
}

void InputMap::Print(Option_t * option) const
{
  ///
  /// Print InputMap information
  ///

  std::string opt = option;

  Printf("InputMap:");
  Printf("  MapFileName: '%s'", fMapFileName.c_str());
  Printf("  MapObj: '%s'", fMapObjName.c_str());
  Printf("  BaseDir: '%s'", fBaseDir.c_str());
  Printf("  FileName: '%s'", fFileName.c_str());
  if (fMap) {
    Printf("  Map:");
    Printf("    Dimensions: %d", fMap->GetNdimensions());
    for (int i = 0; i < fMap->GetNdimensions(); i++) {
      Printf("    Axis %d: %s", i, fMap->GetAxis(i)->GetName());
      if (fMap->GetAxis(i)->GetLabels()) {
        Printf("      Labels:");
        std::string labels;
        for (int j = 2; j <= fMap->GetAxis(i)->GetNbins(); j++) {
          labels += TString::Format("%s,", fMap->GetAxis(i)->GetBinLabel(j));
        }
        labels.pop_back();
        Printf("      [%s]", labels.c_str());
      }
      else {
        Printf("      Bins: %d range: [%.3f,%.3f] ", fMap->GetAxis(i)->GetNbins(), fMap->GetAxis(i)->GetXmin(),
               fMap->GetAxis(i)->GetXmax());
      }
    }
    if (!fProjections.empty()) {
      for (auto & p : fProjections) {
        Printf("  Projection: '%s'", p.first.c_str());
        for (auto & b : p.second) {
          Printf("    Binning: '%s'", b.c_str());
        }
      }
    }
  }
}

std::string InputMap::GetInputFileName(int index)
{
  ///
  /// Get input filename
  ///

  auto  logger = Logger::getInstance("");
  Int_t point[fMap->GetNdimensions()];
  fMap->GetBinContent(index, point);
  std::string path = fBaseDir + '/';
  for (int i = 0; i < fMap->GetNdimensions(); i++) {
    path += std::to_string(point[i]) + "/";
  }
  path += fFileName;
  logger->Trace("Path: %s", path.c_str());

  return path;
}

} // namespace Ndmspc
