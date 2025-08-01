#ifndef NdmspcCoreNUtils_H
#define NdmspcCoreNUtils_H

#include <TFile.h>
#include <TAxis.h>
#include <TMacro.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
#include <set>
#include <vector>
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class NUtils
///
/// \brief NUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class NUtils : TObject {

  /// Constructor
  NUtils() {};
  /// Destructor
  virtual ~NUtils() {};

  public:
  static TFile *     OpenFile(std::string filename, std::string mode = "READ", bool createLocalDir = true);
  static std::string OpenRawFile(std::string filename);
  static bool        SaveRawFile(std::string filename, std::string content);
  static TMacro *    OpenMacro(std::string filename);
  static bool        SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges, bool withOverflow = false);
  static bool SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow = false);
  static bool GetAxisRangeInBase(TAxis * a, int rebin, int rebin_start, int bin, int & min, int & max);
  static bool GetAxisRangeInBase(TAxis * a, int min, int max, TAxis * base, int & minBase, int & maxBase);

  static THnSparse * Convert(TH1 * h1, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * Convert(TH2 * h2, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * Convert(TH3 * h3, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * ReshapeSparseAxes(THnSparse * hns, std::vector<int> order, std::vector<TAxis *> newAxes = {},
                                       std::vector<int> newPoint = {}, Option_t * option = "E");

  static std::vector<std::string> Tokenize(std::string_view input, const char delim);
  static std::vector<int>         TokenizeInt(std::string_view input, const char delim);
  static std::string              Join(const std::vector<std::string> & values, const char delim = ',');
  static std::string              Join(const std::vector<int> & values, const char delim = ',');
  static std::vector<std::string> Find(std::string path, std::string filename = "");
  static std::vector<std::string> FindLocal(std::string path, std::string filename = "");
  static std::vector<std::string> FindEos(std::string path, std::string filename = "");

  static std::set<std::string> Unique(std::vector<std::string> & paths, int axis, std::string path, char token = '/');
  static std::vector<std::string> Truncate(std::vector<std::string> values, std::string value);

  static std::vector<int> ArrayToVector(Int_t * v1, int size);
  static void             VectorToArray(std::vector<int> v1, Int_t * v2);
  static std::string      GetCoordsString(const std::vector<int> & coords, int index = -1, int width = 0);
  static std::string      GetCoordsString(const std::vector<std::string> & coords, int index = -1, int width = 0);
  static void             PrintPointSafe(const std::vector<int> & coords, int index = -1);

  static std::vector<std::vector<int>> Permutations(const std::vector<int> & v);

  static std::string              GetJsonString(json j);
  static int                      GetJsonInt(json j);
  static double                   GetJsonDouble(json j);
  static bool                     GetJsonBool(json j);
  static std::vector<std::string> GetJsonStringArray(json j);

  /// \cond CLASSIMP
  ClassDef(NUtils, 0);
  /// \endcond;

}; // namespace NUtils
} // namespace Ndmspc
#endif
