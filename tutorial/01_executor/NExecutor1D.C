#include <TAxis.h>
#include <TObjArray.h>
#include <NGnTree.h>
#include <NLogger.h>

void NExecutor1D(std::string outFile = "NExecutor1D.root")
{

  // create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from 0 to 10 with 10 bins
  TAxis * a1 = new TAxis(10, 0, 10);
  // set name and title
  a1->SetNameTitle("axis1", "Axis 1");
  // add axis to the list of axes
  axes->Add(a1);

  // Create an NGbnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  // Set binning for axis1 (rebin to 1 bin)
  b["axis1"] = {{1}};
  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Define the processing function
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // print the binning point information
    // point->Print();

    // print the title of the binning point
    NLogInfo("title : %s", point->GetString().c_str());
    NLogInfo("  min=%.2f max=%.2f label=%s", point->GetBinMin("axis1"), point->GetBinMax("axis1"),
             point->GetBinLabel("axis1").c_str());
  };

  // execute the processing function
  ngnt->Process(processFunc);

  // close the NGnTree object
  ngnt->Close(true);

  // Clean up
  delete ngnt;
}
