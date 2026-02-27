#ifndef Ndmspc_NGnHttpServer_H
#define Ndmspc_NGnHttpServer_H
#include "NLogger.h"
#include "NHttpServer.h"
// #include "NGnHistoryEntry.h"
#include "NGnWorkspace.h"
#include "NGnWorkspace.h"

namespace Ndmspc {

/**
 * @brief Function pointer type for HTTP handlers.
 *
 * The handler function takes the following parameters:
 * - std::string: The HTTP request path or identifier.
 * - json&: Reference to the input JSON payload.
 * - json&: Reference to the output JSON payload.
 * - json&: Reference to the output JSON payload to websocket.
 */
using NGnHttpFuncPtr = void (*)(std::string, json &, json &, json &, std::map<std::string, TObject *> &);

/**
 * @brief Map of HTTP handler names to their corresponding function pointers.
 */
using NGnHttpHandlerMap = std::map<std::string, NGnHttpFuncPtr>;

/**
 * @brief Global pointer to the HTTP handler map.
 */
extern NGnHttpHandlerMap * gNdmspcHttpHandlers;

///
/// \class NGnHttpServer
///
/// \brief NGnHttpServer object
///	\author Martin Vala <mvala@cern.ch>
///
class NGnHistoryEntry;
class NGnHistory;
class NGnHttpServer : public NHttpServer {

  public:
  NGnHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000);

  virtual void Print(Option_t * option = "") const override;
  virtual void Clear(Option_t * option = "") override { NHttpServer::Clear(option); }
  void         ClearHistory() { fWorkspace.Clear(); }

  json GetJson() const;

  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  void SetHttpHandlers(std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers) { fHttpHandlers = handlers; }

  void      AddInputObject(const std::string & name, TObject * obj) { fObjectsMap[name] = obj; }
  bool      RemoveInputObject(const std::string & name);
  TObject * GetInputObject(const std::string & name);

  std::map<std::string, Ndmspc::NGnHttpFuncPtr> GetHttpHandlers() const { return fHttpHandlers; }
  std::map<std::string, TObject *> &            GetObjectsMap() { return fObjectsMap; }
  json &                                        GetWorkspace() { return fWorkspace.GetWorkspace(); }
  json &                                        GetState() { return fWorkspace.GetState(); }

  private:
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> fHttpHandlers;       ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;         ///<! Objects map for handlers
  NGnWorkspace                                  fWorkspace{nullptr}; ///<! Workspace object (TNamed)

  /// \cond CLASSIMP
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};

extern NGnHttpServer * gNGnHttpServer;

} // namespace Ndmspc
#endif
