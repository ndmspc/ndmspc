#include <TTree.h>
#include <string>
#include <TH1.h>
#include <THnSparse.h>
#include <TBranch.h>
#include <TObject.h>
#include "NLogger.h"

#include "NTreeBranch.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NTreeBranch);
/// \endcond

namespace Ndmspc {
NTreeBranch::NTreeBranch(TTree * tree, const std::string & name, void * address, const std::string & objClassName)
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
NTreeBranch::~NTreeBranch()
{
  ///
  /// Destructor
  ///
  // delete fObject;
  // delete fBranch;
}
TBranch * NTreeBranch::Branch(TTree * tree, void * address)
{
  ///
  /// Setting up branch
  ///
  if (!tree) {
    NLogError("Tree is nullptr !!!");
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
void NTreeBranch::SetAddress(void * address, bool deleteExisting)
{
  ///
  /// Setting up address
  ///
  NLogTrace("NTreeBranch::SetAddress: Setting address %p for branch '%s' ...", address, fName.c_str());

  if (fObject && deleteExisting) {
    NLogDebug("NTreeBranch::SetAddress: Deleting existing object %p for branch '%s' ...", fObject, fName.c_str());
    delete fObject;
    fObject = nullptr;
  }
  fObject = (TObject *)address;
  fBranch->SetAddress(&fObject);
}
void NTreeBranch::SetBranchAddress(TTree * tree)
{
  ///
  /// Setting up branch address
  ///

  if (!tree) {
    NLogError("Tree is nullptr !!!");
    return;
  }

  NLogTrace("NTreeBranch::SetBranchAddress: Setting branch address '%s' ...", fName.c_str());
  // fObject = nullptr;

  tree->SetBranchStatus(fName.c_str(), fBranchStatus);
  tree->SetBranchAddress(fName.c_str(), &fObject);
  fBranch = tree->GetBranch(fName.c_str());
}

Long64_t NTreeBranch::GetEntry(TTree * tree, Long64_t entry)
{
  ///
  /// Get entry
  ///

  if (tree == nullptr) {
    NLogError("Tree is not initialized !!!");
    return -1;
  }

  NLogTrace("Getting entry for branch '%s' %lld status=%d ...", fBranch->GetName(), entry,
                 tree->GetBranchStatus(fBranch->GetName()));

  Long64_t bytes = 0;
  if (fBranch && tree->GetBranchStatus(fBranch->GetName()) == 1) {
    bytes = fBranch->GetEntry(entry);
    NLogTrace("Getting content from %s with size %.3f MB", fBranch->GetName(), (double)bytes / (1024 * 1024));
    // if (fObject) {
    //   fObject->Print();
    // }
  }

  return bytes;
}
void NTreeBranch::SaveEntry(NTreeBranch * hnstBranchIn, bool useProjection, const std::string projOpt)
{
  ///
  /// Save entry
  ///
  NLogTrace("Saving entry for branch=%s ...", fName.c_str());

  TString classNameStr = hnstBranchIn->GetObjectClassName().c_str();
  // NLogDebug("NTreeBranch::SaveEntry: Obj class name %s ...", classNameStr.Data());
  if (classNameStr.BeginsWith("THnSparse")) {

    THnSparse * in = (THnSparse *)hnstBranchIn->GetObject();
    if (in) {
      if (in->GetNdimensions() > 0) {
        if (useProjection) {
          // in->Print();
          Int_t dims[in->GetNdimensions()];
          for (Int_t iDim = 0; iDim < in->GetNdimensions(); iDim++) {
            dims[iDim] = iDim;
          }
          THnSparse * out = (THnSparse *)in->ProjectionND(in->GetNdimensions(), dims, projOpt.c_str());
          // Loop over all bins
          double sum = 0;
          NLogTrace("Projection of %s with filled bins %lld ...", in->GetName(), out->GetNbins());
          for (Int_t i = 0; i < out->GetNbins(); i++) {
            NLogTrace("Bin %d content=%f", i, out->GetBinContent(i));
            sum += out->GetBinContent(i);
          }
          // out->Projection(0)->Print();
          out->SetEntries(sum);
          out->SetNameTitle(in->GetName(), in->GetTitle());
          // out->Print();
          SetAddress(out);
        }
        else {
          SetAddress(in);
        }
      }
    }
    else {
      SetAddress(nullptr);
    }
  }
  else {
    NLogTrace("Class '%s' is stored default method !!!", classNameStr.Data());
    // return;
  }
}

void NTreeBranch::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  NLogInfo("Branch name='%s' objClassName='%s' address=%p branch=%p status=%d", fName.c_str(),
                fObjectClassName.c_str(), fObject, fBranch, fBranchStatus);
}

} // namespace Ndmspc
