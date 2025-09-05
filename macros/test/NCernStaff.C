#include <string>
#include <TSystem.h>
#include <TInterpreter.h>
#include <TROOT.h>
#include <TFile.h>
#include <THnSparse.h>
#include <TCanvas.h>
#include "NHnSparseBase.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NConfig.h"
void NCernStaff(int nThreads = 1, std::string filename = "cernstaff.root")
{
  std::string fn = filename;
  if (gSystem->AccessPathName(fn.c_str())) {
    // If the file is not found in the current directory, try to find it in the macro directory
    fn = gSystem->DirName(gInterpreter->GetCurrentMacroName());
    fn += "/";
    fn += filename;
  }

  // Open the file
  TFile * file = TFile::Open(fn.c_str());
  if (!file || file->IsZombie()) {
    Ndmspc::NLogger::Error("NCernStaff::Failed to open file %s", filename.c_str());
    return;
  }

  // Get the THnSparse object
  THnSparse * hsparse = dynamic_cast<THnSparse *>(file->Get("hsparse"));
  if (!hsparse) {
    Ndmspc::NLogger::Error("NCernStaff::THnSparse object 'hsparse' not found in %s", filename.c_str());
    return;
  }

  // Create an NHnSparseObject from the THnSparse
  Ndmspc::NHnSparseBase * hnsb = new Ndmspc::NHnSparseBase(hsparse->GetListOfAxes());

  // Define the binning for the axes
  // std::map<std::string, std::vector<std::vector<int>>> b;
  // b["Nation"]   = {{1}};
  // b["Division"] = {{1}};
  // hnsb->GetBinning()->AddBinningDefinition("default", b);

  std::map<std::string, std::vector<std::vector<int>>> b2;
  b2["Flag"]     = {{1}};
  b2["Division"] = {{1}};
  hnsb->GetBinning()->AddBinningDefinition("b2", b2);

  // Print the sparse object
  // hnsb->Print();

  return;

  if (nThreads != 1) {
    ROOT::EnableImplicitMT(nThreads); // Enable multithreading
  }
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*list*/, TList * /*list2*/,
                                                   int                     threadId) {
    // Ndmspc::NLogger::Info("Thread ID: %d", threadId);
    if (point) {
      point->RecalculateStorageCoords();

      const json & cfg = point->GetCfg();
      std::string  opt = cfg.contains("opt") ? cfg["opt"].get<std::string>() : "";
      point->Print(opt.c_str());
      std::string filename =
          cfg.contains("input") && cfg["input"].contains("filename") ? cfg["input"]["filename"].get<std::string>() : "";
      if (!filename.empty()) {
        TFile * file = TFile::Open(filename.c_str());
        if (!file || file->IsZombie()) {
          Ndmspc::NLogger::Error("Cannot open file '%s'", filename.c_str());
          return;
        }
        std::string objectName = cfg.contains("input") && cfg["input"].contains("object")
                                     ? cfg["input"]["object"].get<std::string>()
                                     : "hsparse";
        THnSparse * hns        = dynamic_cast<THnSparse *>(file->Get(objectName.c_str()));
        if (hns == nullptr) {
          Ndmspc::NLogger::Error("Cannot open THnSparse from file '%s'", filename.c_str());
          return;
        }

        // Set Range
        Ndmspc::NUtils::SetAxisRanges(hns, point->GetBaseAxisRanges(), true);
        TH1 * hProj = hns->Projection(0);
        if (hProj) {
          hProj->SetTitle(point->GetTitle().c_str());

          if (hProj->GetEntries() > 0) {
            Ndmspc::NLogger::Info("%s", hProj->GetTitle());
            hProj->Print();
          }
        }
        else {
          Ndmspc::NLogger::Error("Cannot project THnSparse from file '%s'", filename.c_str());
        }

        file->Close();
      }
    }
    gSystem->Sleep(100); // Simulate some processing time
  };

  json cfg                 = json::object();
  cfg["opt"]               = "A";
  cfg["input"]["filename"] = fn;
  cfg["input"]["object"]   = "hsparse";

  // hnsb->Process(processFunc, {1}, {1});
  // hnsb->Process(processFunc, "default", cfg);
  hnsb->Process(processFunc, "b2", cfg);

  // Clean up
  delete hnsb;
  file->Close();
}
