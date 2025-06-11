#include <string>
#include <TH1.h>
#include "NConfig.h"
#include "NHnSparseTreeUtils.h"
void Import(std::string config = "PWGLF-376-dev.json")
{
  TH1::AddDirectory(kFALSE);
  Ndmspc::NConfig * cfg = Ndmspc::NConfig::Instance(config);
  cfg->Print();

  std::string hnstFileNameIn = cfg->GetAnalysisBasePath() + "/inputs/hnst.root";
  // std::string hnstFileNameOut = cfg->GetAnalysisPath() + "/hnst.root";
  std::string hnstFileNameOut = "/tmp/hnst.root";
  Ndmspc::NHnSparseTreeUtils::Reshape(hnstFileNameIn, cfg->GetInputObjectNames(), cfg->GetInputObjectDirecotry(),
                                      hnstFileNameOut);
}
