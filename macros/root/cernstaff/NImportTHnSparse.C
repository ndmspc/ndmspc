#include <string>
#include <TSystem.h>
#include <TInterpreter.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>

void NImportTHnSparse(std::string jsonFile = "import_cernstaff.json", std::string filename = "cernstaff.root")
{
  std::string macroDir = gSystem->DirName(gInterpreter->GetCurrentMacroName());

  if (!Ndmspc::NUtils::AccessPathName(jsonFile)) {
    std::string alt = macroDir + "/" + jsonFile;
    if (Ndmspc::NUtils::AccessPathName(alt)) jsonFile = alt;
  }
  if (!Ndmspc::NUtils::AccessPathName(filename)) {
    std::string alt = macroDir + "/" + filename;
    if (Ndmspc::NUtils::AccessPathName(alt)) filename = alt;
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonFile, filename);
  if (ngnt == nullptr) {
    NLogError("NImportTHnSparse: Import of '%s' failed !!!", filename.c_str());
    return;
  }

  ngnt->Print();
}
