#include <TAxis.h>
#include <TSystem.h>
#include <TObjArray.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NTcpTest(std::string outFile = "NTcpTest.root", std::string bins = "10,10,5,5", int timeout = 200)
{

  json cfg;
  cfg["timeout"] = timeout;

  // Parse the bins string into a vector of integers
  std::vector<int>  binVec;
  std::stringstream ss(bins);
  std::string       item;
  while (std::getline(ss, item, ',')) {
    binVec.push_back(std::stoi(item));
  }
  // Create axes
  TObjArray * axes = new TObjArray();
  for (int i = 1; i <= binVec.size(); ++i) {
    TAxis * a = new TAxis(binVec[i - 1], 0, binVec[i - 1]);
    a->SetNameTitle(Form("axis%d", i), Form("Axis %d bins[%d]", i, binVec[i - 1]));
    axes->Add(a);
  }

  // Create an NGbnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  for (int i = 1; i <= 4; ++i) b[Form("axis%d", i)] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // Retrieve configuration
    json cfg = point->GetCfg();

    // Retrieve number of entries
    int timeout = cfg["timeout"].get<int>();

    // print the title of the binning point
    NLogInfo("title : %s timeout : %d", point->GetString().c_str(), timeout);

    gSystem->Sleep(timeout); // Sleep for the specified timeout to simulate some processing time
    // print the binning point information
    // point->Print();
    TH1 * h = new TH1D("h", Form("Histogram for %s", point->GetString().c_str()), 20, -5, 5);
    h->FillRandom("gaus", point->GetEntryNumber() + 1);
    outputPoint->Add(h);
  };

  // execute the processing function
  ngnt->Process(processFunc, cfg);

  // Clean up
  delete ngnt;
}
