#ifndef Ndmspc_NGnHttpServer_H
#define Ndmspc_NGnHttpServer_H
#include "NHttpServer.h"
#include "NGnWsHandler.h"
#include "NGnHistoryEntry.h"

namespace Ndmspc {

/**
 * @brief Function pointer type for HTTP handlers.
 *
 * The handler function takes the following parameters:
 * - std::string: The HTTP request path or identifier.
 * - json&: Reference to the input JSON payload.
 * - json&: Reference to the output JSON payload.
 * - std::map<std::string, TObject*>&: Reference to a map of named TObject pointers.
 */
using NGnHttpFuncPtr = void (*)(std::string, json &, json &, std::map<std::string, TObject *> &);

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

class NGnHttpServer : public NHttpServer {
  public:
  NGnHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000);

  virtual void Print(Option_t * option = "") const override;

  void SetHttpHandlers(std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers) { fHttpHandlers = handlers; }
  bool WebSocketBroadcast(json message);

  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  void      AddInputObject(const std::string & name, TObject * obj) { fObjectsMap[name] = obj; }
  bool      RemoveInputObject(const std::string & name);
  TObject * GetInputObject(const std::string & name);

  void AddHistoryEntry(NGnHistoryEntry * entry);
  bool RemoveHistoryEntry(int index);
  void ClearHistory();

  bool LoadHistoryFromFile(const std::string & filename);
  bool ExportHistoryToFile(const std::string & filename) const;

  private:
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> fHttpHandlers; ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;   ///<! Objects map for handlers
  std::vector<NGnHistoryEntry *>                fHistory;      ///<! History entries

  /// \cond CLASSIMP
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
