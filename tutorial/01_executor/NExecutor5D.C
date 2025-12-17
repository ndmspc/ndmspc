#include <TAxis.h>
#include <TObjArray.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NExecutor5D(std::string outFile = "NExecutor5D.root")
{

  // Create axes
  TObjArray * axes = new TObjArray();

  // Define week number axis for 52 weeks
  TAxis * aWeek = new TAxis(52, 1, 53);
  aWeek->SetNameTitle("week", "Week Number");
  // set week number labels
  for (int i = 1; i <= 52; ++i) {
    aWeek->SetBinLabel(i, Form("%02d", i));
  }
  axes->Add(aWeek);

  // add day of week axis
  TAxis * aDay = Ndmspc::NUtils::CreateAxisFromLabels("day", "Day", {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"});
  axes->Add(aDay);

  // add hour axis
  TAxis * aHour = Ndmspc::NUtils::CreateAxisFromLabels(
      "hour", "Hour", {"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11",
                       "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23"});
  axes->Add(aHour);

  // add minute axis
  TAxis * aMinute = Ndmspc::NUtils::CreateAxisFromLabels("minute", "Minute", {"00", "15", "30", "45"});
  axes->Add(aMinute);

  // add second axis
  TAxis * aSecond = Ndmspc::NUtils::CreateAxisFromLabels("second", "Second", {"00", "30"});
  axes->Add(aSecond);

  // Create an NGbnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["week"]   = {{1}};
  b["day"]    = {{1}};
  b["hour"]   = {{1}};
  b["minute"] = {{1}};
  b["second"] = {{1}};
  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Define the processing function
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/,
                                                   TList * /*outputPoint*/, int /*threadId*/) {
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
