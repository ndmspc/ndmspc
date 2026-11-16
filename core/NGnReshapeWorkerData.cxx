#include <memory>

#include "NBinning.h"
#include "NBinningDef.h"
#include "NGnReshapeWorkerData.h"
#include "NLogger.h"
#include "NStorageTree.h"
#include "NUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnReshapeWorkerData);
/// \endcond

namespace Ndmspc {
NGnReshapeWorkerData::NGnReshapeWorkerData() : NThreadData() {}
NGnReshapeWorkerData::~NGnReshapeWorkerData()
{
  for (auto & [index, subtree] : fSubtrees) {
    delete subtree;
  }
  fSubtrees.clear();
  if (fTree) {
    fTree->Close(false);
    delete fTree;
    fTree = nullptr;
  }
}

bool NGnReshapeWorkerData::Init(size_t id, NGnTree * source, const std::string & binningName,
                                std::vector<std::vector<int>> levels, std::map<int, std::vector<int>> ranges,
                                std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Open this worker's private tree and record what to reshape
  ///

  SetAssignedIndex(id);
  SetThreadId(std::this_thread::get_id());

  if (source == nullptr || source->GetStorageTree() == nullptr || source->GetStorageTree()->GetTree() == nullptr) {
    NLogError("NGnReshapeWorkerData::Init: Source tree is null !!!");
    return false;
  }

  fLevels     = std::move(levels);
  fRanges     = std::move(ranges);
  fRangesBase = std::move(rangesBase);

  NStorageTree *    storage  = source->GetStorageTree();
  fFileName                  = storage->GetFileName();
  fTreeName                  = storage->GetTree()->GetName();

  fBinningName = binningName;
  if (fBinningName.empty()) fBinningName = source->GetBinning()->GetCurrentDefinitionName();

  NLogTrace("NGnReshapeWorkerData::Init: worker %zu will reshape '%s' from '%s'", id, fBinningName.c_str(),
            fFileName.c_str());
  return true;
}

bool NGnReshapeWorkerData::OpenTree()
{
  ///
  /// Open this worker's private tree, on the thread that will read it
  ///

  if (fFileName.empty() || fTreeName.empty()) {
    NLogError("NGnReshapeWorkerData::OpenTree: No file/tree recorded for worker %zu !!!", GetAssignedIndex());
    return false;
  }

  // Each worker reads the tree through objects of its own: its own TFile and TTree buffers, its own
  // NBinning, and therefore its own NBinningDef::GetContent() sparse, which Reshape mutates as it sets
  // axis ranges.
  fTree = NGnTree::Open(fFileName, "", fTreeName);
  if (fTree == nullptr || fTree->IsZombie()) {
    NLogError("NGnReshapeWorkerData::OpenTree: Cannot open '%s' for worker %zu !!!", fFileName.c_str(),
              GetAssignedIndex());
    fTree = nullptr;
    return false;
  }

  if (fTree->GetBinning()->GetDefinition(fBinningName, true) == nullptr) {
    NLogError("NGnReshapeWorkerData::OpenTree: Binning definition '%s' not found in '%s' !!!", fBinningName.c_str(),
              fFileName.c_str());
    return false;
  }
  return true;
}

void NGnReshapeWorkerData::Process(const std::vector<int> & coords)
{
  ///
  /// Reshape one bin of the first layer
  ///

  // The tree is opened here, on the worker's own thread, not in Init(): ROOT requires a TFile/TTree to
  // be opened and read by one and the same thread (and with implicit MT enabled, see ReshapeBins).
  if (fTree == nullptr && !OpenTree()) return;
  if (fTree == nullptr) return;

  NBinningDef * binningDef = fTree->GetBinning()->GetDefinition(fBinningName, true);
  if (binningDef == nullptr) {
    NLogError("NGnReshapeWorkerData::Process: Binning definition '%s' is null !!!", fBinningName.c_str());
    return;
  }

  // ReshapeBins records fLevel/fNLevels on the navigator it is called on, so each worker gets one of its
  // own. Named by default, exactly like the serial entry point (NGnTree::Reshape), so the nodes this
  // creates carry the same names they would have in a serial reshape.
  std::unique_ptr<NGnNavigator> worker(new NGnNavigator());
  worker->SetGnTree(fTree);

  NGnNavigator * root = worker->ReshapeBins(binningDef, fLevels, 0, fRanges, fRangesBase, nullptr, coords, coords);
  if (root == nullptr) {
    NLogError("NGnReshapeWorkerData::Process: Reshape failed for bin %s", NUtils::GetCoordsString(coords).c_str());
    return;
  }

  // The bin built exactly one child, at its projection cell. Take the children out and drop the
  // now-empty node: its projection belongs to this worker's own tree.
  std::vector<NGnNavigator *> children = root->TakeChildren();
  for (size_t i = 0; i < children.size(); i++) {
    if (children[i] == nullptr) continue;
    children[i]->SetParent(nullptr);
    fSubtrees[(int)i] = children[i];
  }
  delete root;

  fNReshaped++;
}

std::map<int, NGnNavigator *> NGnReshapeWorkerData::TakeSubtrees()
{
  ///
  /// Hand over the subtrees built by this worker
  ///
  std::map<int, NGnNavigator *> subtrees = std::move(fSubtrees);
  fSubtrees.clear();
  return subtrees;
}
} // namespace Ndmspc
