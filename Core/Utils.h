#ifndef NdmspcCoreUtils_H
#define NdmspcCoreUtils_H

#include <TFile.h>
#include <TAxis.h>
#include <TMacro.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class Utils
///
/// \brief Utils object
///	\author Martin Vala <mvala@cern.ch>
///

class Utils : TObject {

  Utils() {};
  virtual ~Utils() {};

  public:
  static TFile *     OpenFile(std::string filename, std::string mode = "READ", bool createLocalDir = true);
  static std::string OpenRawFile(std::string filename);
  static TMacro *    OpenMacro(std::string filename);
  // static void        RebinBins(int & min, int & max, int rebin);
  static std::string GetCutsPath(json cuts);
  static Int_t       GetBinFromBase(Int_t bin, Int_t rebin, Int_t rebin_start);
  static int SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                                 bool normalizeToWidth = false, bool onlyPositive = false, double times = 1);
  static std::vector<std::string> Tokenize(std::string_view input, const char delim);

  /// \cond CLASSIMP
  ClassDef(Utils, 0);
  /// \endcond;

}; // namespace Utils
} // namespace Ndmspc
#endif
