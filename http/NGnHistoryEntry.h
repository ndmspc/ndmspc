#ifndef Ndmspc_NGnHistoryEntry_H
#define Ndmspc_NGnHistoryEntry_H
#include <TNamed.h>
#include <NUtils.h>

namespace Ndmspc {

///
/// \class NGnHistoryEntry
///
/// \brief NGnHistoryEntry object
///	\author Martin Vala <mvala@cern.ch>
///

class NGnHistoryEntry : public TNamed {
  public:
  NGnHistoryEntry(const char * name = "", const char * title = "");
  virtual ~NGnHistoryEntry();

  json GetPayloadIn() const { return fIn; }
  void SetPayloadIn(const json & payload) { fIn = payload; }
  json GetPayloadOut() const { return fOut; }
  void SetPayloadOut(const json & payload) { fOut = payload; }
  json GetPayloadWsOut() const { return fWsOut; }
  void SetPayloadWsOut(const json & payload) { fWsOut = payload; }

  private:
  json fIn;    ///< Input JSON object
  json fOut;   ///< Output JSON object
  json fWsOut; ///< Websocket output JSON object

  /// \cond CLASSIMP
  ClassDef(NGnHistoryEntry, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
