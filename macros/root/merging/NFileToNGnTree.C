#include <string>
#include <TSystem.h>
#include <TInterpreter.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>

// Thin wrapper: the implementation lives in Ndmspc::NUtils::FileToNGnTree, which takes the
// configuration as a json object. The defaults are written out here (same values as
// Ndmspc::NFileToNGnTreeDefaults). An empty cfgFile uses them as they are; a given path is resolved
// relative to the run location and then to the macro directory, and merged over the defaults.
void NFileToNGnTree(std::string cfgFile = "", std::string outFile = "", std::string filesTxt = "",
                    std::string objectsCsv = "")
{
  const std::string macroDir = gSystem->DirName(gInterpreter->GetCurrentMacroName());

  json cfg = {
      {"outFile", "merging_ngnt.root"},
      {"files", json::array()},
      {"objects", json::array({"*"})},
      {"split", true},
      {"splitBase", ".ndmspc/merge"},
      {"chunkAxes", json::array()},
      {"chunkMinBytes", 67108864},
      {"reset", false},
  };

  if (!cfgFile.empty() && !Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, macroDir)) {
    NLogError("NFileToNGnTree: cannot load configuration '%s'", cfgFile.c_str());
    return;
  }

  Ndmspc::NUtils::FileToNGnTree(cfg, outFile, filesTxt, objectsCsv, macroDir);
}
