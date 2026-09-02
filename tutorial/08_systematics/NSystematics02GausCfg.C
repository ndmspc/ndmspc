#include <ndmspc/hep/NAnalysisUtils.h>
void NSystematics02GausCfg(const std::string & cfgFile = "tutorial/08_systematics/sys.json")
{
  Ndmspc::NAnalysisUtils::ProcessSystematics(cfgFile);
}
