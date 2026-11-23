#include <TAxis.h>
#include <TH1.h>
#include <TObjArray.h>
#include <TSystem.h>
#include <map>
#include <string>
#include <vector>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>
#include <ndmspc/core/NGnTree.h>

void NParamsConvertImportMulti(
    const std::string & outputFile = "PAR_ngnt_multi.root",
    const std::string & prefix  = "root://eosuser.cern.ch//eos/user/m/mvala/NDMSPC/tutorials/alice-20261126/alice/rsn/systematics_flat",
    const std::string & postfix = "PAR.root", const std::vector<std::string> & headers = {"variation"},
    const std::string & jsonFile = "", bool force = false)
{
  ///
  /// Import every `<prefix>/<headers…>/<postfix>` into one NGnTree, converting each file first
  ///
  /// `NUtils::Find` discovers the files and `NUtils::AxesFromDirectory` turns the directory levels
  /// named by `headers` into axes: one point is one file. The import is nested the way tutorial 07
  /// does it — a driver tree over those axes runs the per-file import inside its process function —
  /// and `ImportNgnt` then combines the imported files into one tree. Each file is imported with
  /// `jsonFile`; when it is empty the `NParamsImport.json` next to this macro is used, the same
  /// configuration `NParamsImport.C` takes. An existing output (per file and combined) is skipped
  /// unless @p force is set.
  ///
  /// "Converting" is the per-file step: each `<postfix>` is read through `NGnTree::Import` and written
  /// beside itself as `<stem>_ngnt.root`. A tree that is **already** an NGnTree needs no such step —
  /// `NParamsConvertImportMulti.C` combines those as they are.
  ///

  std::string base = prefix;
  while (!base.empty() && base.back() == '/') base.pop_back();

  const std::string macroDir = Ndmspc::NUtils::CurrentMacroDirectory();
  const std::string cfgFile =
      jsonFile.empty() ? (macroDir.empty() ? "NParamsImport.json" : macroDir + "/NParamsImport.json") : jsonFile;

  json cfg;
  if (!Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, macroDir)) {
    NLogError("NParamsConvertImportMulti: Cannot load configuration '%s' !!!", cfgFile.c_str());
    return;
  }

  std::vector<std::string> paths = Ndmspc::NUtils::Find(base, postfix);
  NLogInfo("NParamsConvertImportMulti: Found %zu files to import ...", paths.size());
  if (paths.empty()) {
    NLogError("NParamsConvertImportMulti: No files '%s' under '%s' !!!", postfix.c_str(), base.c_str());
    return;
  }

  // The per-file import writes `<stem>_ngnt.root` next to each input; that name is what the
  // combined import finds under the same directory levels.
  const std::string::size_type dot      = postfix.find_last_of('.');
  const std::string            stem     = (dot == std::string::npos ? postfix : postfix.substr(0, dot));
  const std::string            ngntName = stem + "_ngnt.root";

  std::string out = outputFile;
  if (out.empty()) {
    out = base + "/" + (stem.find('*') == std::string::npos ? stem : std::string("multi")) + "_ngnt_multi.root";
  }
  if (!force && Ndmspc::NUtils::AccessPathName(out)) {
    NLogForce("[INFO] NParamsConvertImportMulti: Output '%s' already exists, skipping (force to overwrite) ...",
              out.c_str());
    return;
  }

  // Directory levels become the driver tree's axes; its process function imports each file.
  TObjArray * dirAxes = Ndmspc::NUtils::AxesFromDirectory(paths, base, postfix, headers, {});
  if (dirAxes == nullptr || dirAxes->GetEntries() == 0) {
    NLogError("NParamsConvertImportMulti: No directory axes for %zu files !!!", paths.size());
    delete dirAxes;
    return;
  }

  std::map<std::string, std::vector<std::vector<int>>> b;
  for (int i = 0; i < dirAxes->GetEntries(); i++) {
    b[((TAxis *)dirAxes->At(i))->GetName()] = {{1}};
  }

  const char *      tmp  = gSystem->TempDirectory();
  const std::string tree = std::string(tmp != nullptr && *tmp != '\0' ? tmp : "/tmp") + "/NParamsConvertImportMulti_" +
                           std::to_string(gSystem->GetPid()) + ".root";

  Ndmspc::NGnTree * nested = new Ndmspc::NGnTree(dirAxes, tree);
  if (nested->IsZombie()) {
    NLogError("NParamsConvertImportMulti: Failed to create the directory tree !!!");
    delete nested;
    return;
  }
  nested->GetBinning()->AddBinningDefinition("default", b);

  // Every file is imported next to itself; a configuration naming one output would make them
  // overwrite each other.
  cfg["outputFile"] = "";

  json nestedCfg;
  nestedCfg["basedir"]  = base;
  nestedCfg["filename"] = postfix;
  nestedCfg["headers"]  = headers;
  nestedCfg["force"]    = force;
  nestedCfg["import"]   = cfg;

  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    json        cfg      = point->GetCfg();
    std::string filename = cfg["basedir"].get<std::string>();
    for (const auto & header : cfg["headers"]) {
      filename += "/" + point->GetBinLabel(header.get<std::string>());
    }
    filename += "/" + cfg["filename"].get<std::string>();

    std::string                  imported = filename;
    const std::string::size_type idot     = imported.find_last_of('.');
    imported = (idot == std::string::npos ? imported : imported.substr(0, idot)) + "_ngnt.root";

    if (!cfg["force"].get<bool>() && Ndmspc::NUtils::AccessPathName(imported)) {
      NLogInfo("[INFO] NParamsConvertImportMulti: '%s' already exists, skipping (force to overwrite) ...",
               imported.c_str());
      return;
    }

    Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(cfg["import"], filename);
    if (ngnt == nullptr) {
      NLogError("NParamsConvertImportMulti: Failed to import '%s' !!!", filename.c_str());
      return;
    }
    outputPoint->Add(new TH1S("outputFile", ngnt->GetStorageTree()->GetFileName().c_str(), 1, 0.5, 1.5));
    delete ngnt;
  };

  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {};
  Ndmspc::NGnEndFuncPtr   endFunc   = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {};

  nested->Process(processFunc, nestedCfg, "", beginFunc, endFunc);
  delete nested;
  gSystem->Unlink(tree.c_str());

  // The parameters are the ones the per-file import wrote; take their names from the first file.
  std::string                  firstImported = paths.front();
  const std::string::size_type fdot          = firstImported.find_last_of('.');
  firstImported = (fdot == std::string::npos ? firstImported : firstImported.substr(0, fdot)) + "_ngnt.root";

  std::vector<std::string> paramNames;
  Ndmspc::NGnTree *        first = Ndmspc::NGnTree::Open(firstImported.c_str());
  if (first != nullptr) {
    if (first->GetParameters() != nullptr) paramNames = first->GetParameters()->GetNames();
    delete first;
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::ImportNgnt(base, ngntName, headers, paramNames, {}, out);
  if (ngnt == nullptr) {
    NLogError("NParamsConvertImportMulti: Failed to combine the imported files into '%s' !!!", out.c_str());
    return;
  }
  delete ngnt;
}
