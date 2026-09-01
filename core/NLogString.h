#ifndef Ndmspc_NLogString_H
#define Ndmspc_NLogString_H

#include <cstdarg>

#include <TNamed.h>
#include <TString.h>

namespace Ndmspc {

///
/// \class NLogString
///
/// \brief Named, multi-line log string storable in a TList (unlike TObjString, has Name/Title)
///	\author Martin Vala <mvala@cern.ch>
///

class NLogString : public TNamed {
  public:
  NLogString(const char * name = "log", const char * title = "Default log");
  virtual ~NLogString();

#if defined(__GNUC__)
  void AddLine(const char * fmt, ...) __attribute__((format(printf, 2, 3)));
#else
  void AddLine(const char * fmt, ...);
#endif

  /// Same as AddLine(), but takes an already-started va_list (for callers forwarding their own varargs).
  void AddLineV(const char * fmt, va_list args);

  const char * GetString() const { return fString.Data(); }
  void         SetString(const char * str) { fString = str; }

  const char * GetType() const { return fType.Data(); }
  void         SetType(const char * type) { fType = type; }

  /// Clear the accumulated content, keeping Name/Title untouched.
  void Reset() { fString.Clear(); }

  private:
  TString fString;       ///< Accumulated log content (lines joined by '\n')
  TString fType{"txt"};  ///< Content type (e.g. "txt", "json", "md"), defaults to "txt"

  /// \cond CLASSIMP
  ClassDef(NLogString, 2);
  /// \endcond;
};
} // namespace Ndmspc
#endif
