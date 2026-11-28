#include <string>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>
#include <ndmspc/core/NGnTree.h>

void NParamsImport(
    const std::string & filename =
      "root://eosuser.cern.ch//eos/user/m/mvala/NDMSPC/tutorials/alice-20261126/alice/rsn/systematics_flat/pid_selection-default/PAR.root",
    const std::string & jsonFile = "")
{
  ///
  /// Macro to import parameters in THnSparse object to NGnTree
  ///
  /// `filename` is the input file. `jsonFile` is the whole configuration; when empty, the
  /// `NParamsImport.json` next to this macro is used. There is **no merging** — the file is the full
  /// configuration. An empty `outputFile` writes next to the input (`PAR.root` -> `PAR_ngnt.root`).
  ///

  const std::string macroDir = Ndmspc::NUtils::CurrentMacroDirectory();
  const std::string cfgFile =
      jsonFile.empty() ? (macroDir.empty() ? "NParamsImport.json" : macroDir + "/NParamsImport.json") : jsonFile;

  json cfg;
  if (!Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, macroDir)) {
    NLogError("NParamsImport: Cannot load configuration '%s' !!!", cfgFile.c_str());
    return;
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(cfg, filename);
  if (ngnt == nullptr) {
    NLogError("NParamsImport: Failed to import '%s' !!!", filename.c_str());
    return;
  }
  delete ngnt;
}
