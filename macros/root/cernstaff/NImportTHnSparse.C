#include <string>
#include <TSystem.h>
#include <TInterpreter.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>

void NImportTHnSparse(std::string jsonFile = "import_cernstaff.json", std::string filename = "cernstaff.root")
{
  std::string macroDir = gSystem->DirName(gInterpreter->GetCurrentMacroName());

  if (gSystem->AccessPathName(jsonFile.c_str())) {
    std::string alt = macroDir + "/" + jsonFile;
    if (!gSystem->AccessPathName(alt.c_str())) jsonFile = alt;
  }
  if (gSystem->AccessPathName(filename.c_str())) {
    std::string alt = macroDir + "/" + filename;
    if (!gSystem->AccessPathName(alt.c_str())) filename = alt;
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonFile, filename);
  if (ngnt == nullptr) {
    NLogError("NImportTHnSparse: Import of '%s' failed !!!", filename.c_str());
    return;
  }

  ngnt->Print();
}
