#include <TH1.h>
#include "Logger.h"
#include "Config.h"
#include "AnalysisUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AnalysisUtils);
/// \endcond

namespace Ndmspc {

bool AnalysisUtils::Init()
{
  ///
  /// Initialize the analysis
  ///
  TH1::AddDirectory(kFALSE);

  auto logger = Ndmspc::Logger::getInstance("");
  auto c      = Ndmspc::Config::Instance();
  if (c == nullptr) {
    logger->Error("Cannot get config instance");
    return false;
  }

  c->Print();

  // Search all paths via find and print them
  std::vector<std::string>      paths = Utils::Find(c->GetAnalysisInputPath(), "AnalysisResults.root");
  std::vector<std::vector<int>> bins;

  for (auto & p : paths) {
    // p.erase(p.find(fFileName), fFileName.length());
    // p.erase(p.find(fBaseDir), fBaseDir.length());
    logger->Info("Path: %s", p.c_str());
    // std::vector<int> bin = Utils::TokenizeInt(p, '/');
    // bins.push_back(bin);
  }

  return true;
}

} // namespace Ndmspc
