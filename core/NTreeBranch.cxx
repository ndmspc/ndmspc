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
    NLogger::Error("Tree is nullptr !!!");
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
void NTreeBranch::SetAddress(void * address)
{
  ///
  /// Setting up address
  ///
  fObject = (TObject *)address;
  fBranch->SetAddress(&fObject);
}
void NTreeBranch::SetBranchAddress(TTree * tree)
{
  ///
  /// Setting up branch address
  ///

  if (!tree) {
    NLogger::Error("Tree is nullptr !!!");
    return;
  }

  NLogger::Trace("Setting branch address '%s' ...", fName.c_str());
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
    NLogger::Error("Tree is not initialized !!!");
    return -1;
  }

  NLogger::Trace("Getting entry for branch '%s' %lld status=%d ...", fBranch->GetName(), entry,
                 tree->GetBranchStatus(fBranch->GetName()));

  Long64_t bytes = 0;
  if (fBranch && tree->GetBranchStatus(fBranch->GetName()) == 1) {
    bytes = fBranch->GetEntry(entry);
    NLogger::Trace("Getting content from %s with size %.3f MB", fBranch->GetName(), (double)bytes / (1024 * 1024));
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
  NLogger::Trace("Saving entry for branch=%s ...", fName.c_str());
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
        THnSparse * out = (THnSparse *)in->ProjectionND(in->GetNdimensions(), dims, projOpt.c_str());
        // Loop over all bins
        double sum = 0;
        NLogger::Trace("Projection of %s with filled bins %lld ...", in->GetName(), out->GetNbins());
        for (Int_t i = 0; i < out->GetNbins(); i++) {
          NLogger::Trace("Bin %d content=%f", i, out->GetBinContent(i));
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
    else {
      SetAddress(nullptr);
    }
  }
}

void NTreeBranch::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  NLogger::Info("Branch '%s' object='%s' address=%p branch=%p status=%d", fName.c_str(), fObjectClassName.c_str(),
                fObject, fBranch, fBranchStatus);
}

} // namespace Ndmspc
