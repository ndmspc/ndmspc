#ifndef Ndmspc_NGnTree_H
#define Ndmspc_NGnTree_H
#include <TObject.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TObject.h>
#include "NBinning.h"
#include "NStorageTree.h"
// #include "NBinningPoint.h"

namespace Ndmspc {

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
using NHnSparseProcessFuncPtr = void (*)(Ndmspc::NBinningPoint *, TList *, TList *, int);
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
   * @param treename Optional tree name (default: "hnst").
   */
  NGnTree(std::vector<TAxis *> axes, std::string filename = "", std::string treename = "hnst");

  /**
   * @brief Constructor from TObjArray of axes.
   * @param axes TObjArray of axes.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "hnst").
   */
  NGnTree(TObjArray * axes, std::string filename = "", std::string treename = "hnst");

  /**
   * @brief Constructor from another NGnTree.
   * @param ngnt Pointer to NGnTree object.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "hnst").
   */
  NGnTree(NGnTree * ngnt, std::string filename = "", std::string treename = "hnst");

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
   * @brief Open NGnTree from file.
   * @param filename File name.
   * @param branches Branches to open.
   * @param treename Tree name (default: "hnst").
   * @return Pointer to opened NGnTree.
   */
  static NGnTree * Open(const std::string & filename, const std::string & branches = "",
                        const std::string & treename = "hnst");

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
  NGnNavigator *                 fNavigator{nullptr};   ///! Navigator object

  /// \cond CLASSIMP
  ClassDefOverride(NGnTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
