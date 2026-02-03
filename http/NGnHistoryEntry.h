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

  void SetConfig(const json & config) { fConfig = config; }
  json GetConfig() { return fConfig; }

  private:
  json fConfig; ///< Configuration JSON object

  /// \cond CLASSIMP
  ClassDef(NGnHistoryEntry, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
