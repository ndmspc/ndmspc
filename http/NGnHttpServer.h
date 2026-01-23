#ifndef Ndmspc_NMonHttpServer_H
#define Ndmspc_NMonHttpServer_H
#include "NHttpServer.h"
#include "NGnWsHandler.h"

namespace Ndmspc {

///
/// \class NGnHttpServer
///
/// \brief NGnHttpServer object
///	\author Martin Vala <mvala@cern.ch>
///

class NGnHttpServer : public NHttpServer {
  public:
  NGnHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000);

  NGnWsHandler * GetWebSocketHandler() const { return fNWsHandler; }

  protected:
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg);

  private:
  NGnWsHandler * fNWsHandler{nullptr}; ///< WebSocket handler instance

  /// \cond CLASSIMP
  ClassDef(NGnHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
