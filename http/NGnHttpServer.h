#ifndef Ndmspc_NGnHttpServer_H
#define Ndmspc_NGnHttpServer_H
#include "NHttpServer.h"
#include "NGnNavigator.h"
#include "NGnTree.h"
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

  virtual void Print(Option_t * option = "") const override;

  void SetHttpHandlers(std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers) { fHttpHandlers = handlers; }
  bool WebSocketBroadcast(json message);

  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  private:
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> fHttpHandlers; ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;   ///<! Objects map for handlers

  /// \cond CLASSIMP
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
