#ifndef Ndmspc_NGnTree_H
#define Ndmspc_NGnTree_H
#include <TObject.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TObject.h>
#include "NBinning.h"
#include "NParameters.h"
#include "NStorageTree.h"
#include "NWsClient.h"
// #include "NBinningPoint.h"

namespace Ndmspc {

/**
 * @brief Function pointer type for processing sparse nodes in the tree.
 *
 * This function is expected to process a binning point and two lists, with an additional integer parameter.
 *
 * @param Ndmspc::NBinningPoint* Pointer to the binning point to process.
 * @param TList* Pointer to the first list involved in processing.
 * @param TList* Pointer to the second list involved in processing.
 * @param int Additional integer parameter for processing.
 */
using NHnSparseProcessFuncPtr = void (*)(Ndmspc::NBinningPoint *, TList *, TList *, int);

/**
 * @class NGnTree
 * @brief NDMSPC tree object for managing multi-dimensional data storage and processing.
 *
 * Provides methods for construction, printing, playing, processing, projections, and access to binning,
 * storage, outputs, and input trees. Supports opening from files/trees, entry management, and integration
 * with NBinning and NStorageTree.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NGnNavigator;
class NGnTree : public TObject {
  public:
  /**
   * @brief Default constructor.
   */
  NGnTree();

  /**
   * @brief Constructor from vector of axes.
   * @param axes Vector of TAxis pointers.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "ngnt").
   */
  NGnTree(std::vector<TAxis *> axes, std::string filename = "", std::string treename = "ngnt");

  /**
   * @brief Constructor from TObjArray of axes.
   * @param axes TObjArray of axes.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "ngnt").
   */
  NGnTree(TObjArray * axes, std::string filename = "", std::string treename = "ngnt");

  /**
   * @brief Constructor from another NGnTree.
   * @param ngnt Pointer to NGnTree object.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "ngnt").
   */
  NGnTree(NGnTree * ngnt, std::string filename = "", std::string treename = "ngnt");

  /**
   * @brief Constructor from NBinning and NStorageTree.
   * @param binning Pointer to NBinning object.
   * @param treeStorage Pointer to NStorageTree object.
   */
  NGnTree(NBinning * binning, NStorageTree * treeStorage);

  /**
   * @brief Destructor.
   */
  virtual ~NGnTree();

  /**
   * @brief Print tree information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const override;

  /**
   * @brief Draws the tree object.
   *
   * This method overrides the base class Draw function to provide
   * custom drawing logic for the NGnTree object.
   *
   * @param option Optional string to specify drawing options.
   */
  virtual void Draw(Option_t * option = "") override;

  /**
   * @brief Play tree data with optional binning and output point IDs.
   * @param timeout Timeout in seconds.
   * @param binning Binning name.
   * @param outputPointIds Vector of output point IDs.
   * @param ranges Vector of ranges for axes.
   * @param option Play options.
   * @param ws Optional workspace string.
   */
  void Play(int timeout = 0, std::string binning = "", std::vector<int> outputPointIds = {0},
            std::vector<std::vector<int>> ranges = {}, Option_t * option = "", std::string ws = "");

  /**
   * @brief Get pointer to binning object.
   * @return Pointer to NBinning.
   */
  NBinning * GetBinning() const { return fBinning; }

  /**
   * @brief Set binning object pointer.
   * @param binning Pointer to NBinning.
   */
  void SetBinning(NBinning * binning) { fBinning = binning; }

  /**
   * @brief Get pointer to storage tree object.
   * @return Pointer to NStorageTree.
   */
  NStorageTree * GetStorageTree() const { return fTreeStorage; }

  /**
   * @brief Get outputs map.
   * @return Map of output names to TList pointers.
   */
  std::map<std::string, TList *> GetOutputs() const { return fOutputs; }

  /**
   * @brief Get output list by name.
   * @param name Output name (optional).
   * @return Pointer to TList.
   */
  TList * GetOutput(std::string name = "");

  /**
   * @brief Set outputs map.
   * @param outputs Map of output names to TList pointers.
   */
  void SetOutputs(std::map<std::string, TList *> outputs) { fOutputs = outputs; }

  /**
   * @brief Get pointer to input NGnTree.
   * @return Pointer to NGnTree.
   */
  NGnTree * GetInput() const { return fInput; }

  /**
   * @brief Set input NGnTree pointer.
   * @param input Pointer to NGnTree.
   */
  void SetInput(NGnTree * input) { fInput = input; }

  /**
   * @brief Returns the navigator associated with this tree.
   * @return Pointer to the NGnNavigator instance.
   */
  NGnNavigator * GetNavigator() const { return fNavigator; }

  /**
   * @brief Sets the navigator for this tree.
   * @param navigator Pointer to the NGnNavigator instance to associate.
   */
  void SetNavigator(NGnNavigator * navigator);

  /**
   * @brief Get number of entries in storage tree.
   * @return Number of entries.
   */
  Long64_t GetEntries() const { return fTreeStorage ? fTreeStorage->GetEntries() : 0; }

