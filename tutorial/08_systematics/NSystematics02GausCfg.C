#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NSystematicsStats.h>
#include <NAnalysisUtils.h>
#include <NUtils.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>
#include <TF1.h>

void NSystematics02GausCfg(const std::string & cfgFile = "tutorial/08_systematics/sys.json")
{
  Ndmspc::NAnalysisUtils::ProcessSystematics(cfgFile);
}
