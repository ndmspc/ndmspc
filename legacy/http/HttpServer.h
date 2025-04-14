#ifndef NdmspcCoreHttpServer_H
#define NdmspcCoreHttpServer_H

#include <THttpServer.h>
#include "CloudEvent.h"
#include "WebSocketHandler.h"

class THttpCallArg;
namespace Ndmspc {

///
/// \class HttpServer
///
/// \brief HttpServer object
///	\author Martin Vala <mvala@cern.ch>
///
///
class HttpServer : public THttpServer {
  public:
  HttpServer(const char * engine = "http:8080", bool ws = true);

  WebSocketHandler * GetWebSocketHandler() const { return fWebSocketHandler; }

  protected:
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg);
  virtual void ProcessCloudEventRequest(CloudEvent * ce, std::shared_ptr<THttpCallArg> arg);

  private:
  WebSocketHandler * fWebSocketHandler{nullptr};

  /// \cond CLASSIMP
  ClassDef(HttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
