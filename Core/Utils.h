#ifndef NdmspcCoreUtils_H
#define NdmspcCoreUtils_H

#include <TAxis.h>
#include <TMacro.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
#include <string>
using json = nlohmann::json;

namespace NdmSpc {
class Utils {
  public:
  static std::string OpenRawFile(std::string filename);
  static TMacro *    OpenMacro(std::string filename);
  static json &      LoadConfig(std::string config, std::string userConfig);
  static json &      Cfg() { return fCfg; }
  static void        RebinBins(int & min, int & max, int rebin);
  static int SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                                 bool normalizeToWidth = false, bool onlyPositive = false, double times = 1);
  static std::vector<std::string> Tokenize(std::string_view input, const char delim);

  private:
  static json fCfg;
}; // namespace Utils
} // namespace NdmSpc
#endif
