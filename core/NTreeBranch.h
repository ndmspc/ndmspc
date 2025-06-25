#ifndef Ndmspc_NTreeBranch_H
#define Ndmspc_NTreeBranch_H
#include <TObject.h>
#include <TBranch.h>
#include <string>

namespace Ndmspc {

///
/// \class NTreeBranch
///
/// \brief NTreeBranch object
///	\author Martin Vala <mvala@cern.ch>
///
class NTreeBranch : public TObject {
  public:
  NTreeBranch(TTree * tree = nullptr, const std::string & name = "", void * address = nullptr,
              const std::string & objClassName = "TObject");
  virtual ~NTreeBranch();

  virtual void Print(Option_t * option = "") const;

  ///< Sets branch name
  void SetName(const std::string & name) { fName = name; }
  ///< Sets Object class name
  void SetObjectClassName(const std::string & objClassName) { fObjectClassName = objClassName; }
  ///< Sets branch
  void SetBranch(TBranch * branch) { fBranch = branch; }
  ///< Sets object
  void SetObject(TObject * obj) { fObject = obj; }
  ///< Returns branch
  TBranch * GetBranch() { return fBranch; }
  ///< Returns branch status
  int GetBranchStatus() const { return fBranchStatus; }
  ///< Returns Object
  TObject *   GetObject() const { return fObject; }
  std::string GetObjectClassName() const { return fObjectClassName; }

  TBranch * Branch(TTree * tree, void * address);
  void      SetAddress(void * address);
  void      SetBranchAddress(TTree * tree);
  void      SetBranchStatus(int status) { fBranchStatus = status; }

  Long64_t GetEntry(TTree * tree, Long64_t entry);
  void     SaveEntry(NTreeBranch * hnstIn, bool useProjection = false, const std::string projOpt = "OE");

  private:
  std::string fName{""};                   ///< Branch name
  int         fBranchStatus{1};            ///<! Branch status
  TBranch *   fBranch{nullptr};            ///<! Branch
  TObject *   fObject{nullptr};            ///<! Object
  std::string fObjectClassName{"TObject"}; ///< Object class name

  /// \cond CLASSIMP
  ClassDef(NTreeBranch, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
