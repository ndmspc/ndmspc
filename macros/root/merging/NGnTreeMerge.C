#include <string>
#include <TSystem.h>
#include <TInterpreter.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>

// Thin wrapper: the implementation lives in Ndmspc::NUtils::NGnTreeMerge, which takes the
// configuration as a json object. The defaults are written out here (same values as
// Ndmspc::NGnTreeMergeDefaults). An empty cfgFile uses them as they are; a given path is resolved
// relative to the run location and then to the macro directory, and merged over the defaults.
void NGnTreeMerge(std::string cfgFile = "", std::string outFile = "", std::string objectsCsv = "",
                  bool deletePartials = false)
{
  const std::string macroDir = gSystem->DirName(gInterpreter->GetCurrentMacroName());

  json cfg = {
      {"inputFile", ".ndmspc/merge"},
      {"outFile", "merged_ngnt.root"},
      {"objects", json::array({"*"})},
      {"excludeObjects", json::array()},
      {"mergeTypes", json::array({"TH1", "TH2", "TH3", "TProfile", "THnSparse", "TEfficiency"})},
      {"onUnsupported", "keepFirst"},
      {"force", false},
      {"reset", false},
      {"combine", true},
      {"combineChunks", false},
      {"serializeBytes", 67108864},
      {"processes", 2},
      {"mode", "ipc"},
      {"port", 5555},
      {"stallTimeout", 3600},
  };

  if (!cfgFile.empty() && !Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, macroDir)) {
    NLogError("NGnTreeMerge: cannot load configuration '%s'", cfgFile.c_str());
    return;
  }

  Ndmspc::NUtils::NGnTreeMerge(cfg, outFile, objectsCsv, deletePartials, macroDir);
}
