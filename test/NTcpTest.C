#include <TAxis.h>
#include <TSystem.h>
#include <TObjArray.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NTcpTest(std::string outFile = "NTcpTest.root", std::string bins = "10,10,5,5", int timeout = 200, bool oddsOnly = false)
{

  json cfg;
  cfg["timeout"] = timeout;
  cfg["oddsOnly"] = oddsOnly;

  // Parse the bins string into a vector of integers
  std::vector<int>  binVec;
  std::stringstream ss(bins);
  std::string       item;
  while (std::getline(ss, item, ',')) {
    binVec.push_back(std::stoi(item));
  }
  // Create axes
  TObjArray * axes = new TObjArray();
  for (size_t i = 1; i <= binVec.size(); ++i) {
    TAxis * a = new TAxis(binVec[i - 1], 0, binVec[i - 1]);
    a->SetNameTitle(Form("axis%zu", i), Form("Axis %zu bins[%d]", i, binVec[i - 1]));
    axes->Add(a);
  }

  // Create an NGbnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  for (size_t i = 1; i <= 4; ++i) b[Form("axis%zu", i)] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  ngnt->InitParameters({"mean", "rms"});

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // Retrieve configuration
    json cfg = point->GetCfg();

    // Retrieve number of entries
    int timeout = cfg["timeout"].get<int>();
    bool oddsOnly = cfg["oddsOnly"].get<bool>();

    if (oddsOnly && (point->GetEntryNumber() % 2 == 0)) {
      NLogInfo("Skipping even entry %d", point->GetEntryNumber());
      return;
    }

    // print the title of the binning point
    NLogInfo("title : %s timeout : %d oddsOnly : %d", point->GetString().c_str(), timeout, oddsOnly);

    gSystem->Sleep(timeout); // Sleep for the specified timeout to simulate some processing time
    // print the binning point information
    // point->Print();
    TH1 * h = new TH1D("h", Form("Histogram for %s", point->GetString().c_str()), 20, -5, 5);
    h->FillRandom("gaus", point->GetEntryNumber() + 1);


    Ndmspc::NParameters * params = point->GetParameters();
    if (params) {
      params->SetParameter("mean", h->GetMean());
      params->SetParameter("rms", h->GetRMS());
    }

    outputPoint->Add(h);
  };

  // execute the processing function
  ngnt->Process(processFunc, cfg);

  // Clean up
  delete ngnt;
}
