#include <TFile.h>
#include <TTree.h>
#include <TStopwatch.h>
#include <THnSparse.h>
#include <TH1.h>
#include <omp.h>
#include <pthread.h>

#include "Logger.h"
#include "HnSparseTree.h"
#include "HnSparseTreeUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::HnSparseTreeUtils);
/// \endcond

namespace Ndmspc {
bool HnSparseTreeUtils::Read(int limit, std::string filename)
{
  auto logger = Ndmspc::Logger::getInstance("");
  TH1::AddDirectory(kFALSE);
  TStopwatch timer;
  timer.Start();
  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    logger->Error("File '%s' not found", filename.c_str());
    return false;
  }
  TTree * tree = (TTree *)file->Get("ndh");
  if (!tree) {
    logger->Error("Error: Tree 'ndh' not found");
    return false;
  }
  // tree->SetBranchStatus("*", 0);
  // tree->SetBranchStatus("unlikepm", 1);
  // tree->SetBranchStatus("mixingpm", 1);
  // tree->SetBranchStatus("unliketrue", 1);

  std::vector<THnSparse *> sparses;
  std::vector<std::string> names;
  std::vector<TBranch *>   branches;
  for (auto b : *tree->GetListOfBranches()) {
    names.push_back(b->GetName());
    sparses.push_back(nullptr);
  }
  int i = 0;
  for (auto n : names) {
    tree->SetBranchAddress(n.c_str(), &sparses[i]);
    branches.push_back(tree->GetBranch(n.c_str()));
    // tree->SetBranchStatus(n.c_str(), 1);
    logger->Info("Branch '%s' status=%d", n.c_str(), tree->GetBranchStatus(n.c_str()));
    i++;
  }

  Long64_t bytes    = 0;
  Long64_t bytessum = 0;
  for (int i = 0; i < tree->GetEntriesFast(); i++) {
    if (i >= limit) break;
    logger->Info("Entry=%d", i);
    // Printfrintf("%.3f MB", (double)tree->GetEntry(i) / (1024 * 1024));
    // Printf("%.3f MB", (double)bs->GetEntry(i) / (1024 * 1024));
    // Printf("%.3f MB", (double)bm->GetEntry(i) / (1024 * 1024));
    for (auto b : branches) {

      if (b && tree->GetBranchStatus(b->GetName()) == 1) {
        bytes = b->GetEntry(i);
        bytessum += bytes;
        logger->Info("Getting content from %s with size %.3f MB", b->GetName(), (double)bytes / (1024 * 1024));
      }
    }

    // int j = 0;
    // for (auto s : sparses) {
    //   // Printf("sparse=%p", s);
    //   // if (s && tree->GetBranchStatus(branches[j]->GetName()) == 1) {
    //   if (s && s->GetNdimensions() > 0) {
    //     s->Print();
    //     s->Projection(0)->Print();
    //   }
    //   j++;
    // }
  }
  timer.Stop();
  timer.Print();
  logger->Info("Total bytes read: %.3f MB", (double)bytessum / (1024 * 1024));
  return true;
}

bool HnSparseTreeUtils::ReadNew(int limit, std::string filename)
{
  Logger * logger = Ndmspc::Logger::getInstance("");
  TH1::AddDirectory(kFALSE);
  TStopwatch timer;
  timer.Start();
  HnSparseTree * hnst = Ndmspc::HnSparseTree::Load(filename);

  // hnst->Print();
  Long64_t bytessum = 0;
  for (int i = 0; i < hnst->GetTree()->GetEntriesFast(); i++) {
    if (i >= limit) break;
    bytessum += hnst->GetEntry(i);
  }

  for (auto & kv : hnst->GetBranchesMap()) {
    THnSparse * s = (THnSparse *)kv.second.GetObject();
    if (s && s->GetNdimensions() > 0) {
      s->Print();
      s->Projection(0)->Print();
    }
  }

  logger->Info("Total bytes read: %.3f MB", (double)bytessum / (1024 * 1024));

  return true;
}

