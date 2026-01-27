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

  NGnWsHandler * GetWebSocketHandler() const { return fNWsHandler; }
  void           SetNGnTree(NGnTree * tree) { fNGnTree = tree; }

  protected:
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  private:
  NGnWsHandler * fNWsHandler{nullptr}; ///< WebSocket handler instance
  NGnTree *      fNGnTree{nullptr};    ///< NGnTree instance
  NGnNavigator * fNavigator{nullptr};  ///< NGnNavigator instance

  /// \cond CLASSIMP
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
