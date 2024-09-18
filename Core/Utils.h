#ifndef NdmspcCoreUtils_H
#define NdmspcCoreUtils_H

#include <TAxis.h>
#include <TMacro.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace NdmSpc {
class Utils {
  public:
  static std::string OpenRawFile(std::string filename);
  static TMacro *    OpenMacro(std::string filename);
  static int SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                                 bool normalizeToWidth = false, bool onlyPositive = false, double times = 1);
}; // namespace Utils
} // namespace NdmSpc
#endif