bool HnSparseTreeUtils::Import(const Ndmspc::Config & c, std::string filename, int limit, std::string className)
{
  ///
  /// Import from config
  ///
  ///

  auto          logger = Ndmspc::Logger::getInstance("");
  HnSparseTreeC hnst;
  hnst.InitTree(filename, "ndh");

  std::vector<std::vector<std::string>> objsExpressionsIn;
  std::vector<std::string>              aliasesIn;
  c.GetInputObjectNames(objsExpressionsIn, aliasesIn);

  // Get list of unique object names from objsExpressionsIn
  std::vector<std::string> objNames;
  for (auto & obj : objsExpressionsIn) {
    for (auto & o : obj) {
      // logger->Debug("Adding object '%s' ...", o.c_str());
      objNames.push_back(o);
    }
  }
  for (auto & n : objNames) {
    THnSparseD * hns = nullptr;
    // logger->Debug("Adding branch '%s' hns=%p ...", n.c_str(), hns);
    hnst.AddBranch(n, hns, className);
  }
  // return true;

  std::string filenameIn = c.GetInputMap()->GetInputFileName(0);
  logger->Info("Opening file='%s' obj='%s' ...", filenameIn.c_str(), objNames[0].c_str());
  TFile * file = TFile::Open(filenameIn.c_str());
  if (!file) {
    logger->Error("Cannot open file '%s'", filenameIn.c_str());
    return false;
  }
  std::string objDir = c.GetInputObjectDirectory() + "/";
  THnSparse * hn     = dynamic_cast<THnSparse *>(file->Get((objDir + objNames[0]).c_str()));
  if (!hn) {
    logger->Error("Cannot get object '%s' from file '%s'", objNames[0].c_str(), filenameIn.c_str());
    return false;
  }

  TAxis *     a;
  TObjArray * axesInputMap = (TObjArray *)c.GetInputMap()->GetMap()->GetListOfAxes()->Clone();
  TObjArray * axesData     = (TObjArray *)hn->GetListOfAxes()->Clone();
  file->Close();

  TObjArray * newAxis = new TObjArray();
  for (Int_t iDim = 0; iDim < axesInputMap->GetEntries(); iDim++) {
    a = (TAxis *)axesInputMap->At(iDim);
    newAxis->Add(a);
  }
  for (Int_t iDim = 0; iDim < axesData->GetEntries(); iDim++) {
    a = (TAxis *)axesData->At(iDim);
    // a->Set(1, a->GetXmin(), a->GetXmax());
    // a->Set(1, a->GetXmin(), a->GetXmax());
    newAxis->Add(a);
  }

  hnst.InitAxes(newAxis, axesInputMap->GetEntries());
  //
  THnSparse * hMap = c.GetInputMap()->GetMap();
  for (int i = 0; i < hMap->GetNbins(); i++) {
    if (limit > 0 && i >= limit) {
      break;
    }
    Int_t p[hnst.GetNdimensions()];
    hMap->GetBinContent(i, p);
    for (int j = 0; j < axesInputMap->GetEntries(); j++) {
      hnst.SetPointAt(j, p[j]);
    }
    for (int j = 0; j < axesData->GetEntries(); j++) {
      // a = (TAxis *)axesData->At(j);
      // hnst.SetPointAt(j + axesInputMap->GetEntries(), a->GetNbins());
      hnst.SetPointAt(j + axesInputMap->GetEntries(), 1);
    }

    std::string fn = c.GetInputMap()->GetInputFileName(i);
    ImportSingle(&hnst, fn, objNames);
  }

  hnst.Close();
  //
  // // GetAxis(1)->SetRange(7, 7);
  // // AddProjectionIndexes();
  // // GetAxis(1)->SetRange(6, 6);
  // // AddProjectionIndexes();
  // // GetAxis(1)->SetRange();
  //
  // // std::vector<Long64_t> indexes = GetProjectionIndexes();
  // // for (auto & i : indexes) {
  // //   logger->Info("Index=%lld", i);
  // // }
  //

  return true;
}
bool HnSparseTreeUtils::ImportSingle(HnSparseTree * hnst, std::string filename, std::vector<std::string> objnames)
{
  ///
  /// Create instance from file and object name
  ///
  auto logger = Ndmspc::Logger::getInstance("");

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    logger->Error("Cannot open file '%s'", filename.c_str());
    return false;
  }

  TObject *        s    = nullptr;
  Ndmspc::Config & c    = Ndmspc::Config::Instance();
  std::string      path = c.GetInputObjectDirectory();
  int              i    = -1;
  for (auto & name : objnames) {
    i++;
    s = file->Get((path + "/" + name).c_str());
    if (s) s->Print();

    hnst->GetBranch(name)->SetAddress(s);
  }

  hnst->FillTree();
  file->Close();
  return true;
}

