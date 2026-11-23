#include <string>
#include <vector>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>
#include <ndmspc/core/NGnTree.h>



// ndmspc-run -n 22:1 macros/ndmspc/import/NParamsImportMulti.C --macro-params '"PAR_ngnt_multi.root","root://eosuser.cern.ch//eos/user/m/mvala/NDMSPC/tutorials/alice-20261126/alice/rsn/systematics_multi","PAR_ngnt.root",{"dca","ncls","vtzx","pid","bg","bgNorm","fitFun","fitRange"}'
void NParamsImportMulti(
    const std::string & outputFile = "PAR_ngnt_flat.root",
    const std::string & prefix  = "root://eosuser.cern.ch//eos/user/m/mvala/NDMSPC/tutorials/alice-20261126/alice/rsn/systematics_flat",
    const std::string & postfix = "PAR_ngnt.root", const std::vector<std::string> & headers = {"variations"},
    bool force = false)
{
  ///
  /// Import every `<prefix>/<headers…>/<postfix>` into one NGnTree
  ///
  /// Each file found is **already** an NGnTree — the `PAR.root` -> `PAR_ngnt.root` step is
  /// `NParamsConvertImportMulti.C`'s, and nothing is converted here. `NUtils::Find` discovers the
  /// trees and `ImportNgnt` combines them, the directory levels named by `headers` becoming axes: one
  /// point is one file. An existing output is skipped unless @p force is set.
  ///

  std::string base = prefix;
  while (!base.empty() && base.back() == '/') base.pop_back();

  std::vector<std::string> paths = Ndmspc::NUtils::Find(base, postfix);
  NLogInfo("NParamsImportMulti: Found %zu files to import ...", paths.size());
  if (paths.empty()) {
    NLogError("NParamsImportMulti: No files '%s' under '%s' !!!", postfix.c_str(), base.c_str());
    return;
  }

  std::string out = outputFile;
  if (out.empty()) {
    const std::string::size_type dot  = postfix.find_last_of('.');
    const std::string            stem = (dot == std::string::npos ? postfix : postfix.substr(0, dot));
    out = base + "/" + (stem.find('*') == std::string::npos ? stem : std::string("multi")) + "_multi.root";
  }
  if (!force && Ndmspc::NUtils::AccessPathName(out)) {
    NLogForce("[INFO] NParamsImportMulti: Output '%s' already exists, skipping (force to overwrite) ...", out.c_str());
    return;
  }

  // The parameters are the ones the converted files carry; take their names from the first one.
  std::vector<std::string> paramNames;
  Ndmspc::NGnTree *        first = Ndmspc::NGnTree::Open(paths.front().c_str());
  if (first != nullptr) {
    if (first->GetParameters() != nullptr) paramNames = first->GetParameters()->GetNames();
    delete first;
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::ImportNgnt(base, postfix, headers, paramNames, {}, out);
  if (ngnt == nullptr) {
    NLogError("NParamsImportMulti: Failed to combine the imported files into '%s' !!!", out.c_str());
    return;
  }
  delete ngnt;
}
