#ifndef Ndmspc_NBinningDef_H
#define Ndmspc_NBinningDef_H
#include <string>
#include <vector>
#include <map>
#include <TObject.h>
#include <TAxis.h>

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
  NBinningDef(std::map<std::string, std::vector<std::vector<int>>> definition = {}, NBinning * binning = nullptr);
  virtual ~NBinningDef();

  virtual void Print(Option_t * option = "") const;

  std::map<std::string, std::vector<std::vector<int>>> GetDefinition() const { return fDefinition; }
  void SetDefinition(const std::map<std::string, std::vector<std::vector<int>>> & d) { fDefinition = d; }
  void SetAxisDefinition(std::string axisName, const std::vector<std::vector<int>> & d) { fDefinition[axisName] = d; }

  std::vector<Long64_t>   GetIds() const { return fIds; }
  std::vector<Long64_t> & GetIds() { return fIds; }
  Long64_t                GetId(int index) const;
  TObjArray *             GetAxes() const { return fAxes; }

  private:
  std::map<std::string, std::vector<std::vector<int>>> fDefinition; ///< Binning mapping definition
  std::vector<Long64_t>                                fIds{};      ///< List of IDs for the binning definition

  TObjArray * fAxes{nullptr}; ///< List of axes for the binning definition

  /// \cond CLASSIMP
  ClassDef(NBinningDef, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