bool HnSparseTreeUtils::Distribute(const std::string & in, const std::string & out, const std::string & filename,
                                   int level)
{
  ///
  /// Distribute
  ///

  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Distributing '%s' ...", in.c_str());

  HnSparseTree * hnst = Ndmspc::HnSparseTree::Load(in);

  // hnst->Print();
  Long64_t bytessum = 0;

  // hnst->GetAxis(1)->SetRange(6, 6);
  hnst->GetIndexes().clear();
  hnst->AddProjectionIndexes();
  std::vector<Long64_t>         indexes = hnst->GetProjectionIndexes();
  Int_t                         point[hnst->GetNdimensions()];
  std::vector<std::vector<int>> points;
  for (auto & i : indexes) {
    bytessum += hnst->GetEntry(i);
    hnst->GetBinContent(i, point);

    std::vector<int> p;
    for (int j = 0; j < hnst->GetNdimensions(); j++) {
      p.push_back(point[j]);
    }
    std::string   axes_path   = hnst->GetAxes().GetPath(point);
    std::string   outFileName = out + "/" + axes_path + "/" + filename;
    HnSparseTreeC hnstOut;
    hnstOut.SetFileName(outFileName);

    hnstOut.InitTree();
    hnstOut.InitAxes(hnst->GetListOfAxes(), level);
    // hnstOut.SetAxes(hnst->GetAxes());
    // Set branches map
    hnstOut.SetBranchesMap(hnst->GetBranchesMap());
    // TODO: Make it more general and use IterateNDimensionalSpace
    for (int iEntry = 0; iEntry < hnst->GetAxis(5)->GetNbins(); iEntry++) {

      // if (i > 0) break;
      std::vector<std::vector<int>> ranges = {{5 - 4, iEntry + 1, iEntry + 1}};
      hnstOut.SaveEntry(hnst, iEntry, ranges, true);
    }
    hnstOut.GetAxes().Print();
    hnstOut.Close();
    logger->Info("Output file='%s' ...", outFileName.c_str());
    points.push_back(p);
  }

  logger->Info("Total bytes read: %.3f MB", (double)bytessum / (1024 * 1024));
  std::string   outFileName = out + "/" + filename;
  HnSparseTreeC hnstMap;
  hnstMap.SetAxes(hnst->GetAxes());
  hnstMap.InitAxes(hnst->GetListOfAxes(), level);
  hnstMap.InitTree(outFileName, "ndh");
  hnstMap.FillPoints(points);
  hnstMap.Close();

  return true;
}

void HnSparseTreeUtils::IterateNDimensionalSpace(const std::vector<int> & minBounds, const std::vector<int> & maxBounds,
                                                 const std::function<void(const std::vector<int> &)> & userFunction)
{
  ///
  /// Iterate N-dimensional space
  ///
  std::vector<int> point(minBounds.size(), 0);
  for (int i = 0; i < minBounds.size(); i++) {
    point[i] = minBounds[i];
  }

  while (point[0] <= maxBounds[0]) {
    userFunction(point);
    point[minBounds.size() - 1]++;
    for (int i = minBounds.size() - 1; i > 0; i--) {
      if (point[i] > maxBounds[i]) {
        point[i] = minBounds[i];
        point[i - 1]++;
      }
    }
  }
}

void HnSparseTreeUtils::IterateNDimensionalSpaceParallel(
    const std::vector<int> & minBounds, const std::vector<int> & maxBounds,
    const std::function<void(const std::vector<int> &)> & userFunction, int numThreads)
{

  Logger * logger = Logger::getInstance("");
  logger->Info("[TODO]Iterating N-dimensional space in parallel ...");
  if (minBounds.size() != maxBounds.size() || minBounds.empty()) {
    std::cerr << "Error: Invalid bounds dimensions" << std::endl;
    return;
  }

  const int dimensions = minBounds.size();

  // Calculate total number of points
  long long              totalPoints = 1;
  std::vector<long long> dimensionSizes(dimensions);
  for (int i = 0; i < dimensions; i++) {
    dimensionSizes[i] = maxBounds[i] - minBounds[i] + 1;
    totalPoints *= dimensionSizes[i];
  }

  if (numThreads > 0) {
    omp_set_num_threads(numThreads);
  }

// Process points in parallel
#pragma omp parallel
  {
    std::vector<int> point(dimensions);

#pragma omp for schedule(dynamic)
    for (long long idx = 0; idx < totalPoints; idx++) {
      // Convert linear index to n-dimensional point
      long long remaining = idx;
      for (int dim = dimensions - 1; dim >= 0; dim--) {
        long long dimSize = dimensionSizes[dim];
        point[dim]        = minBounds[dim] + (remaining % dimSize);
        remaining /= dimSize;
      }

      // Apply user function to the current point
      userFunction(point);
    }
  }
}

} // namespace Ndmspc
