#ifndef NdmspcCoreUtils_H
#define NdmspcCoreUtils_H

#include <TFile.h>
#include <TAxis.h>
#include <TMacro.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
#include <set>
#include <vector>
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class Utils
///
/// \brief Utils object
///	\author Martin Vala <mvala@cern.ch>
///

class Utils : TObject {

  /// Constructor
  Utils() {};
  /// Destructor
  virtual ~Utils() {};

  public:
  static TFile *     OpenFile(std::string filename, std::string mode = "READ", bool createLocalDir = true);
  static std::string OpenRawFile(std::string filename);
  static bool        SaveRawFile(std::string filename, std::string content);
  static TMacro *    OpenMacro(std::string filename);
  // static void        RebinBins(int & min, int & max, int rebin);
  static std::string GetBasePath(json cfg);
  static std::string GetCutsPath(json cuts);
  static Int_t       GetBinFromBase(Int_t bin, Int_t rebin, Int_t rebin_start);
  static int SetResultValueError(json cfg, THnSparse * output, std::string name, Int_t * point, double val, double err,
                                 bool normalizeToWidth = false, bool onlyPositive = false, double times = 1);
  static std::vector<std::string> Tokenize(std::string_view input, const char delim);
  static std::vector<int>         TokenizeInt(std::string_view input, const char delim);
  static std::vector<std::string> Find(std::string path, std::string filename = "");
  static std::vector<std::string> FindLocal(std::string path, std::string filename = "");
  static std::vector<std::string> FindEos(std::string path, std::string filename = "");

  static std::set<std::string> Unique(std::vector<std::string> & paths, int axis, std::string path, char token = '/');
  static std::vector<std::string> Truncate(std::vector<std::string> values, std::string value);
  static TH2D *                   GetMappingHistogram(std::string name, std::string title, std::set<std::string> x,
                                                      std::set<std::string> y = {});

  static bool SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>>);
  /// \cond CLASSIMP
  ClassDef(Utils, 0);
  /// \endcond;

}; // namespace Utils
} // namespace Ndmspc
#endif
