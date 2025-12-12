#include <TAxis.h>
#include <TObjArray.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NExecutor1DCategory(std::string outFile = "NExecutor1DCategory.root")
{

  // Create axes
  TObjArray * axes = new TObjArray();

  // set of category labels
  std::vector<std::string> categories = {"A", "B", "C", "D", "E"};
  // define a category axis from the labels
  TAxis * a1c = Ndmspc::NUtils::CreateAxisFromLabels("axis1c", "Axis 1 (category)", categories);
  // add axis to the list of axes
  axes->Add(a1c);

  // Create an NGbnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  // Set binning for axis1 (rebin to 1 bin)
  b["axis1c"] = {{1}};
  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Define the processing function
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // print the title of the binning point
    NLogInfo("title : %s", point->GetString().c_str());
    // print the binning point information
    // point->Print();
  };

  // execute the processing function
  ngnt->Process(processFunc);

  // close the NGnTree object
  ngnt->Close(true);

  // Clean up
  delete ngnt;
}
