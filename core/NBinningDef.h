#ifndef Ndmspc_NBinningDef_H
#define Ndmspc_NBinningDef_H
#include <string>
#include <vector>
#include <map>
#include <TObject.h>
#include <TAxis.h>
#include <THnSparse.h>
namespace Ndmspc {

///
/// \class NBinningDef
///
/// \brief NBinningDef object
///	\author Martin Vala <mvala@cern.ch>
///
class NBinning;

class NBinningDef : public TObject {
  public:
  NBinningDef(std::string name = "default", std::map<std::string, std::vector<std::vector<int>>> definition = {},
              NBinning * binning = nullptr);
  virtual ~NBinningDef();

  virtual void Print(Option_t * option = "") const;

  std::map<std::string, std::vector<std::vector<int>>> GetDefinition() const { return fDefinition; }
  void SetDefinition(const std::map<std::string, std::vector<std::vector<int>>> & d) { fDefinition = d; }
  void SetAxisDefinition(std::string axisName, const std::vector<std::vector<int>> & d) { fDefinition[axisName] = d; }
  std::vector<int> GetVariableAxes() const { return fVariableAxes; }
  void             AddVariableAxis(int axis) { fVariableAxes.push_back(axis); }

  void RefreshContentFromIds();
  void RefreshIdsFromContent();

  std::vector<Long64_t>   GetIds() const { return fIds; }
  std::vector<Long64_t> & GetIds() { return fIds; }
  Long64_t                GetId(int index) const;
  TObjArray *             GetAxes() const { return fContent->GetListOfAxes(); }
  THnSparse *             GetContent() const { return fContent; }

  NBinning * GetBinning() const { return fBinning; }

  private:
  NBinning *                                           fBinning{nullptr}; ///< Pointer to the parent binning
  std::string                                          fName;             ///< Name of the binning definition
  std::map<std::string, std::vector<std::vector<int>>> fDefinition;       ///< Binning mapping definition
  std::vector<int>      fVariableAxes{};   ///< List of variable axes indices in the content histogram
  std::vector<Long64_t> fIds{};            ///< List of IDs for the binning definition
  THnSparse *           fContent{nullptr}; ///< Template histogram for the binning definition

  /// \cond CLASSIMP
  ClassDef(NBinningDef, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
