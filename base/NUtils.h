#ifndef NdmspcCoreNUtils_H
#define NdmspcCoreNUtils_H

#include <set>
#include <vector>
#include <TFile.h>
#include <TCanvas.h>
#include <TAxis.h>
#include <TMacro.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <TBufferJSON.h>
#include <TString.h>
#include "NLogger.h"

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
  static int Cp(std::string source, std::string destination,Bool_t progressbar = kTRUE);
  static bool CreateDirectory(const std::string & path);

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

  using RawJsonInjections = std::vector<std::pair<std::vector<std::string>, std::string>>;

  /**
   * Injects multiple raw JSON strings into a json object at the specified
   * nested key paths. Each entry in the injections vector is a pair of:
   *   - keys:    nested key path (e.g. {"data", "detector", "list"})
   *   - rawJson: raw JSON string to inject at that path
   *
   * This avoids re-parsing raw JSON strings, preserving the exact output of TBufferJSON.
   *
   * @param json       The json object to inject into
   * @param injections Vector of {keys, rawJson} pairs
   * @return           The final JSON string with all raw JSONs injected
   * @throws std::invalid_argument if any keys array is empty
   * @throws std::runtime_error if any placeholder is not found after dump
   */
  static std::string InjectRawJson(json & j, const RawJsonInjections & injections);

  /**
   * @brief Add one raw JSON injection entry into metadata field.
   *
   * The server serializer can later collect these entries and apply InjectRawJson
   * without each handler duplicating metadata format details.
   *
   * @param j JSON envelope that stores injection metadata
   * @param path Nested key path where raw JSON should be injected
   * @param rawJson Raw JSON string to inject
   * @param injectionsKey Metadata key used to store injection entries
   */
  static void AddRawJsonInjection(json & j, const std::vector<std::string> & path, const std::string & rawJson,
                                  const std::string & injectionsKey = "__raw_json_injections");

  /**
   * @brief Collect raw JSON injection entries from metadata field.
   *
   * @param j JSON envelope containing injection metadata
   * @param injections Output vector of parsed {path, rawJson} entries
   * @param injectionsKey Metadata key used to store injection entries
   * @return True if metadata exists and at least one valid injection was collected
   */
  static bool CollectRawJsonInjections(const json & j, RawJsonInjections & injections,
                                       const std::string & injectionsKey = "__raw_json_injections");

  /**
   * @brief Merge raw JSON string with metadata fields.
   *
   * Parses the raw JSON string and merges in metadata fields. Metadata fields
   * at the top level are added/merged directly; nested paths are not supported.
   *
   * @param rawJson Raw JSON string to merge into
   * @param metadata JSON object with fields to merge
   * @return Merged JSON string
   */
  static std::string MergeRawJsonWithMetadata(const std::string & rawJson, const json & metadata);

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
   * @param reset Reset axis ranges before setting new ones.
   * @return True if successful.
   */
  static bool SetAxisRanges(THnSparse * sparse, std::vector<std::vector<int>> ranges = {}, bool withOverflow = false,
                            bool modifyTitle = false, bool reset = true);

  /**
   * @brief Set axis ranges for THnSparse using map of ranges.
   * @param sparse Input THnSparse.
   * @param ranges Map of axis ranges.
   * @param withOverflow Include overflow bins.
   * @param modifyTitle Modify histogram title.
   * @param reset Reset axis ranges before setting new ones.
   * @return True if successful.
   */
  static bool SetAxisRanges(THnSparse * sparse, std::map<int, std::vector<int>> ranges, bool withOverflow = false,
                            bool modifyTitle = false, bool reset = true);

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
   * @brief Get string representation of coordinates.
   * @param coords Vector of coordinates.
   * @param index Index to highlight (-1 for none).
   * @param width Width for formatting.
   * @return String representation.
   */
  static std::string GetCoordsString(const std::vector<size_t> & coords, int index = -1, int width = 0);

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
   * @brief Get process CPU and RSS memory statistics using ROOT's gSystem::GetProcInfo
   * @return json object containing cpu and rss information
   */
  static json GetSystemStats();

  /**
   * @brief Get TFile read/write statistics by inspecting ROOT's list of open files
   * @return json object containing per-file and aggregated IO statistics
   */
  static json GetTFileIOStats();

  /**
   * @brief Get system-wide network interface totals (RX/TX bytes) in a cross-platform way.
   *        On Linux reads /proc/net/dev; on macOS uses getifaddrs() and struct if_data.
   * @return json object containing per-interface and aggregated rx/tx bytes
   */
  static json GetNetDevStats();

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
   * @brief Create a ROOT TCanvas with specified name, title, and dimensions.
   *
   * This utility function creates and returns a pointer to a new TCanvas object.
   *
   * @param name Name of the canvas.
   * @param title Title of the canvas.
   * @param width Width of the canvas in pixels (default: 800).
   * @param height Height of the canvas in pixels (default: 600).
   * @return Pointer to the created TCanvas object.
   */
  static TCanvas * CreateCanvas(const std::string & name, const std::string & title, int width = 800, int height = 600);

  /**
   * @brief Safely delete a vector of ROOT objects, bypassing GarbageCollect.
   *
   * Extracts and empties pad primitive lists (to prevent GarbageCollect during
   * canvas/pad destruction), then deletes all objects directly.
   *
   * @param objects Vector of object pointers. Cleared after deletion.
   */
  static void SafeDeleteObjects(std::vector<TObject *> & objects);

  /**
   * @brief Safely delete a TList and all its contents, bypassing ROOT's GarbageCollect.
   *
   * Extracts objects into a vector, destroys the TList shell, then delegates
   * to SafeDeleteObjects.
   *
   * @param lst Pointer reference to the TList. Set to nullptr after deletion.
   */
  static void SafeDeleteTList(TList *& lst);

  /**
   * @brief Safely delete a TObject, handling TList contents and TCanvas/TPad cleanup.
   *
   * If the object is a TList, delegates to SafeDeleteTList. Otherwise deletes directly.
   *
   * @param obj Pointer reference to the object. Set to nullptr after deletion.
   */
  static void SafeDeleteObject(TObject *& obj);

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
