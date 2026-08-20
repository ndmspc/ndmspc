#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <NSystematicsStats.h>
#include <ndmspc/hep/NAnalysisUtils.h>
#include <ndmspc/core/NUtils.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>
#include <TF1.h>

void NSystematics02GausCfg(const std::string & cfgFile = "tutorial/08_systematics/sys.json")
{
  Ndmspc::NAnalysisUtils::ProcessSystematics(cfgFile);
}
