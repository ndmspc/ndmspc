#include <TH1.h>
#include <TSystem.h>
#include <TGrid.h>
#include "Logger.h"
#include "Config.h"
#include "HttpRequestCurl.h"
#include "AliceUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AliceUtils);
/// \endcond

namespace Ndmspc {

std::string AliceUtils::QueryTrainInfo(int id)
{
  auto logger = Ndmspc::Logger::getInstance("");

  HttpRequestCurl req;
  std::string     response;
  std::string     homedir           = gSystem->HomeDirectory();
  std::string     cert_path         = homedir + "/.globus/usercert.pem";
  std::string     key_path          = homedir + "/.globus/userkey.pem";
  std::string     key_password_file = homedir + "/.globus/password.txt";
  std::string     url = "https://alimonitor.cern.ch/alihyperloop-data/trains/train.jsp?train_id=" + std::to_string(id);
  bool            insecure = true;

  try {
    response = req.get(url, cert_path, key_path, key_password_file, insecure);
    // std::cout << response << std::endl;
  }
  catch (const std::exception & e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return ""; // Indicate an error
  }

  return response;
}

bool AliceUtils::InputsFromHyperloop(std::string config, std::set<int> ids, std::string analysis)
{
  ///
  /// Initialize the analysis
  ///
  TH1::AddDirectory(kFALSE);

  auto logger = Ndmspc::Logger::getInstance("");

  auto c = new Ndmspc::Config(config);
  if (c == nullptr) {
    logger->Error("Problem getting config file %s", config.c_str());
    return false;
  }

  // loop over all ids and call query hyperloop
  for (auto id : ids) {
    std::string response = QueryTrainInfo(id);
    // TODO: Fix message string size in logger
    // logger->Info("Train %d: %s", id, response.c_str());
    json j = json::parse(response);
    // logger->Info("Train %d: %s", id, j.dump().c_str());
    // Print train id dataset_name dataset_production_type mergeResults[0]["outputdir"]
    bool isMC = j["dataset_production_type"].get<std::string>() == "MC" ? true : false;
    logger->Info("Train %d %s %s %s", j["id"].get<int>(), j["dataset_name"].get<std::string>().c_str(),
                 j["dataset_production_type"].get<std::string>().c_str(),
                 j["mergeResults"][0]["outputdir"].get<std::string>().c_str());
    Import("alien://" + j["mergeResults"][0]["outputdir"].get<std::string>() + "/AnalysisResults.root", analysis,
           j["dataset_name"].get<std::string>(), isMC, id);
  }

  // if (c) c->Print();

  return true;
}

void AliceUtils::Import(std::string filename, std::string analysis, std::string production, bool mc,
                        Long64_t hyperloopId, std::string resultsFilename, std::string base, std::string postfix)

{
  auto logger = Ndmspc::Logger::getInstance("");
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

  std::vector<std::string> tokens  = Ndmspc::Utils::Tokenize(filename, '/');
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

  // check if outFilename file exists
  if (TFile::Open(outFilename.c_str())) {
    Printf("File %s already exists !!!", outFilename.c_str());
    return;
  }

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

} // namespace Ndmspc
