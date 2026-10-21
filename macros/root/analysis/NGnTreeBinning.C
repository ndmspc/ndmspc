#include <string>
#include <sstream>
#include <vector>
#include <TSystem.h>
#include <TInterpreter.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>

// Thin wrapper: the implementation lives in Ndmspc::NUtils::NGnTreeBinning, which takes the
// configuration as a json object. The defaults are written out here (same values as
// Ndmspc::NGnTreeBinningDefaults). An empty cfgFile uses them as they are; a given path is
// resolved relative to the run location and then to the macro directory, and merged over the
// defaults.
void NGnTreeBinning(std::string cfgFile = "", std::string outFile = "", std::string objectsCsv = "")
{
  const std::string macroDir = gSystem->DirName(gInterpreter->GetCurrentMacroName());

  json cfg = {
      {"inputFile", ""},
      {"objectDirectory", ""},
      {"objects", json::array({"*"})},
      {"excludeObjects", json::array()},
      {"axes", json::array()},
      {"binnings", json::object()},
      {"projectionAxis", 0},
      {"outFile", "binning_ngnt.root"},
      {"processes", 2},
      {"mode", "ipc"},
      {"port", 5555},
      {"stallTimeout", 3600},
  };

  if (!cfgFile.empty() && !Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, macroDir)) {
    NLogError("NGnTreeBinning: cannot load configuration '%s'", cfgFile.c_str());
    return;
  }

  if (!outFile.empty()) cfg["outFile"] = outFile;

  if (!objectsCsv.empty()) {
    std::vector<std::string> objects = cfg.value("objects", std::vector<std::string>{});
    std::stringstream        ss(objectsCsv);
    std::string              token;
    while (std::getline(ss, token, ',')) {
      const size_t first = token.find_first_not_of(" \t");
      const size_t last  = token.find_last_not_of(" \t");
      if (first == std::string::npos) continue;
      objects.push_back(token.substr(first, last - first + 1));
    }
    cfg["objects"] = objects;
  }

  Ndmspc::NUtils::NGnTreeBinning(cfg, macroDir);
}
