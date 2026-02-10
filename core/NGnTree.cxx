#include <cstddef>
#include <ctime>
#include <numbers>
#include <string>
#include <vector>
#include "TAxis.h"
#include <TDirectory.h>
#include <TObject.h>
#include <TList.h>
#include <TROOT.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TCanvas.h>
#include <TSystem.h>
#include <TMap.h>
#include <TObjString.h>
#include <TTree.h>
#include <TBufferJSON.h>
#include <sys/poll.h>
#include "NParameters.h"
#include "NStorageTree.h"
#include "NBinning.h"
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NGnThreadData.h"
#include "NLogger.h"
#include "NTreeBranch.h"
#include "NUtils.h"
#include "NStorageTree.h"
#include "NWsClient.h"
#include "NGnNavigator.h"
#include "NGnTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnTree);
/// \endcond

namespace Ndmspc {
/**
 * @brief Global pointer to the HTTP handler map.
 *
 * This variable holds a pointer to the global HttpHandlerMap instance,
 * which is used to manage HTTP request handlers within the application.
 * It is initialized to nullptr and should be set during application startup.
 */

NGnTree::NGnTree() : TObject() {}

NGnTree::NGnTree(std::vector<TAxis *> axes, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (axes.empty()) {
    NLogError("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}
NGnTree::NGnTree(TObjArray * axes, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///

  if (axes == nullptr) {
    NLogError("NGnTree::NGnTree: Axes TObjArray is nullptr.");
    MakeZombie();
    return;
  }

  if (axes == nullptr && axes->GetEntries() == 0) {
    NLogError("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}

NGnTree::NGnTree(NGnTree * ngnt, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (ngnt == nullptr) {
    NLogError("NGnTree::NGnTree: NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  if (ngnt->GetBinning() == nullptr) {
    NLogError("NGnTree::NGnTree: Binning in NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  // TODO: Import binning from user
  fBinning     = (NBinning *)ngnt->GetBinning()->Clone();
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}

NGnTree::NGnTree(NBinning * b, NStorageTree * s) : TObject(), fBinning(b), fTreeStorage(s), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (fBinning == nullptr) {
    NLogError("NGnTree::NGnTree: Binning is nullptr.");
    MakeZombie();
  }
  if (s == nullptr) {
    fTreeStorage = new NStorageTree(fBinning);
    fTreeStorage->InitTree("", "ngnt");
  }

  if (fTreeStorage == nullptr) {
    NLogError("NGnTree::NGnTree: Storage tree is nullptr.");
    MakeZombie();
  }

  // fBinning->Initialize();
  //
  // TODO: Check if this is needed
  fTreeStorage->SetBinning(fBinning);
  fBinning->GetPoint()->SetTreeStorage(fTreeStorage);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}
NGnTree::NGnTree(THnSparse * hns, std::string parameterAxis, const std::string & outFileName, json cfg)
    : TObject(), fInput(nullptr)
{
  ///
  /// Import THnSparse as NGnTree and use given parameter axis as result parameter
  ///

  std::map<std::string, std::vector<std::vector<int>>> b;
  TObjArray *                                          axes             = new TObjArray();
  int                                                  parameterAxisIdx = -1;
  std::vector<std::string>                             labels;
  for (int i = 0; i < hns->GetNdimensions(); i++) {
    TAxis * axisIn = (TAxis *)hns->GetAxis(i);
    TAxis * axis   = (TAxis *)axisIn->Clone();

    // check if parameterAxis matches axis name
    if (parameterAxis.compare(axis->GetName()) == 0) {
      parameterAxisIdx = i;
      TAxis * axis     = hns->GetAxis(parameterAxisIdx);
      for (int bin = 1; bin <= axis->GetNbins(); bin++) {
        // NLogInfo("Axis bin %d label: %s", bin, axis->GetBinLabel(bin));
        labels.push_back(axis->GetBinLabel(bin));
      }
      continue;
    }

    // set label
    if (axisIn->IsAlphanumeric()) {
      NLogTrace("Setting axis '%s' labels from input THnSparse", axis->GetName());
      for (int bin = 1; bin <= axisIn->GetNbins(); bin++) {
        std::string label = axisIn->GetBinLabel(bin);
        if (!labels.empty()) axis->SetBinLabel(bin, axisIn->GetBinLabel(bin));
      }
    }

    axes->Add(axis);
    b[axis->GetName()] = {{1}};
  }

  // json cfg;
  cfg["_parameterAxis"] = parameterAxisIdx;
  cfg["_labels"]        = labels;

  // return nullptr;
  NLogDebug("Importing THnSparse as NGnTree with parameter axis '%s' (index %d) ...", parameterAxis.c_str(),
            parameterAxisIdx);
  NGnTree * ngnt = new NGnTree(axes, outFileName);
  NLogDebug("Created NGnTree for THnSparse import ...");
  if (ngnt->IsZombie()) {
    NLogError("NGnTree::Import: Failed to create NGnTree !!!");
    MakeZombie();
    return;
  }
  // ngnt->GetStorageTree()->GetTree()->GetUserInfo()->Add(hns->Clone());
  // Get env variable for tmp dir $NDMSPC_TMP_DIR
  const char * tmpDirStr = gSystem->Getenv("NDMSPC_TMP_DIR");

  std::string tmpDir;

  if (!tmpDirStr || tmpDir.empty()) {
    tmpDir = "/tmp";
  }
  std::string tmpFilename = tmpDir + "/ngnt_imported_input" + std::to_string(gSystem->GetPid()) + ".root";
  NGnTree *   ngntIn      = new NGnTree(axes, tmpFilename);
  if (ngntIn->IsZombie()) {
    NLogError("NGnTree::Import: Failed to create NGnTree for input !!!");
    SafeDelete(ngnt);
    MakeZombie();
    return;
  }
  // ngntIn->GetStorageTree()->GetTree()->GetUserInfo()->Add(hns->Clone());
  ngntIn->GetOutput("default")->Add(hns->Clone("test"));
  ngntIn->Close(true);

  // return;
  // delete ngntIn;

  ngnt->SetInput(NGnTree::Open(tmpFilename)); // Set input to self

  ngnt->GetInput()->Print();

  ngnt->GetBinning()->AddBinningDefinition("default", b);
  ngnt->InitParameters(cfg["_labels"].get<std::vector<std::string>>());

  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    // point->Print();
    json cfg = point->GetCfg();

    NGnTree * ngntIn = point->GetInput();
    if (!ngntIn) {
      NLogError("NGnTree::Import: Input NGnTree is nullptr !!!");
      return;
    }
    // ngntIn->Print();

    THnSparse * hns = (THnSparse *)ngntIn->GetOutput("default")->At(0);
    if (hns == nullptr) {
      NLogError("NGnTree::Import: THnSparse 'hns' not found in storage tree !!!");
      return;
    }

    int                           axisIdx = cfg["_parameterAxis"].get<int>();
    std::vector<std::vector<int>> ranges;
    // set ranges from point storage coords
    int iAxis = 0;
    for (int i = 0; i < hns->GetNdimensions(); i++) {
      if (i == axisIdx) continue; // skip parameter axis
      int coord = point->GetStorageCoords()[iAxis++];
      ranges.push_back({i, coord, coord});
      // NLogInfo("Setting axis %d range to [%d, %d]", i, coord, coord);
    }
    NUtils::SetAxisRanges(hns, ranges);
    TH1 * h = hns->Projection(axisIdx, "O");
    if (h->GetEntries() > 0) {
      NParameters * params = point->GetParameters();
      if (params) {
        for (int bin = 1; bin <= h->GetNbinsX(); bin++) {
          params->SetParameter(bin, h->GetBinContent(bin), h->GetBinError(bin));
        }
      }
      // outputPoint->Add(hParams);
      outputPoint->Add(h);

      // Get all objects
      TFile * f = TFile::Open(cfg["filename"].get<std::string>().c_str());
      if (f->IsZombie()) {
        NLogError("NGnTree::Import: Cannot open file '%s' !!!", cfg["filename"].get<std::string>().c_str());
        return;
      }

      std::string objFormatMinMax =
          cfg["objectFormatMinMax"].is_string() ? cfg["objectFormatMinMax"].get<std::string>() : "";
      std::string objFormatAxis =
          cfg["objectFormatAxis"].is_string() ? cfg["objectFormatAxis"].get<std::string>() : "_";
      // loop over all object in cfg["objects"]
      for (auto & [objName, objCfg] : cfg["objects"].items()) {
        std::string objPath = objCfg["prefix"].get<std::string>();

        for (auto & axisName : cfg["axes"]) {
          if (objFormatMinMax.empty()) {
            objPath += std::to_string(point->GetBin(axisName.get<std::string>()));
          }
          else {
            double min = point->GetBinMin(axisName.get<std::string>());
            double max = point->GetBinMax(axisName.get<std::string>());
            objPath += TString::Format(objFormatMinMax.c_str(), min, max).Data();
          }
          objPath += objFormatAxis;
        }

        // remove trailing underscore
        objPath = objPath.substr(0, objPath.size() - objFormatAxis.size());

        TObject * obj = f->Get(objPath.c_str());
        if (!obj) {
          NLogError("NGnTree::Import: Cannot get object '%s' from file '%s' !!!", objPath.c_str(),
                    cfg["filename"].get<std::string>().c_str());
          continue;
        }
        NLogTrace("NGnTree::Import: Retrieved object '%s' storting to '%s' ...", objPath.c_str(), objName.c_str());
        // outputPoint->Add(obj->Clone(objName.c_str()));
        if (obj->InheritsFrom(TCanvas::Class())) {
          TCanvas * cObj = (TCanvas *)obj;
          cObj->SetName(objName.c_str());
          if (cObj->GetWw() == 0.0 || cObj->GetWh() == 0.0) {
            NLogWarning("gPad has at least one zero dimension.");
            return;
          }
          obj = cObj->Clone();
          // Prevent ROOT from managing cleanup through global lists to avoid RecursiveRemove crashes
          ((TCanvas *)obj)->SetBit(kMustCleanup, kFALSE);
          // Also disable cleanup for primitives inside the canvas
          TList * primitives = ((TCanvas *)obj)->GetListOfPrimitives();
          if (primitives) {
            primitives->SetBit(kMustCleanup, kFALSE);
            TIter next(primitives);
            TObject * prim;
            while ((prim = next())) {
              prim->SetBit(kMustCleanup, kFALSE);
            }
          }
        }
        outputPoint->Add(obj);
      }
      f->Close();
    }
  };

  // NUtils::SetAxisRanges(, std::vector<std::vector<int>> ranges)
  ngnt->Process(processFunc, cfg);
  ngnt->Close(true);
  // Remove tmp file
  gSystem->Exec(TString::Format("rm -f %s", tmpFilename.c_str()));
}

NGnTree::~NGnTree()
{
  ///
  /// Destructor
  ///

  SafeDelete(fBinning);
  // SafeDelete(fTreeStorage);
  // SafeDelete(fNavigator);
  SafeDelete(fWsClient);
  SafeDelete(fParameters);
}
void NGnTree::Print(Option_t * option) const
{
  ///
  /// Print object
  ///

  TString opt = option;

  // Print list of axes
  NLogInfo("NGnTree::Print: Printing NGnTree object [ALL] ...");
  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogError("Binning is not initialized in NGnTree !!!");
  }
  if (fTreeStorage) {
    fTreeStorage->Print(option);
  }
  else {
    NLogError("Storage tree is not initialized in NGnTree !!!");
  }
}

void NGnTree::Draw(Option_t * /*option*/)
{
  ///
  /// Draw object
  ///

  NLogInfo("NGnTree::Draw: Drawing NGnTree object [not implemented yet]...");
}

bool NGnTree::Process(NGnProcessFuncPtr func, const json & cfg, std::string binningName, NGnBeginFuncPtr beginFunc,
                      NGnEndFuncPtr endFunc)
{
  ///
  /// Process the sparse object with the given function
  ///

  if (!fBinning) {
    NLogError("Binning is not initialized in NGnTree !!!");
    return false;
  }

  NBinning * binningIn = (NBinning *)fBinning->Clone();

  std::vector<std::string> defNames = fBinning->GetDefinitionNames();
  if (!binningName.empty()) {
    // Check if binning definitions exist
    if (std::find(defNames.begin(), defNames.end(), binningName) == defNames.end()) {
      NLogError("Binning definition '%s' not found in NGnTree !!!", binningName.c_str());
      return false;
    }
    defNames.clear();
    defNames.push_back(binningName);
  }

  fBinning->Reset();
  fBinning->SetCfg(cfg); // Set configuration to binning point
  bool rc = Process(func, defNames, cfg, binningIn, beginFunc, endFunc);
  if (!rc) {
    NLogError("NGnTree::Process: Processing failed !!!");
    return false;
  }
  // bool rc = false;
  return true;
}

bool NGnTree::Process(NGnProcessFuncPtr func, const std::vector<std::string> & defNames, const json & cfg,
                      NBinning * binningIn, NGnBeginFuncPtr beginFunc, NGnEndFuncPtr endFunc)
{
  ///
  /// Process the sparse object with the given function
  ///

  NLogInfo("NGnTree::Process: Starting processing with %zu definitions ...", defNames.size());
  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  TH1::AddDirectory(kFALSE);

  NUtils::EnableMT();

  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use
  if (nThreads < 1) nThreads = 1;
  const char * wsUrl = gSystem->Getenv("NDMSPC_WS_URL");
  if (!fWsClient && wsUrl) {
    fWsClient = new NWsClient();
    fWsClient->Connect(wsUrl);
    NLogInfo("NGnTree::Process: Connected to WebSocket server at '%s'", wsUrl);
  }

  std::vector<Ndmspc::NGnThreadData> threadDataVector(nThreads);

  const char * tmpDirEnv = gSystem->Getenv("NDMSPC_TMP_DIR");
  std::string  tmpDir    = tmpDirEnv ? tmpDirEnv : "/tmp";
  TString      tmpDirStr = fTreeStorage->GetPrefix();
  // check if tmpDir starts with root:// or http:// or https://
  if (!(tmpDirStr.BeginsWith("root://") || tmpDirStr.BeginsWith("http://") || tmpDirStr.BeginsWith("https://"))) {
    tmpDir = tmpDirStr.Data();
  }

  std::string jobDir     = tmpDir + "/.ndmspc/tmp/" + std::to_string(gSystem->GetPid());
  std::string filePrefix = jobDir;
  for (size_t i = 0; i < threadDataVector.size(); ++i) {
    std::string filename = filePrefix + "/" + std::to_string(i) + "/" + fTreeStorage->GetPostfix();
    bool        rc       = threadDataVector[i].Init(i, func, beginFunc, endFunc, this, binningIn, fInput, filename,
                                                    fTreeStorage->GetTree()->GetName());
    if (!rc) {
      NLogError("Failed to initialize thread data %zu, exiting ...", i);
      return false;
    }
    threadDataVector[i].SetCfg(cfg); // Set configuration to binning point
  }
  size_t processedEntries = 0;
  size_t totalEntries     = 0;
  auto   start_par        = std::chrono::high_resolution_clock::now();
  auto   task             = [&](const std::vector<int> & coords, Ndmspc::NGnThreadData & thread_obj) {
    // NLogWarning("Processing coordinates %s in thread %zu", NUtils::GetCoordsString(coords).c_str(),
    //                       thread_obj.GetAssignedIndex());
    // thread_obj.Print();
    json progress;
    progress["jobName"]  = thread_obj.GetHnSparseBase()->GetBinning()->GetCurrentDefinitionName();
    progress["status"]   = "R";
    progress["task"]     = coords[0];
    NWsClient * wsClient = thread_obj.GetHnSparseBase()->GetWsClient();
    if (wsClient) wsClient->Send(progress.dump());
    thread_obj.Process(coords);
    processedEntries++;
    progress["status"] = "D";
    if (wsClient) wsClient->Send(progress.dump());

    if (!NLogger::GetConsoleOutput()) {
      size_t nRunning = (totalEntries - processedEntries >= threadDataVector.size()) ? threadDataVector.size()
                                                                                                   : totalEntries - processedEntries;
      NUtils::ProgressBar(processedEntries, totalEntries, start_par, TString::Format("R%4zu", nRunning).Data());
    }
  };
  size_t iDef   = 0;
  int    sumIds = 0;

  for (auto & name : defNames) {
    auto binningDef = binningIn->GetDefinition(name);
    if (!binningDef) {
      NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
      return false;
    }
    std::vector<int> mins, maxs;
    mins.push_back(0);
    maxs.push_back(binningDef->GetIds().size() - 1);
    NLogDebug("NGnTree::Process: Processing with binning definition '%s' with %zu entries", name.c_str(),
              binningDef->GetIds().size());

    if (NLogger::GetConsoleOutput()) {
      NLogInfo("NGnTree::Process: Processing binning definition '%s' with %d tasks ...", name.c_str(), maxs[0] + 1);
    }
    else {
      Printf("Processing binning definition '%s' with %d tasks ...", name.c_str(), maxs[0] + 1);
    }
    totalEntries = maxs[0] + 1;
    if (!NLogger::GetConsoleOutput())
      NUtils::ProgressBar(processedEntries, totalEntries, start_par,
                          TString::Format("R%4d", (int)threadDataVector.size()).Data());

    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      threadDataVector[i].GetHnSparseBase()->GetBinning()->SetCurrentDefinitionName(name);
    }
    json jobMon;
    jobMon["jobName"] = name;
    // jobMon["user"]    = gSystem->Getenv("USER");
    // jobMon["host"]    = gSystem->HostName();
    jobMon["pid"]    = gSystem->GetPid();
    jobMon["status"] = "I";
    jobMon["ntasks"] = maxs[0] + 1;
    if (fWsClient) {
      NLogInfo("NGnTree::Process: Sending job monitor info to WebSocket server ...");
      fWsClient->Send(jobMon.dump());
    }
    /// Main execution

    Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
    executorMT.ExecuteParallel<Ndmspc::NGnThreadData>(task, threadDataVector);

    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      threadDataVector[i].ExecuteEndFunction();
    }
    // Update hnsbBinningIn with the processed ids
    NLogDebug("NGnTree::Process: [BEGIN] ------------------------------------------------");
    sumIds += binningIn->GetDefinition(name)->GetIds().size();
    binningIn->GetDefinition(name)->GetIds().clear();
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      NLogDebug("NGnTree::Process: -> Thread %zu processed %lld entries", i, threadDataVector[i].GetNProcessed());
      // threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->Print();
      binningIn->GetDefinition(name)->GetIds().insert(
          binningIn->GetDefinition(name)->GetIds().end(),
          threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().begin(),
          threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().end());
      sort(binningIn->GetDefinition(name)->GetIds().begin(), binningIn->GetDefinition(name)->GetIds().end());
    }
    // hnsbBinningIn->GetDefinition(name)->Print();
    // remove entries present in hnsbBinningIn from other definitions
    for (size_t i = 0; i < defNames.size(); i++) {

      std::string other_name = defNames[i];
      auto        otherDef   = binningIn->GetDefinition(other_name);
      if (i <= iDef) {
        continue;
      }
      if (!otherDef) {
        NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", other_name.c_str());
        return false;
      }
      // remove entries that has value less then sumIds
      for (auto it = otherDef->GetIds().begin(); it != otherDef->GetIds().end();) {
        NLogDebug("NGnTree::Process: Checking entry %lld from definition '%s' against sumIds=%d", *it,
                  other_name.c_str(), sumIds);
        if (*it < sumIds) {
          NLogDebug("NGnTree::Process: Removing entry %lld from definition '%s'", *it, other_name.c_str());
          it = otherDef->GetIds().erase(it);
        }
        else {
          ++it;
        }
      }

      binningIn->GetDefinition(other_name)->Print();
    }
    // hnsbBinningIn->GetDefinition(name)->Print();
    iDef++;

    NLogDebug("NGnTree::Process: [END] ------------------------------------------------");
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    NLogInfo("NGnTree::Process: Execution completed and it took %s .",
             NUtils::FormatTime(par_duration.count() / 1000).c_str());

    NLogInfo("NGnTree::Process: Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Result from thread %zu: ", data.GetAssignedIndex());
      data.GetHnSparseBase()->GetStorageTree()->Close(true);
    }

    NLogDebug("NGnTree::Process: Merging %zu results ...", threadDataVector.size());
    TList *                 mergeList  = new TList();
    Ndmspc::NGnThreadData * outputData = new Ndmspc::NGnThreadData();
    outputData->Init(0, func, nullptr, nullptr, this, binningIn);
    outputData->SetCfg(cfg);
    // outputData->Init(0, func, this);

    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Adding thread data %zu to merge list ...", data.GetAssignedIndex());
      // data.GetHnSparseBase()->GetBinning()->GetPoint()->SetCfg(cfg);
      mergeList->Add(&data);
    }

    Long64_t nmerged = outputData->Merge(mergeList);
    if (nmerged <= 0) {
      NLogError("NGnTree::Process: Failed to merge thread data, exiting ...");
      delete mergeList;
      return false;
    }
    NLogInfo("NGnTree::Process: Merged %lld outputs successfully", nmerged);
    // delete all temporary files
    // for (auto & data : threadDataVector) {
    //   std::string filename = data.GetHnSparseBase()->GetStorageTree()->GetFileName();
    //   NLogTrace("NGnTree::Process: Deleting temporary file '%s' ...", filename.c_str());
    //   gSystem->Exec(TString::Format("rm -f %s", filename.c_str()));
    // }
    //

    fTreeStorage = outputData->GetHnSparseBase()->GetStorageTree();
    fOutputs     = outputData->GetHnSparseBase()->GetOutputs();
    fBinning     = outputData->GetHnSparseBase()->GetBinning(); // Update binning to the merged one
    fParameters  = outputData->GetHnSparseBase()->GetParameters();

    if (NLogger::GetConsoleOutput()) {
      NLogInfo("NGnTree::Process: Processing completed successfully. Output was stored in '%s'.",
               fTreeStorage->GetFileName().c_str());
    }
    else {
      Printf("Processing completed successfully. Output was stored in '%s'.", fTreeStorage->GetFileName().c_str());
    }

    jobMon["status"] = "D";
    if (fWsClient) {
      fWsClient->Send(jobMon.dump());
    }
  }

  if (fWsClient) {
    // gSystem->Sleep(1000); // wait for messages to be sent
    fWsClient->Disconnect();
    SafeDelete(fWsClient);
  }

  gSystem->Exec(TString::Format("rm -fr %s", jobDir.c_str()));
  gROOT->SetBatch(batch); // Restore ROOT batch mode
  return true;
}
TList * NGnTree::GetOutput(std::string name)
{
  ///
  /// Get output list for the given binning definition name
  ///

  if (name.empty()) {
    name = fBinning->GetCurrentDefinitionName();
  }
  if (fOutputs.find(name) == fOutputs.end()) {
    fOutputs[name] = new TList();
    fOutputs[name]->SetName(name.c_str());
  }
  return fOutputs[name];
}

NGnTree * NGnTree::Open(const std::string & filename, const std::string & branches, const std::string & treename)
{
  ///
  /// Open NGnTree from file
  ///

  NLogDebug("Opening '%s' with branches='%s' and treename='%s' ...", filename.c_str(), branches.c_str(),
            treename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogError("NGnTree::Open: Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogError("NGnTree::Open: Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  return Open(tree, branches, file);
}

NGnTree * NGnTree::Open(TTree * tree, const std::string & branches, TFile * file)
{
  ///
  /// Open NGnTree from existing tree
  ///

  NBinning * hnstBinning = (NBinning *)tree->GetUserInfo()->At(0);
  if (!hnstBinning) {
    NLogError("NGnTree::Open: Cannot get binning from tree '%s'", tree->GetName());
    return nullptr;
  }
  NStorageTree * hnstStorageTree = (NStorageTree *)tree->GetUserInfo()->At(1);
  if (!hnstStorageTree) {
    NLogError("NGnTree::Open: Cannot get tree storage info from tree '%s'", tree->GetName());
    return nullptr;
  }

  std::map<std::string, TList *> outputs;
  TDirectory *                   dir = nullptr;
  if (file) {
    dir    = (TDirectory *)file->Get("outputs");
    auto l = dir->GetListOfKeys();
    for (auto kv : *l) {
      TObject * obj = dir->Get(kv->GetName());
      if (!obj) continue;
      TList * l = dynamic_cast<TList *>(obj);
      if (!l) continue;
      outputs[l->GetName()] = l;
      NLogDebug("Imported output list for binning '%s' with %d object(s) from file '%s'", l->GetName(), l->GetEntries(),
                file->GetName());
    }
  }
  // TDirectory * dir = (TDirectory *)tree->GetUserInfo()->FindObject("outputs");
  // if (dir) {
  //   dir->Print();
  // }

  NGnTree * ngnt = new NGnTree(hnstBinning, hnstStorageTree);

  if (!hnstStorageTree->SetFileTree(file, tree)) return nullptr;
  // if (!ngnt->InitBinnings({})) return nullptr;
  // ngnt->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    NLogTrace("NGnTree::Open: Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnstStorageTree->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnstStorageTree->GetBranchesMap()) {
      NLogTrace("NGnTree::Open: Enabled branches: %s", kv.first.c_str());
    }
  }
  // Set all branches to be read
  hnstStorageTree->SetBranchAddresses();
  ngnt->SetOutputs(outputs);

  NGnNavigator * nav = new NGnNavigator();
  nav->SetGnTree(ngnt);
  ngnt->SetNavigator(nav);

  return ngnt;
}

void NGnTree::SetNavigator(NGnNavigator * navigator)
{
  ///
  /// Set navigator
  ///

  if (fNavigator) {
    NLogTrace("NGnTree::SetNavigator: Replacing existing navigator ...");
    SafeDelete(fNavigator);
  }

  fNavigator = navigator;
}

bool NGnTree::Close(bool write)
{
  ///
  /// Close the storage tree
  ///

  if (!fTreeStorage) {
    NLogError("NGnTree::Close: Storage tree is not initialized in NGnTree !!!");
    return false;
  }

  return fTreeStorage->Close(write, fOutputs);
}

Int_t NGnTree::GetEntry(Long64_t entry, bool checkBinningDef)
{
  ///
  /// Get entry
  ///
  if (!fTreeStorage) {
    NLogError("NGnTree::GetEntry: Storage tree is not initialized in NGnTree !!!");
    return -1;
  }

  int bytes =
      fTreeStorage->GetEntry(entry, fBinning->GetPoint(0, fBinning->GetCurrentDefinitionName()), checkBinningDef);
  if (fTreeStorage->GetBranch("_params")) fParameters = (NParameters *)fTreeStorage->GetBranch("_params")->GetObject();
  return bytes;
}
Int_t NGnTree::GetEntry(std::vector<std::vector<int>> /*range*/, bool checkBinningDef)
{
  ///
  /// Get entry by range
  ///

  return GetEntry(0, checkBinningDef);
}

void NGnTree::Play(int timeout, std::string binning, std::vector<int> outputPointIds,
                   std::vector<std::vector<int>> ranges, Option_t * option, std::string ws)
{
  ///
  /// Play the tree
  ///
  TString opt = option;
  opt.ToUpper();

  std::string annimationTempDir =
      TString::Format("%s/.ndmspc/animation/%d", gSystem->Getenv("HOME"), gSystem->GetPid()).Data();
  gSystem->Exec(TString::Format("mkdir -p %s", annimationTempDir.c_str()));

  Ndmspc::NWsClient * client = nullptr;

  if (!ws.empty()) {
    client = new Ndmspc::NWsClient();
    if (!client->Connect(ws)) {
      NLogError("Failed to connect to '%s' !!!", ws.c_str());
      return;
    }
    NLogInfo("Connected to %s", ws.c_str());
  }

  if (binning.empty()) {
    binning = fBinning->GetCurrentDefinitionName();
  }

  NBinningDef * binningDef = fBinning->GetDefinition(binning);
  if (!binningDef) {
    NLogError("NGnTree::Play: Binning definition '%s' not found in NGnTree !!!", binning.c_str());
    NLogError("Available binning definitions:");
    for (auto & name : fBinning->GetDefinitionNames()) {
      if (name == fBinning->GetCurrentDefinitionName())
        NLogError(" [*] %s", name.c_str());
      else
        NLogError(" [ ] %s", name.c_str());
    }
    return;
  }

  THnSparse * bdContent = (THnSparse *)binningDef->GetContent()->Clone();

  std::string bdContentName = TString::Format("bdContent_%s", binning.c_str()).Data();
  // Set axis ranges if provided
  if (!ranges.empty()) NUtils::SetAxisRanges(bdContent, ranges, false, true);

  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{bdContent->CreateIter(true /*use axis range*/)};
  std::vector<Long64_t>                           ids;
  // std::vector<Long64_t> ids = binningDef->GetIds();
  while ((linBin = iter->Next()) >= 0) {
    // NLogDebug("Original content bin %lld: %f", linBin, bdContentOrig->GetBinContent(linBin));
    ids.push_back(linBin);
  }
  if (ids.empty()) {
    NLogWarning("NGnTree::Play: No entries found in binning definition '%s' !!!", binning.c_str());
    return;
  }
  // return;

  TCanvas * c1 = nullptr;
  if (!client) {
    c1 = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("c1");
    if (c1 == nullptr) c1 = new TCanvas("c1", "NGnTree::Play", 800, 600);
    c1->Clear();
    c1->cd();
    c1->DivideSquare(outputPointIds.size() > 0 ? outputPointIds.size() + 1 : 1);
    gSystem->ProcessEvents();
  }
  binningDef->Print();
  bdContent->Reset();

  // loop over all ids and print them
  for (auto id : ids) {
    // for (int id = 0; id < GetEntries(); id++) {
    GetEntry(id);
    fBinning->GetPoint()->Print();
    TList * l = (TList *)fTreeStorage->GetBranch("outputPoint")->GetObject();
    if (!l || l->IsEmpty()) {
      NLogWarning("NGnTree::Play: No 'outputPoint' for entry %lld !!!", id);
      continue;
    }
    else {
      // NLogInfo("Output for entry %lld:", id);
      // l->Print(opt.Data());

      if (outputPointIds.empty()) {
        outputPointIds.resize(l->GetEntries());
        for (int i = 0; i < l->GetEntries(); i++) {
          outputPointIds[i] = i;
        }
      }
      int n = outputPointIds.size();

      if (client) {
        std::string msg = TBufferJSON::ConvertToJSON(l).Data();
        if (!client->Send(msg)) {
          NLogError("Failed to send message `%s`", msg.c_str());
        }
        else {
          NLogTrace("Sent: %s", msg.c_str());
        }
      }
      else {

        Double_t v = 1.0;
        for (int i = 0; i < n; i++) {
          // NLogDebug("Drawing output object id %d (list index %d) on pad %d", outputPointIds[i], i, i + 1);

          c1->cd(i + 2);
          TObject * obj = l->At(outputPointIds[i]);
          if (obj) {
            // obj->Print();
            obj->Draw();
          }
          if (obj->InheritsFrom(TH1::Class()) && i == 0) {
            TH1 * h = (TH1 *)obj;
            v       = h->GetMean();
            NLogDebug("Mean value from histogram [%s]: %f", h->GetName(), v);
          }
        }
        bdContent->SetBinContent(fBinning->GetPoint()->GetStorageCoords(), 1);
        c1->cd(1);
        TH1 * bdProj = (TH1 *)gROOT->FindObjectAny("bdProj");
        if (bdProj) {
          delete bdProj;
          bdProj = nullptr;
        }
        if (bdContent->GetNdimensions() == 1) {
          bdProj = bdContent->Projection(0, "O");
        }
        else if (bdContent->GetNdimensions() == 2) {
          bdProj = bdContent->Projection(0, 1, "O");
        }
        else if (bdContent->GetNdimensions() == 3) {
          bdProj = bdContent->Projection(0, 1, 2, "O");
        }
        else {
          NLogError("NGnTree::Play: Cannot project THnSparse with %d dimensions", bdContent->GetNdimensions());
        }
        if (bdProj) {
          bdProj->SetName("bdProj");
          bdProj->SetTitle(TString::Format("Binning '%s' content projection", binning.c_str()).Data());
          bdProj->SetMinimum(0);
          // bdProj->SetDirectory(nullptr);
          bdProj->Draw("colz");
          // c1->ModifiedUpdate();
        }
      }
    }
    if (c1) {
      c1->ModifiedUpdate();
      c1->SaveAs(TString::Format("%s/ndmspc_play_%06lld.png", annimationTempDir.c_str(), bdContent->GetNbins()).Data());
    }
    gSystem->ProcessEvents();
    if (timeout > 0) gSystem->Sleep(timeout);
    NLogInfo("%d", id);
  }

  NLogInfo("Creating animation gif from %s/ndmspc_play_*.png ...", annimationTempDir.c_str());
  gSystem->Exec(
      TString::Format("magick -delay 20 -loop 0 %s/ndmspc_play_*.png ndmspc_play.gif", annimationTempDir.c_str()));
  gSystem->Exec(TString::Format("rm -fr %s", annimationTempDir.c_str()));
  NLogInfo("Animation saved to %s/ndmspc_play.gif", annimationTempDir.c_str());

  if (client) client->Disconnect();
  delete bdContent;
}

TList * NGnTree::Projection(const json & cfg, std::string binningName)
{
  ///
  /// Project THnSparse objects from the output lists based on configuration
  ///

  // SetInput(); // Set input to selfp
  SetInput(Ndmspc::NGnTree::Open(fTreeStorage->GetFileName()));
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * /*outputPoint*/,
                                             int /*threadId*/) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json cfg = point->GetCfg();

    Printf("Processing THnSparse projection with configuration: %s", cfg.dump().c_str());

    Ndmspc::NGnTree * ngntIn = point->GetInput();
    // ngntIn->Print();
    // ngntIn->GetEntry(0);
    ngntIn->GetEntry(point->GetEntryNumber());

    // loop over all cfg["objects"]
    for (auto & [objName, objCfg] : cfg["objects"].items()) {
      NLogInfo("Processing object '%s' ...", objName.c_str());

      THnSparse * hns = (THnSparse *)(ngntIn->GetStorageTree()->GetBranchObject(objName));
      if (hns == nullptr) {
        NLogError("NGnTree::Projection: THnSparse 'hns' not found in storage tree !!!");
        return;
      }
      // hns->Print("all");
      // loop over cfg["objects"][objName] array of projection dimension names
      for (size_t i = 0; i < objCfg.size(); i++) {

        NLogInfo("Processing projection %zu for object '%s' ...", i, objName.c_str());
        std::vector<int>         dims;
        std::vector<std::string> dimNames = cfg["objects"][objName][i].get<std::vector<std::string>>();
        for (const auto & dimName : dimNames) {
          NLogDebug("Looking for dimension name '%s' in THnSparse ...", dimName.c_str());
          int dim = -1;
          for (int i = 0; i < hns->GetNdimensions(); i++) {
            if (dimName == hns->GetAxis(i)->GetName()) {
              dim = i;
              break;
            }
          }
          if (dim >= 0)
            dims.push_back(dim);
          else {
            NLogError("NGnTree::Projection: Dimension name '%s' not found in THnSparse !!!", dimName.c_str());
          }
        }
        // Print dims
        NLogInfo("Projecting THnSparse on dimensions: %s", NUtils::GetCoordsString(dims, -1).c_str());
        TH1 * hPrev = (TH1 *)output->At(i);
        TH1 * hProj = NUtils::ProjectTHnSparse(hns, dims, "O");
        hProj->SetName(TString::Format("%s_proj_%s", objName.c_str(), NUtils::Join(dims, '_').c_str()).Data());
        if (hPrev) {
          hPrev->Add(hProj);
        }
        else {
          output->Add(hProj);
        }
      }
    }
    output->Print();
  };

