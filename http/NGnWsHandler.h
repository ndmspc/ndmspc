#ifndef NdmspcNMonWsHandler_H
#define NdmspcNMonWsHandler_H
#include <map>    // For std::map
#include <string> // For std::string
#include <mutex>  // For std::mutex
#include <cstdio>
#include <TString.h>
#include <THttpCallArg.h> // For THttpCallArg from ROOT
#include "NWsHandler.h"

class THttpCallArg;
class TTimer;
namespace Ndmspc {

class NGnWsHandler : public NWsHandler {
  public:
  NGnWsHandler(const char * name = nullptr, const char * title = nullptr);
  Bool_t ProcessWS(THttpCallArg * arg) override;

  /// \cond CLASSIMP
  ClassDefOverride(NGnWsHandler, 1);
  /// \endcond;
};

} // namespace Ndmspc
#endif
