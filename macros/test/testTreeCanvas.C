#include <TCanvas.h>
#include <TH1F.h>
#include <TList.h>
#include <TRandom.h>
#include <TSystem.h>

void printRSS(const char * label = "")
{
  ProcInfo_t info;
  gSystem->GetProcInfo(&info);
  Printf("[RSS] %s: %ld kB", label, info.fMemResident);
}

// Function to create a TList of canvases, each with histograms filled with random Gaussian distributions
TList * createCanvasesWithHistos(int nCanvases, int nHistosPerCanvas, int nEntriesPerHisto)
{
  TList * canvasList = new TList();
  for (int i = 0; i < nCanvases; ++i) {
    Printf("Creating canvas %d/%d", i + 1, nCanvases);
    TString   cname = TString::Format("c%d", i + 1);
    TCanvas * c     = new TCanvas(cname, cname, 800, 600);
    c->DivideSquare(nHistosPerCanvas);
    for (int j = 0; j < nHistosPerCanvas; ++j) {
      c->cd(j + 1);
      TString hname = TString::Format("h%d_%d", i + 1, j + 1);
      TH1F *  h     = new TH1F(hname, hname, 100, -5, 5);
      for (int k = 0; k < nEntriesPerHisto; ++k) {
        h->Fill(gRandom->Gaus(0, 1));
      }
      h->Fit("gaus", "Q"); // Fit the histogram with a Gaussian function, suppressing output
      h->Draw();
    }
    c->ModifiedUpdate();
    gSystem->ProcessEvents();
    canvasList->Add(c);
  }

  // Optionally, return or use canvasList as needed
  return canvasList;
}

// Function to write TList to TTree and file
void writeTreeCanvas(int nEntries = 10, int nCanvases = 1, int nHistosPerCanvas = 1, int nEntriesPerHisto = 1000)
{
  TH1::AddDirectory(kFALSE); // Avoid issues with histograms being associated with the current directory

  //   TList * l = createCanvasesWithHistos(nCanvases, nHistosPerCanvas, nEntriesPerHisto);
  //   Printf("Created %d canvases with %d histograms each. Final list contains %d objects.", nCanvases,
  //   nHistosPerCanvas,
  //  l->GetSize());

  TFile * file = TFile::Open("treeCanvas.root", "RECREATE");
  // Create TTree
  TTree * tree = new TTree("tree", "Tree with TList branch");

  // Add TList as a branch
  TList * listPtr = nullptr;
  tree->Branch("canvasList", "TList", &listPtr);

  TList * l = createCanvasesWithHistos(nCanvases, nHistosPerCanvas, nEntriesPerHisto);

  printRSS("before fill loop");
  for (int i = 0; i < nEntries; ++i) {
    Printf("Filling tree entry %d/%d", i + 1, nEntries);
    printRSS(TString::Format("entry %d", i + 1));
    listPtr = (TList *)l->Clone(); // Clone the list to ensure we have a separate instance for each entry
    // listPtr = createCanvasesWithHistos(nCanvases, nHistosPerCanvas, nEntriesPerHisto);
    tree->GetBranch("canvasList")->SetAddress(&listPtr);
    tree->Fill();

    if (listPtr) {
      listPtr->Delete(); // Delete all canvases inside the list
      delete listPtr;
      listPtr = nullptr;
    }

    // if (listPtr) {
    //   for (int j = 0; j < listPtr->GetSize(); ++j) {
    //     delete listPtr->At(j);
    //   }
    //   listPtr->Clear();
    // }

    // delete listPtr; // Clean up the cloned list after filling the tree
  }

  // Optionally, write tree to a file

  tree->Write();
  file->Close();
  //   delete tree;
  delete file;
  printRSS("after write");
  Printf("Tree written to treeCanvas.root");
}

void readTreeCanvas(int nEntries = 0)
{
  TFile * file = TFile::Open("treeCanvas.root", "READ");
  TTree * tree = (TTree *)file->Get("tree");
  if (!tree) {
    Printf("Tree not found in file.");
    file->Close();
    return;
  }

  TList * listPtr = nullptr;
  tree->SetBranchAddress("canvasList", &listPtr);
  if (!nEntries) {
    nEntries = tree->GetEntries();
  }

  printRSS("before read loop");
  for (int i = 0; i < nEntries; ++i) {
    // Printf("Reading tree entry %d/%lld", i + 1, tree->GetEntries());.
    tree->GetEntry(i);
    printRSS(TString::Format("entry %d", i + 1));
    if (listPtr) {
      //   Printf("Read TList with %d objects from tree.", listPtr->GetSize());
      // Example: iterate over canvases
      for (int j = 0; j < listPtr->GetSize(); ++j) {
        TObject * obj = listPtr->At(j);
        if (obj->InheritsFrom(TCanvas::Class())) {
          TCanvas * c = (TCanvas *)obj;
          Printf("Canvas %s", c->GetName());
          // Optionally, iterate over histograms in canvas
          // ...
        }
      }
    }
    else {
      Printf("TList pointer is null.");
    }

    if (listPtr) {
      listPtr->Delete(); // Delete all canvases inside the list
      delete listPtr;
      listPtr = nullptr;
    }
  }

  file->Close();
}

void testTreeCanvas(TString o = "w", int nEntries = 10, int nCanvases = 1, int nHistosPerCanvas = 1,
                    int nEntriesPerHisto = 1000)
{
  gROOT->SetBatch(kTRUE); // Run in batch mode to avoid opening canvases
  if (!o.CompareTo("w")) {
    writeTreeCanvas(nEntries, nCanvases, nHistosPerCanvas, nEntriesPerHisto);
  }
  else if (!o.CompareTo("r")) {
    readTreeCanvas(nEntries); // Read back the tree to verify contents
  }
  else {
    Printf("Invalid option '%s'. Use 'w' to write or 'r' to read.", o.Data());
  }
}
