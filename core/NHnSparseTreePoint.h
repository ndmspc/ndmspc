#ifndef Ndmspc_NHnSparseTreePoint_H
#define Ndmspc_NHnSparseTreePoint_H
#include <TObject.h>
#include <vector>
#include "NHnSparseObject.h"

namespace Ndmspc {

///
/// \class NHnSparseTreePoint
///
/// \brief NHnSparseTreePoint object
///	\author Martin Vala <mvala@cern.ch>
///
class NHnSparseObject;
class NHnSparseTree;

class NHnSparseTreePoint : public TObject {
  public:
  NHnSparseTreePoint(NHnSparseTree * hnst = nullptr);
  virtual ~NHnSparseTreePoint();

  virtual void Print(Option_t * option = "") const;
  virtual void Reset();

  std::vector<int>    GetPointContent() const { return fPointContent; }
  std::vector<int>    GetPointStorage() const { return fPointStorage; }
  std::string         GetTitle(const std::string & prefix = "") const;
  std::vector<double> GetPointMins() const { return fPointMin; }
  std::vector<double> GetPointMaxs() const { return fPointMax; }
  void                GetPointMinMax(int axisId, double & min, double & max) const;
  double              GetPointMin(int axisId) const;
  double              GetPointMax(int axisId) const;
  std::vector<int>    GetPointBinning(Int_t axisId) const;
  NHnSparseTree *     GetHnSparseTree() const { return fHnst; }
  std::vector<int>    GetVariableAxisIndexes() const;
  Long64_t            GetEntryNumber() const;

  void SetHnSparseTree(NHnSparseTree * hnst) { fHnst = hnst; }
  void SetPointContent(const std::vector<int> & content);
  // void SetPointStorage(const std::vector<int> & storage);
  //
  NHnSparseObject * GetHnSparseObject() const { return fHnsObj; }
  void              SetHnSparseObject(NHnSparseObject * hnsObj) { fHnsObj = hnsObj; }

  private:
  NHnSparseTree *          fHnst{nullptr};   ///! Binning object
  std::vector<int>         fPointContent;    ///< Point coordinates of content
  std::vector<int>         fPointStorage;    ///< Point coordinates of storage
  std::vector<double>      fPointMin;        ///< Point coordinates of min
  std::vector<double>      fPointMax;        ///< Point coordinates of max
  std::vector<std::string> fPointLabel;      ///< Point coordinates of label
  NHnSparseObject *        fHnsObj{nullptr}; ///< HnSparseObject

  /// \cond CLASSIMP
  ClassDef(NHnSparseTreePoint, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
