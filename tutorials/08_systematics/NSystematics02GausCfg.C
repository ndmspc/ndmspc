#include <ndmspc/hep/NAnalysisUtils.h>
void NSystematics02GausCfg(const std::string & cfgFile = "tutorials/08_systematics/sys.json")
{
  Ndmspc::NAnalysisUtils::ProcessSystematics(cfgFile);
}
