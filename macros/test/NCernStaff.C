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
void NCernStaff(int nThreads = 1, std::string outFile = "/tmp/hnst_cernstaff.root",
                std::string filename = "cernstaff.root")
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
  Ndmspc::NHnSparseBase * hnsb = new Ndmspc::NHnSparseBase(hsparse->GetListOfAxes(), outFile);
  // return;

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["Nation"]   = {{1}};
  b["Division"] = {{1}};
  // b["Flag"]     = {{1}};
  // b["Grade"]    = {{1}};
  // b["Step"]  = {{1}};

  hnsb->GetBinning()->AddBinningDefinition("default", b);

  std::map<std::string, std::vector<std::vector<int>>> b2;
  b2["Flag"]     = {{1}};
  b2["Division"] = {{1}};
  hnsb->GetBinning()->AddBinningDefinition("b2", b2);

  // Print the sparse object
  hnsb->Print();

  // return;

  json cfg = json::object();
  // cfg["opt"]               = "A";
  cfg["input"]["filename"] = fn;
  cfg["input"]["object"]   = "hsparse";
  cfg["sparse"]            = true;
  cfg["sparse"]            = false;

  // return;

  if (nThreads != 1) {
    ROOT::EnableImplicitMT(nThreads); // Enable multithreading
  }
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // Ndmspc::NLogger::Info("Thread ID: %d", threadId);

    if (!point) {
      Ndmspc::NLogger::Error("Point is nullptr !!!");
      return;
    }

    // point->Print("");
    // Ndmspc::NLogger::Info("Point title: %s", point->GetTitle().c_str());

    TH1 * h = (TH1 *)output->FindObject("test");
    if (!h) {
      h = new TH1F("test", "test", 10, 0, 10);
      output->Add(h);
    }

    h->Fill(2);

    const json & cfg = point->GetCfg();
    // std::string  opt = cfg.contains("opt") ? cfg["opt"].get<std::string>() : "";
    // point->Print(opt.c_str());
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

      if (cfg.contains("sparse") && cfg["sparse"].get<bool>()) {
        Int_t       axes[] = {1};
        THnSparse * hsProj = hns->Projection(1, axes, "OE");
        if (hsProj) {
          hsProj->SetNameTitle(hns->GetName(), hns->GetTitle());
          Ndmspc::NStorageTree * ts = point->GetTreeStorage();
          Ndmspc::NTreeBranch *  b  = ts->GetBranch(hns->GetName());
          if (!b) {
            ts->AddBranch(hns->GetName(), nullptr, "THnSparseD");
            b = ts->GetBranch(hns->GetName());
          }
          b->SetAddress(hsProj);
        }
        else {
          Ndmspc::NLogger::Error("Cannot project THnSparse from file '%s'", filename.c_str());
        }
      }
      else {
        TH1 * hProj = hns->Projection(0);
        if (hProj) {
          hProj->SetTitle(point->GetTitle().c_str());

          if (hProj->GetEntries() >= 0) {
            // Ndmspc::NLogger::Info("[%d] %s", threadId, hProj->GetTitle());
            outputPoint->Add(hProj);
            // outputPoint->Print();
          }
        }
        else {
          Ndmspc::NLogger::Error("Cannot project THnSparse from file '%s'", filename.c_str());
        }
      }
      file->Close();
    }
    // gSystem->Sleep(100); // Simulate some processing time
  };

  // hnsb->Process(processFunc, {1}, {1});
  bool rc = false;
  rc      = hnsb->Process(processFunc, cfg);
  // rc = hnsb->Process(processFunc, cfg, "default");
  // rc = hnsb->Process(processFunc, cfg, "b2");
  hnsb->GetBinning()->SetCurrentDefinitionName("default");
  // hnsb->Print();

  if (rc) {
    Ndmspc::NLogger::Info("Processing completed successfully.");
    hnsb->Close(true);
  }
  else {
    Ndmspc::NLogger::Error("Processing failed.");
    hnsb->Close(false);
  }

  // return;
  // TCanvas * c1 = new TCanvas("c1", "c1", 800, 600);
  // c1->Divide(2, 1);
  // TH1 * htest = (TH1 *)hnsb->GetOutput("default")->FindObject("test");
  // if (htest) {
  //   // htest->Print();
  //   c1->cd(1);
  //   htest->DrawCopy();
  // }
  // TH1 * htestb2 = (TH1 *)hnsb->GetOutput("b2")->FindObject("test");
  // if (htestb2) {
  //   // htestb2->Print();
  //   c1->cd(2);
  //   htestb2->DrawCopy();
  // }

  // Clean up
  delete hnsb;
  file->Close();
}
