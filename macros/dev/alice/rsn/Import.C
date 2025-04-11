#include <string>
#include "RtypesCore.h"
#include <TFile.h>
#include <TGrid.h>

// Import("alien:///alice/cern.ch/user/a/alihyperloop/outputs/0036/360353/83976/AnalysisResults.root","PWGLF-376","LHC22o_pass7_minBias_medium",false)
// Import("alien:///alice/cern.ch/user/a/alihyperloop/outputs/0035/354440/81633/AnalysisResults.root","PWGLF-376","LHC24f3c",true)
std::vector<std::string> Tokenize(std::string_view input, const char delim);
void                     Import(
                        std::string filename = "alien:///alice/cern.ch/user/a/alihyperloop/outputs/0036/360353/83976/AnalysisResults.root",
                        std::string analysis = "PWGXX-YYY", std::string production = "LHCXXX", bool mc = false, Long64_t hyperloopId = -1,
                        std::string resultsFilename = "", std::string base = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/dev/alice",
                        std::string postfix = "inputs")

{
  if (analysis.empty()) {
    Printf("Please provide a valid analysis name !!! e.g. PWGXX-YYY");
    return;
  }
  if (production.empty()) {
    Printf("Please provide a valid production name !!! e.g. LHCXXX");
    return;
  }

  // checl id base has slash at the end, if yes remove it
  if (base.back() == '/') {
    base.pop_back();
  }

  if (!postfix.empty()) {
    base += "/" + postfix;
  }

  base += "/" + analysis;

  if (mc) {
    production = "0/" + production;
  }
  else {
    production += "/0";
  }
  base += "/" + production;

  std::vector<std::string> tokens  = Tokenize(filename, '/');
  int                      nTokens = tokens.size();
  if (hyperloopId < 0) {
    hyperloopId = std::stol(tokens[nTokens - 3]);
  }
  base += "/" + std::to_string(hyperloopId);

  if (resultsFilename.empty()) {
    resultsFilename = tokens[nTokens - 1];
  }
  base += "/" + resultsFilename;

  std::string outFilename = base;
  Printf("Copying %s to %s", filename.c_str(), outFilename.c_str());
  // check if filename starts with alien://
  if (filename.find("alien://") == 0) {
    if (!TGrid::Connect("alien://")) {
      Printf("Failed to connect to alien !!!");
      return;
    }
  }
  if (!TFile::Cp(filename.c_str(), outFilename.c_str())) {
    Printf("Failed to copy %s to %s", filename.c_str(), outFilename.c_str());
    return;
  }
  // root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/bins/7/1/6/6/AnalysisResults.root
  // /eos/ndmspc/scratch/alice/hyperloop/PWGLF-376/354351/AnalysisResults.root
  // root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/bins/1/8/2/6/AnalysisResults.root
  // /eos/ndmspc/scratch/alice/hyperloop/PWGLF-376/354440/AnalysisResults.root
}
std::vector<std::string> Tokenize(std::string_view input, const char delim)
{
  ///
  /// Tokenize helper function
  ///

  std::vector<std::string> out;

  for (auto found = input.find(delim); found != std::string_view::npos; found = input.find(delim)) {
    out.emplace_back(input, 0, found);
    input.remove_prefix(found + 1);
  }

  if (not input.empty()) out.emplace_back(input);

  return out;
}
