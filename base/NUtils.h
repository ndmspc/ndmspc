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
/// \brief Utility class providing static helper functions for file operations, histogram manipulations,
///        axis handling, string and vector utilities, JSON parsing, and progress display.
/// \author Martin Vala <mvala@cern.ch>
///
class NUtils : TObject {

  /// Constructor
  NUtils() {};
  /// Destructor
  virtual ~NUtils() {};

  public:
  /**
   * @brief Enable multi-threading with specified number of threads.
   * @param numthreads Number of threads to enable (0 for default).
   */
  static bool EnableMT(Int_t numthreads = -1);

  /**
   * @brief Check if a path is accessible.
   * @param path Path to check.
   * @return True if accessible, false otherwise.
   */
  static bool AccessPathName(std::string path);

  /**
   * @brief Check if a file is supported.
   * @param filename Name of the file.
   * @return True if supported, false otherwise.
   */
  static bool IsFileSupported(std::string filename);

  /**
   * @brief Copy a file from source to destination.
   * @param source Source file path.
   * @param destination Destination file path.
   * @return Status code (0 for success).
   */
  static int Cp(std::string source, std::string destination);

  /**
   * @brief Open a ROOT file.
   * @param filename File name.
   * @param mode File open mode ("READ", "UPDATE", etc.).
   * @param createLocalDir Create local directory if needed.
   * @return Pointer to opened TFile.
   */
  static TFile * OpenFile(std::string filename, std::string mode = "READ", bool createLocalDir = true);

  /**
   * @brief Open a raw file and return its content as string.
   * @param filename File name.
   * @return File content.
   */
  static std::string OpenRawFile(std::string filename);

  /**
   * @brief Save content to a raw file.
   * @param filename File name.
   * @param content Content to save.
   * @return True if successful.
   */
  static bool SaveRawFile(std::string filename, std::string content);

  /**
   * @brief Open a macro file.
   * @param filename Macro file name.
   * @return Pointer to TMacro object.
   */
  static TMacro * OpenMacro(std::string filename);

  /**
   * @brief Loads a JSON configuration file into the provided json object.
   *
   * @param cfg Reference to a json object where the file contents will be loaded.
   * @param filename Path to the JSON file to load.
   * @return true if the file was loaded and parsed successfully, false otherwise.
   */
  static bool LoadJsonFile(json & cfg, std::string filename);
  /**
   * @brief Project a THnSparse histogram onto specified axes.
   * @param hns Input THnSparse.
   * @param axes Axes to project.
   * @param option Projection options.
   * @return Pointer to projected TH1.
   */
  static TH1 * ProjectTHnSparse(THnSparse * hns, const std::vector<int> & axes, Option_t * option = "");