  // NBinningDef *                 binningDef = fInput->GetBinning()->GetDefinition(binningName);
  NBinningDef * binningDef = GetBinning()->GetDefinition(binningName);
  THnSparse *   hnsIn      = binningDef->GetContent();
  // std::vector<std::vector<int>> ranges{{0, 2, 2}, {2, 1, 1}};
  std::vector<std::vector<int>> ranges = cfg["ranges"].get<std::vector<std::vector<int>>>();
  NUtils::SetAxisRanges(hnsIn, ranges); // Set the ranges for the axes
  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsIn->CreateIter(true /*use axis range*/)};
  std::vector<Long64_t>                           ids;
  // std::vector<Long64_t> ids = binningDef->GetIds();
  while ((linBin = iter->Next()) >= 0) {
    ids.push_back(linBin);
  }
  if (ids.empty()) {
    NLogWarning("NGnTree::Projection: No entries found in binning definition '%s' !!!", binningDef->GetName());
    binningDef->RefreshIdsFromContent();
    return nullptr;
  }

  binningDef->GetIds() = ids;

  // NUtils::SetAxisRanges(, std::vector<std::vector<int>> ranges)
  Process(processFunc, cfg);

  // Refresh binning definition ids from content after processing
  binningDef->RefreshIdsFromContent();

  // GetInput()->Close(false);
  return GetOutput(fBinning->GetCurrentDefinitionName());

  // Close(false);
}

