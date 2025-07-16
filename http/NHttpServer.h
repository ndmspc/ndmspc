#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <THttpServer.h>
#include "NCloudEvent.h"
#include "NWsHandler.h"

class THttpCallArg;
namespace Ndmspc {

///
/// \class NHttpServer
///
/// \brief NHttpServer object
///	\author Martin Vala <mvala@cern.ch>
///
///
class NHttpServer : public THttpServer {
  public:
  NHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000);

  NWsHandler * GetWebSocketHandler() const { return fNWsHandler; }

  protected:
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg);
  virtual void ProcessNCloudEventRequest(NCloudEvent * ce, std::shared_ptr<THttpCallArg> arg);

  private:
  NWsHandler * fNWsHandler{nullptr};

  /// \cond CLASSIMP
  ClassDef(NHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
