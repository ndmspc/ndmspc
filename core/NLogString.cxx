#include <cstdarg>
#include <cstdio>
#include <vector>

#include "NLogString.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NLogString);
/// \endcond

namespace Ndmspc {
NLogString::NLogString(const char * name, const char * title) : TNamed(name, title) {}
NLogString::~NLogString() {}

void NLogString::AddLine(const char * fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  AddLineV(fmt, args);
  va_end(args);
}

void NLogString::AddLineV(const char * fmt, va_list args)
{
  va_list argsCopy;
  va_copy(argsCopy, args);
  int neededSize = vsnprintf(nullptr, 0, fmt, argsCopy);
  va_end(argsCopy);

  std::vector<char> buf(neededSize < 0 ? 1 : neededSize + 1);
  if (neededSize >= 0) vsnprintf(buf.data(), buf.size(), fmt, args);

  TString line(buf.data());

  if (fString.IsNull()) {
    fString = line;
  }
  else {
    fString += "\n";
    fString += line;
  }
}
} // namespace Ndmspc