NGnNavigator * NGnTree::Reshape(std::string binningName, std::vector<std::vector<int>> levels, int level,
                                std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Reshape the navigator
  ///

  NGnNavigator navigator;
  navigator.SetGnTree(this);

  return navigator.Reshape(binningName, levels, level, ranges, rangesBase);
}

NGnNavigator * NGnTree::GetResourceStatisticsNavigator(std::string binningName, std::vector<std::vector<int>> levels,
                                                       int level, std::map<int, std::vector<int>> ranges,
                                                       std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Get resource statistics navigator
  ///

  if (binningName.empty()) {
    binningName = fBinning->GetCurrentDefinitionName();
  }

  THnSparse * hns = (THnSparse *)fOutputs[binningName]->FindObject("resource_monitor");
  if (!hns) {
    NLogError("NGnTree::Draw: Resource monitor THnSparse not found in outputs !!!");
    return nullptr;
  }
  hns->Print("all");
  // return nullptr;

  auto ngnt = new NGnTree(hns, "stat", "/tmp/hnst_imported_for_drawing.root");
  if (ngnt->IsZombie()) {
    NLogError("NGnTree::GetResourceStatisticsNavigator: Failed to import resource monitor THnSparse !!!");
    return nullptr;
  }
  ngnt->Print();
  ngnt->Close();

  // return nullptr;
  auto ngnt2 = NGnTree::Open("/tmp/hnst_imported_for_drawing.root");
  auto nav   = ngnt2->Reshape("default", levels, level, ranges, rangesBase);
  // // nav->Export("/tmp/hnst_imported_for_drawing.json", {}, "ws://localhost:8080/ws/root.websocket");
  // // nav->Draw();
  return nav;
}