  /**
   * @brief Get entry by index.
   * @param entry Entry index.
   * @param checkBinningDef Check binning definition.
   * @return Status code.
   */
  Int_t GetEntry(Long64_t entry, bool checkBinningDef = true);

  /**
   * @brief Close the tree, optionally writing data.
   * @param write If true, write data before closing.
   * @return True if closed successfully.
   */
  bool Close(bool write = false);

  /**
   * @brief Process tree data using a function pointer and configuration.
   * @param func Processing function pointer.
   * @param cfg JSON configuration object.
   * @param binningName Binning name.
   * @return True if processed successfully.
   */
  bool Process(NHnSparseProcessFuncPtr func, const json & cfg = json::object(), std::string binningName = "");

  /**
   * @brief Process tree data using a function pointer and definition names.
   * @param func Processing function pointer.
   * @param defNames Vector of definition names.
   * @param cfg JSON configuration object.
   * @param binningIn Pointer to NBinning object.
   * @return True if processed successfully.
   */
  bool Process(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames,
               const json & cfg = json::object(), NBinning * binningIn = nullptr);

  bool ProcessOld(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames,
                  const json & cfg = json::object(), NBinning * binningIn = nullptr);
  /**
   * @brief Project tree data using configuration and binning name.
   * @param cfg JSON configuration object.
   * @param binningName Binning name.
   * @return Pointer to TList of projected objects.
   */
  TList * Projection(const json & cfg, std::string binningName = "");
  /**
   * @brief Reshape navigator using binning name and levels.
   * @param binningName Name of binning definition.
   * @param levels Vector of levels.
   * @param level Current level (default: 0).
   * @param ranges Map of ranges for axes.
   * @param rangesBase Map of base ranges for axes.
   * @return Pointer to reshaped NGnNavigator.
   */
  NGnNavigator * Reshape(std::string binningName, std::vector<std::vector<int>> levels, int level = 0,
                         std::map<int, std::vector<int>> ranges = {}, std::map<int, std::vector<int>> rangesBase = {});
  /**
   * @brief Returns a navigator for resource statistics based on binning and levels.
   *
   * @param binningName Name of the binning scheme to use.
   * @param levels A vector of vectors specifying the levels for binning.
   * @param level The starting level (default is 0).
   * @param ranges Optional map specifying ranges for each level.
   * @param rangesBase Optional map specifying base ranges for each level.
   * @return Pointer to NGnNavigator configured for resource statistics.
   */
  NGnNavigator * GetResourceStatisticsNavigator(std::string binningName, std::vector<std::vector<int>> levels,
                                                int level = 0, std::map<int, std::vector<int>> ranges = {},
                                                std::map<int, std::vector<int>> rangesBase = {});
  /**
   * @brief Returns the parameters associated with this tree.
   * @return Pointer to NParameters object containing the tree's parameters.
   */
  NParameters * GetParameters() const { return fParameters; }

  /**
   * @brief Returns the associated NWsClient instance.
   * @return Pointer to the NWsClient object.
   */
  NWsClient * GetWsClient() const { return fWsClient; }

  /**
   * @brief Sets the NWsClient instance for this object.
   * @param client Pointer to the NWsClient object to associate.
   */
  void SetWsClient(NWsClient * client) { fWsClient = client; }

  /**
   * @brief Initializes the parameters for the tree using the provided parameter names.
   *
   * This function sets up the internal parameter structure based on the given list of parameter names.
   *
   * @param paramNames A vector of strings containing the names of the parameters to initialize.
   * @return true if initialization was successful, false otherwise.
   */
  bool InitParameters(const std::vector<std::string> & paramNames);

  /**
   * @brief Open NGnTree from file.
   * @param filename File name.
   * @param branches Branches to open.
   * @param treename Tree name (default: "ngnt").
   * @return Pointer to opened NGnTree.
   */
  static NGnTree * Open(const std::string & filename, const std::string & branches = "",
                        const std::string & treename = "ngnt");

  /**
   * @brief Open NGnTree from TTree.
   * @param tree Pointer to TTree.
   * @param branches Branches to open.
   * @param file Pointer to TFile.
   * @return Pointer to opened NGnTree.
   */
  static NGnTree * Open(TTree * tree, const std::string & branches = "", TFile * file = nullptr);

  static NGnTree * Import(THnSparse * hns, std::string parameterAxis = "",
                          const std::string & filename = "/tmp/hnst_imported.root");

  protected:
  NBinning *                     fBinning{nullptr};     ///< Binning object
  NStorageTree *                 fTreeStorage{nullptr}; ///< Tree storage
  std::map<std::string, TList *> fOutputs;              ///< Outputs
  NGnTree *                      fInput{nullptr};       ///< Input NGnTree for processing
  NGnNavigator *                 fNavigator{nullptr};   ///<! Navigator object
  NParameters *                  fParameters{nullptr};  ///< Parameters object
  NWsClient *                    fWsClient{nullptr};    ///<! WebSocket client for communication

  /// \cond CLASSIMP
  ClassDefOverride(NGnTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
