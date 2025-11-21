#ifndef NdmspcCoreNUtils_H
#define NdmspcCoreNUtils_H

#include <set>
#include <vector>
#include <TFile.h>
#include <TAxis.h>
#include <TMacro.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
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
  static void        EnableMT(UInt_t numthreads = 0);
  static bool        AccessPathName(std::string path);
  static bool        IsFileSupported(std::string filename);
  static int         Cp(std::string source, std::string destination);
  static TFile *     OpenFile(std::string filename, std::string mode = "READ", bool createLocalDir = true);
  static std::string OpenRawFile(std::string filename);
  static bool        SaveRawFile(std::string filename, std::string content);
  static TMacro *    OpenMacro(std::string filename);
  static TH1 *       ProjectTHnSparse(THnSparse * hns, const std::vector<int> & axes, Option_t * option = "");
  static bool SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges = {}, bool withOverflow = false,
                            bool modifyTitle = false);
  static bool SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow = false,
                            bool modifyTitle = false);
  static bool GetAxisRangeInBase(TAxis * a, int rebin, int rebin_start, int bin, int & min, int & max);
  static bool GetAxisRangeInBase(TAxis * a, int min, int max, TAxis * base, int & minBase, int & maxBase);

  static TAxis *     CreateAxisFromLabels(const std::string & name, const std::string & title,
                                          const std::vector<std::string> & labels);
  static TAxis *     CreateAxisFromLabels(const std::string & name, const std::string & title,
                                          const std::set<std::string> & labels);
  static THnSparse * Convert(TH1 * h1, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * Convert(TH2 * h2, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * Convert(TH3 * h3, std::vector<std::string> names = {}, std::vector<std::string> titles = {});
  static THnSparse * ReshapeSparseAxes(THnSparse * hns, std::vector<int> order, std::vector<TAxis *> newAxes = {},
                                       std::vector<int> newPoint = {}, Option_t * option = "E");
  static void        GetTrueHistogramMinMax(const TH1 * h, double & min_val, double & max_val,
                                            bool include_overflow_underflow = false);

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
  static std::string      GetCoordsString(const std::vector<Long64_t> & coords, int index = -1, int width = 0);
  static std::string      GetCoordsString(const std::vector<std::string> & coords, int index = -1, int width = 0);
  static void             PrintPointSafe(const std::vector<int> & coords, int index = -1);

  static std::vector<std::vector<int>> Permutations(const std::vector<int> & v);

  static std::string              GetJsonString(json j);
  static int                      GetJsonInt(json j);
  static double                   GetJsonDouble(json j);
  static bool                     GetJsonBool(json j);
  static std::vector<std::string> GetJsonStringArray(json j);

  static std::string FormatTime(long long seconds);

  static void ProgressBar(int current, int total, int barWidth = 70, std::string prefix = "", std::string suffix = "");
  static void ProgressBar(int current, int total, std::chrono::high_resolution_clock::time_point startTime,
                          int barWidth = 70, std::string prefix = "", std::string suffix = "");
  static THnSparse * CreateSparseFromParquetTaxi(const std::string & filename, THnSparse * hns = nullptr,
                                                 Int_t nMaxRows = -1);

  /// \cond CLASSIMP
  ClassDef(NUtils, 0);
  /// \endcond;

}; // namespace NUtils
} // namespace Ndmspc
#endif
