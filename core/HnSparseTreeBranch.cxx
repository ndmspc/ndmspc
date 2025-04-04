#include <TTree.h>
#include <string>
#include "THnSparse.h"
#include <TBranch.h>
#include <TObject.h>
#include "Logger.h"

#include "HnSparseTreeBranch.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::HnSparseTreeBranch);
/// \endcond

namespace Ndmspc {
HnSparseTreeBranch::HnSparseTreeBranch(TTree * tree, const std::string & name, void * address,
                                       const std::string & objClassName)
    : TObject(), fName(name), fObjectClassName(objClassName)
{
  ///
  /// Constructor
  ///
  // SetAddress(address);
  if (tree) {
    Branch(tree, address);
  }
}
HnSparseTreeBranch::~HnSparseTreeBranch()
{
  ///
  /// Destructor
  ///
  // delete fObject;
  // delete fBranch;
}
TBranch * HnSparseTreeBranch::Branch(TTree * tree, void * address)
{
  ///
  /// Setting up branch
  ///
  if (!tree) {
    auto logger = Ndmspc::Logger::getInstance("");
    logger->Error("Tree is nullptr !!!");
    return nullptr;
  }

  if (tree->GetBranch(fName.c_str())) {
    fBranch = tree->GetBranch(fName.c_str());
    return fBranch;
  }
  // fBranch = tree->Branch(fName.c_str(), fObjectClassName.c_str(), &address);
  tree->Branch(fName.c_str(), fObjectClassName.c_str(), &address);
  fBranch = tree->GetBranch(fName.c_str());
  return fBranch;
}
void HnSparseTreeBranch::SetAddress(void * address)
{
  ///
  /// Setting up address
  ///
  fObject = (TObject *)address;
  fBranch->SetAddress(&fObject);
}
void HnSparseTreeBranch::SetBranchAddress(TTree * tree)
{
  ///
  /// Setting up branch address
  ///

  auto logger = Ndmspc::Logger::getInstance("");
  if (!tree) {
    logger->Error("Tree is nullptr !!!");
    return;
  }

  // logger->Info("Setting branch address '%s' ...", fName.c_str());
  // fObject = nullptr;

  tree->SetBranchStatus(fName.c_str(), fBranchStatus);
  tree->SetBranchAddress(fName.c_str(), &fObject);
  fBranch = tree->GetBranch(fName.c_str());
}

Long64_t HnSparseTreeBranch::GetEntry(TTree * tree, Long64_t entry)
{
  ///
  /// Get entry
  ///
  auto logger = Ndmspc::Logger::getInstance("");

  Long64_t bytes = 0;
  if (fBranch && tree->GetBranchStatus(fBranch->GetName()) == 1) {
    bytes = fBranch->GetEntry(entry);
    // logger->Trace("Getting content from %s with size %.3f MB", fBranch->GetName(), (double)bytes / (1024 * 1024));
    // if (fObject) {
    //   fObject->Print();
    // }
  }

  return bytes;
}
void HnSparseTreeBranch::SaveEntry(HnSparseTreeBranch * hnstBranchIn, bool useProjection)
{
  ///
  /// Save entry
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Saving entry for branch=%s ...", fName.c_str());
  // return;

  THnSparse * in = (THnSparse *)hnstBranchIn->GetObject();
  if (in) {
    if (in->GetNdimensions() > 0) {
      if (useProjection) {
        // in->Print();
        Int_t dims[in->GetNdimensions()];
        for (Int_t iDim = 0; iDim < in->GetNdimensions(); iDim++) {
          dims[iDim] = iDim;
        }
        THnSparse * out = (THnSparse *)in->ProjectionND(in->GetNdimensions(), dims, "O");
        out->Print();
        out->SetNameTitle(in->GetName(), in->GetTitle());
        SetAddress(out);
      }
      else {
        SetAddress(in);
      }
    }
    else {
      SetAddress(nullptr);
    }
  }
}

void HnSparseTreeBranch::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Branch '%s' object='%s' address=%p", fName.c_str(), fObjectClassName.c_str(), fObject);
}

} // namespace Ndmspc
