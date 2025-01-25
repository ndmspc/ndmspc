#ifndef NdmspcWebSocketHandler_H
#define NdmspcWebSocketHandler_H
#include "THttpWSHandler.h"
#include "TString.h"

#include <cstdio>

class THttpCallArg;
class TTimer;
namespace Ndmspc {

///
/// \class WebSocketHandler
///
/// \brief WebSocketHandler object
///	\author Martin Vala <mvala@cern.ch>
///
class WebSocketHandler : public THttpWSHandler {
  public:
  UInt_t fWSId{0};
  Int_t  fServCnt{0};

  WebSocketHandler(const char * name = nullptr, const char * title = nullptr);
  virtual ~WebSocketHandler();

  // load custom HTML page when open correspondent address
  TString GetDefaultPageContent() override { return "file:ws.htm"; }

  Bool_t ProcessWS(THttpCallArg * arg) override;

  /// per timeout sends data portion to the client
  Bool_t HandleTimer(TTimer *) override;
};

} // namespace Ndmspc
#endif