bool NGnTree::InitParameters(const std::vector<std::string> & paramNames)
{
  ///
  /// Initialize parameters
  ///

  if (fParameters) {
    NLogTrace("NGnTree::InitParameters: Replacing existing parameters ...");
    delete fParameters;
  }

  if (paramNames.empty()) {
    NLogTrace("NGnTree::InitParameters: No parameter names provided, skipping ...");
    return false;
  }

  fParameters = new NParameters("results", "Results", paramNames);

  return true;
}

NGnTree * NGnTree::Import(const std::string & findPath, const std::string & fileName,
                          const std::vector<std::string> & headers, const std::string & outputFile)
{
  ///
  /// Import NGnTree from mutiple files in the given path
  ///

  std::vector<std::string> paths = NUtils::Find(findPath, fileName);
  NLogInfo("NGnTree::Import: Found %zu files to import ...", paths.size());

  TObjArray * ngntArray = NUtils::AxesFromDirectory(paths, findPath, fileName, headers);
  int         nDirAxes  = ngntArray->GetEntries();

  NGnTree * ngntFirst = NGnTree::Open(paths[0]);
  // Add all axes from ngntFirst to ngntArray
  for (const auto & axis : ngntFirst->GetBinning()->GetAxes()) {
    ngntArray->Add(axis->Clone());
  }
  ngntFirst->Close(false);

  std::map<std::string, std::vector<std::vector<int>>> b;

  for (int i = 0; i < ngntArray->GetEntries(); i++) {
    TAxis * axis = (TAxis *)ngntArray->At(i);
    b[axis->GetName()].push_back({1});
  }

  NGnTree * ngnt = new NGnTree(ngntArray, outputFile);
  ngnt->SetIsPureCopy(true);

  // return nullptr;
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  json cfg;
  cfg["basedir"]  = findPath;
  cfg["filename"] = fileName;
  cfg["nDirAxes"] = nDirAxes;
  cfg["headers"]  = headers;
  // cfg["ndmspc"]["shared"]["currentFileName"]  = "";
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // point->Print();

    json        cfg      = point->GetCfg();
    std::string filename = cfg["basedir"].get<std::string>();
    filename += "/";
    for (auto & header : cfg["headers"]) {
      filename += point->GetBinLabel(header.get<std::string>());
      filename += "/";
    }
    // filename += "/";
    // filename += point->GetBinLabel("c");
    // filename += point->GetBinLabel("year");
    // filename += "/";
    filename += cfg["filename"].get<std::string>();
    NGnTree * ngnt = (NGnTree *)point->GetTempObject("file");
    if (!ngnt || filename.compare(ngnt->GetStorageTree()->GetFileName()) != 0) {
      NLogInfo("NGnTree::Import: Opening file '%s' ...", filename.c_str());
      if (ngnt) {
        NLogDebug("NGnTree::Import: Closing previously opened file '%s' ...",
                  ngnt->GetStorageTree()->GetFileName().c_str());
        // ngnt->Close(false);
        // delete ngnt;
        point->SetTempObject("file", nullptr);
      }
      ngnt = NGnTree::Open(filename.c_str());
      if (!ngnt || ngnt->IsZombie()) {
        NLogError("NGnTree::Import: Cannot open file '%s'", filename.c_str());
        return;
      }
      point->SetTempObject("file", ngnt);
    }

    int         nDirAxes  = cfg["nDirAxes"].get<int>();
    Int_t *     coords    = point->GetCoords();
    std::string coordsStr = NUtils::GetCoordsString(NUtils::ArrayToVector(coords, point->GetNDimensionsContent()));
    NLogInfo("NGnTree::Import: Processing point with coords %s ...", coordsStr.c_str());

    Long64_t entryNumber =
        ngnt->GetBinning()->GetContent()->GetBin(&coords[3 * nDirAxes], kFALSE); // skip first 3 dir axes
    NLogInfo("NGnTree::Import: Corresponding entry number in file '%s' is %lld", filename.c_str(), entryNumber);

    ngnt->GetEntry(entryNumber);

    // // add outputPoint content to outputPoint list
    // TList * inputOutputPoint = (TList *)ngnt->GetStorageTree()->GetBranch("outputPoint")->GetObject();
    // for (int i = 0; i < inputOutputPoint->GetEntries(); i++) {
    //   outputPoint->Add(inputOutputPoint->At(i));
    // }

    // set all branches from ngnt to branch addresses in current object
    for (const auto & kv : ngnt->GetStorageTree()->GetBranchesMap()) {
      // check if branch exists in current storage tree
      if (point->GetStorageTree()->GetBranch(kv.first) == nullptr) {
        NLogTrace("NGnTree::Import: Adding branch '%s' to storage tree ...", kv.first.c_str());
        point->GetStorageTree()->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
      }
      NLogTrace("NGnTree::Import: Setting branch address for branch '%s' ...", kv.first.c_str());
      point->GetTreeStorage()->GetBranch(kv.first)->SetAddress(kv.second.GetObject());
    }
    outputPoint->Add(new TNamed("source_file", filename));

    // ngnt->Print();

    // NLogInfo("NGnTree::Import: nDirAxes=%d ...", cfg["nDirAxes"].get<int>());

    // json & tempCfg = point->GetTempCfg();
    // if (tempCfg["test"].is_null()) {
    //   NLogInfo("Setting temp cfg test value to 42");
    //   tempCfg["test"] = 42;
    // }
    // NLogInfo("Temp cfg test value: %d", tempCfg["test"].get<int>());

    // f->ls();
  };
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
  };

  Ndmspc::NGnEndFuncPtr endFunc = [](Ndmspc::NBinningPoint * point, int /*threadId*/) {
    NGnTree * ngnt = (NGnTree *)point->GetTempObject("file");
    if (ngnt) {
      NLogDebug("NGnTree::Import: Closing last file '%s' ...", ngnt->GetStorageTree()->GetFileName().c_str());
      // ngnt->Close(false);
      // delete ngnt;
      point->SetTempObject("file", nullptr);
    }
  };

  ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);
  return ngnt;
}

} // namespace Ndmspc