  /**
   * @brief Set axis ranges for THnSparse using vector of ranges.
   * @param sparse Input THnSparse.
   * @param ranges Vector of axis ranges.
   * @param withOverflow Include overflow bins.
   * @param modifyTitle Modify histogram title.
   * @return True if successful.
   */
  static bool SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges = {}, bool withOverflow = false,
                            bool modifyTitle = false);

  /**
   * @brief Set axis ranges for THnSparse using map of ranges.
   * @param sparse Input THnSparse.
   * @param ranges Map of axis ranges.
   * @param withOverflow Include overflow bins.
   * @param modifyTitle Modify histogram title.
   * @return True if successful.
   */
  static bool SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow = false,
                            bool modifyTitle = false);

  /**
   * @brief Get axis range in base for rebinned axis.
   * @param a Axis pointer.
   * @param rebin Rebin factor.
   * @param rebin_start Start bin for rebinning.
   * @param bin Bin index.
   * @param min Output minimum bin.
   * @param max Output maximum bin.
   * @return True if successful.
   */
  static bool GetAxisRangeInBase(TAxis * a, int rebin, int rebin_start, int bin, int & min, int & max);

  /**
   * @brief Get axis range in base axis.
   * @param a Axis pointer.
   * @param min Minimum bin.
   * @param max Maximum bin.
   * @param base Base axis pointer.
   * @param minBase Output minimum base bin.
   * @param maxBase Output maximum base bin.
   * @return True if successful.
   */
  static bool GetAxisRangeInBase(TAxis * a, int min, int max, TAxis * base, int & minBase, int & maxBase);

  /**
   * @brief Create a TAxis from a list of labels.
   * @param name Axis name.
   * @param title Axis title.
   * @param labels Vector of labels.
   * @return Pointer to created TAxis.
   */
  static TAxis * CreateAxisFromLabels(const std::string & name, const std::string & title,
                                      const std::vector<std::string> & labels);

  /**
   * @brief Create a TAxis from a set of labels.
   * @param name Axis name.
   * @param title Axis title.
   * @param labels Set of labels.
   * @return Pointer to created TAxis.
   */
  static TAxis * CreateAxisFromLabelsSet(const std::string & name, const std::string & title,
                                         const std::set<std::string> & labels);

  /**
   * @brief Convert TH1 to THnSparse.
   * @param h1 Input TH1.
   * @param names Axis names.
   * @param titles Axis titles.
   * @return Pointer to THnSparse.
   */
  static THnSparse * Convert(TH1 * h1, std::vector<std::string> names = {}, std::vector<std::string> titles = {});

  /**
   * @brief Convert TH2 to THnSparse.
   * @param h2 Input TH2.
   * @param names Axis names.
   * @param titles Axis titles.
   * @return Pointer to THnSparse.
   */
  static THnSparse * Convert(TH2 * h2, std::vector<std::string> names = {}, std::vector<std::string> titles = {});

  /**
   * @brief Convert TH3 to THnSparse.
   * @param h3 Input TH3.
   * @param names Axis names.
   * @param titles Axis titles.
   * @return Pointer to THnSparse.
   */
  static THnSparse * Convert(TH3 * h3, std::vector<std::string> names = {}, std::vector<std::string> titles = {});

  /**
   * @brief Reshape axes of THnSparse.
   * @param hns Input THnSparse.
   * @param order New axis order.
   * @param newAxes New axes (optional).
   * @param newPoint New point (optional).
   * @param option Option string.
   * @return Pointer to reshaped THnSparse.
   */
  static THnSparse * ReshapeSparseAxes(THnSparse * hns, std::vector<int> order, std::vector<TAxis *> newAxes = {},
                                       std::vector<int> newPoint = {}, Option_t * option = "E");

  /**
   * @brief Get minimum and maximum value of histogram bins.
   * @param h Input histogram.
   * @param min_val Output minimum value.
   * @param max_val Output maximum value.
   * @param include_overflow_underflow Include overflow/underflow bins.
   */
  static void GetTrueHistogramMinMax(const TH1 * h, double & min_val, double & max_val,
                                     bool include_overflow_underflow = false);

  /**
   * @brief Creates an array of axes objects from files in specified directories.
   *
   * Searches the given directories for files matching the specified file name and headers,
   * and constructs a TObjArray of axes objects found in those files.
   *
   * @param paths A vector of directory paths to search.
   * @param findPath The subdirectory path to look for within each directory.
   * @param fileName The name of the file to search for in each directory.
   * @param axesNames A vector of axis names
   * @return A pointer to a TObjArray containing the axes objects, or nullptr if none found.
   */
  static TObjArray * AxesFromDirectory(const std::vector<std::string> paths, const std::string & findPath,
                                       const std::string & fileName, const std::vector<std::string> & axesNames);

  /**
   * @brief Tokenize a string by delimiter.
   * @param input Input string.
   * @param delim Delimiter character.
   * @return Vector of tokens.
   */
  static std::vector<std::string> Tokenize(std::string_view input, const char delim);

  /**
   * @brief Tokenize a string into integers by delimiter.
   * @param input Input string.
   * @param delim Delimiter character.
   * @return Vector of integers.
   */
  static std::vector<int> TokenizeInt(std::string_view input, const char delim);

  /**
   * @brief Join vector of strings into a single string with delimiter.
   * @param values Vector of strings.
   * @param delim Delimiter character.
   * @return Joined string.
   */
  static std::string Join(const std::vector<std::string> & values, const char delim = ',');

  /**
   * @brief Join vector of integers into a single string with delimiter.
   * @param values Vector of integers.
   * @param delim Delimiter character.
   * @return Joined string.
   */
  static std::string Join(const std::vector<int> & values, const char delim = ',');

  /**
   * @brief Find files in a path matching filename.
   * @param path Directory path.
   * @param filename Filename pattern.
   * @return Vector of found file paths.
   */
  static std::vector<std::string> Find(std::string path, std::string filename = "");

  /**
   * @brief Find local files in a path matching filename.
   * @param path Directory path.
   * @param filename Filename pattern.
   * @return Vector of found local file paths.
   */
  static std::vector<std::string> FindLocal(std::string path, std::string filename = "");

  /**
   * @brief Find EOS files in a path matching filename.
   * @param path Directory path.
   * @param filename Filename pattern.
   * @return Vector of found EOS file paths.
   */
  static std::vector<std::string> FindEos(std::string path, std::string filename = "");

  /**
   * @brief Get unique values from vector of strings at specified axis.
   * @param paths Vector of paths.
   * @param axis Axis index.
   * @param path Path string.
   * @param token Token character.
   * @return Set of unique strings.
   */
  static std::set<std::string> Unique(std::vector<std::string> & paths, int axis, std::string path, char token = '/');

  /**
   * @brief Truncate vector of strings by a value.
   * @param values Vector of strings.
   * @param value Value to truncate by.
   * @return Truncated vector.
   */
  static std::vector<std::string> Truncate(std::vector<std::string> values, std::string value);

  /**
   * @brief Convert array to vector.
   * @param v1 Input array.
   * @param size Size of array.
   * @return Vector of integers.
   */
  static std::vector<int> ArrayToVector(Int_t * v1, int size);

  /**
   * @brief Convert vector to array.
   * @param v1 Input vector.
   * @param v2 Output array.
   */
  static void VectorToArray(std::vector<int> v1, Int_t * v2);

  /**
   * @brief Get string representation of coordinates.
   * @param coords Vector of coordinates.
   * @param index Index to highlight (-1 for none).
   * @param width Width for formatting.
   * @return String representation.
   */
  static std::string GetCoordsString(const std::vector<int> & coords, int index = -1, int width = 0);

  /**
   * @brief Get string representation of coordinates (Long64_t).
   * @param coords Vector of coordinates.
   * @param index Index to highlight (-1 for none).
   * @param width Width for formatting.
   * @return String representation.
   */
  static std::string GetCoordsString(const std::vector<Long64_t> & coords, int index = -1, int width = 0);

  /**
   * @brief Get string representation of coordinates (string).
   * @param coords Vector of coordinates.
   * @param index Index to highlight (-1 for none).
   * @param width Width for formatting.
   * @return String representation.
   */
  static std::string GetCoordsString(const std::vector<std::string> & coords, int index = -1, int width = 0);

  /**
   * @brief Print coordinates safely.
   * @param coords Vector of coordinates.
   * @param index Index to highlight (-1 for none).
   */
  static void PrintPointSafe(const std::vector<int> & coords, int index = -1);

  /**
   * @brief Generate all permutations of a vector.
   * @param v Input vector.
   * @return Vector of permutations.
   */
  static std::vector<std::vector<int>> Permutations(const std::vector<int> & v);

  /**
   * @brief Get JSON value as string.
   * @param j Input JSON.
   * @return String value.
   */
  static std::string GetJsonString(json j);

  /**
   * @brief Get JSON value as integer.
   * @param j Input JSON.
   * @return Integer value.
   */
  static int GetJsonInt(json j);

  /**
   * @brief Get JSON value as double.
   * @param j Input JSON.
   * @return Double value.
   */
  static double GetJsonDouble(json j);

  /**
   * @brief Get JSON value as boolean.
   * @param j Input JSON.
   * @return Boolean value.
   */
  static bool GetJsonBool(json j);

  /**
   * @brief Get JSON value as array of strings.
   * @param j Input JSON.
   * @return Vector of strings.
   */
  static std::vector<std::string> GetJsonStringArray(json j);

  /**
   * @brief Format time in seconds to human-readable string.
   * @param seconds Time in seconds.
   * @return Formatted time string.
   */
  static std::string FormatTime(long long seconds);

  /**
   * @brief Display progress bar.
   * @param current Current progress.
   * @param total Total value.
   * @param barWidth Width of the bar.
   * @param prefix Prefix string.
   * @param suffix Suffix string.
   */
  static void ProgressBar(int current, int total, std::string prefix = "", std::string suffix = "", int barWidth = 50);

  /**
   * @brief Display progress bar with timing.
   * @param current Current progress.
   * @param total Total value.
   * @param startTime Start time point.
   * @param barWidth Width of the bar.
   * @param prefix Prefix string.
   * @param suffix Suffix string.
   */
  static void ProgressBar(int current, int total, std::chrono::high_resolution_clock::time_point startTime,
                          std::string prefix = "", std::string suffix = "", int barWidth = 50);

  /**
   * @brief Create THnSparse from Parquet Taxi file.
   * @param filename Parquet file name.
   * @param hns Optional input THnSparse.
   * @param nMaxRows Maximum number of rows to read.
   * @return Pointer to created THnSparse.
   */
  static THnSparse * CreateSparseFromParquetTaxi(const std::string & filename, THnSparse * hns = nullptr,
                                                 Int_t nMaxRows = -1);

  /// \cond CLASSIMP
  ClassDef(NUtils, 0);
  /// \endcond;

}; // namespace NUtils
} // namespace Ndmspc
#endif
