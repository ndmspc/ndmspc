#include <TFile.h>
#include <TFileMerger.h>
#include <TObjArray.h>
#include <TObjString.h>
#include <TString.h>
#include <TSystem.h>
#include <TUrl.h>
#include <sstream>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

int NdmspcMerge(std::string configFile = "myAnalysis.json", TString fileOpt = "?remote=1")
{

  json          cfg;
  std::ifstream f(configFile);
  if (f.is_open()) {
    cfg = json::parse(f);
  }
  else {
    Printf("Error: Config file '%s' was not found !!!", configFile.c_str());
    return 1;
  }

  std::string hostUrl = cfg["ndmspc"]["output"]["host"].get<std::string>();
  if (hostUrl.empty()) {
    Printf("Error:  cfg[ndmspc][output][host] is empty!!!");
    return 2;
  }

  std::string path = hostUrl + "/" + cfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  int nDimsCuts = 0;
  for (auto & cut : cfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    path += cut["axis"].get<std::string>() + "_";
    nDimsCuts++;
  }
  path[path.size() - 1] = '/';
  std::string outFile = path + "results.root";

  path += "bins";

  TUrl        url(path.c_str());
  std::string outHost        = url.GetHost();
  std::string inputDirectory = url.GetFile();

  Printf("Doing eos find -f %s", path.c_str());

  std::string contentFile = cfg["ndmspc"]["output"]["file"].get<std::string>();
  TString     findUrl;

  // Vector of string to save tokens
  std::vector<std::string> tokens;

  if (!inputDirectory.empty()) {
    findUrl = TString::Format(
        "root://%s//proc/user/?mgm.cmd=find&mgm.find.match=%s&mgm.path=%s&mgm.format=json&mgm.option=f&filetype=raw",
        outHost.c_str(), contentFile.c_str(), inputDirectory.c_str());

    TFile * f = TFile::Open(findUrl.Data());
    if (!f) return 1;

    // Printf("%lld", f->GetSize());

    int  buffsize = 4096;
    char buff[buffsize + 1];

    Long64_t    buffread = 0;
    std::string content;
    while (buffread < f->GetSize()) {

      if (buffread + buffsize > f->GetSize()) buffsize = f->GetSize() - buffread;

      // Printf("Buff %lld %d", buffread, buffsize);
      f->ReadBuffer(buff, buffread, buffsize);
      buff[buffsize] = '\0';
      content += buff;
      buffread += buffsize;
    }

    f->Close();

    std::string ss  = "mgm.proc.stdout=";
    size_t      pos = ss.size() + 1;
    content         = content.substr(pos);

    // stringstream class check1
    std::stringstream check1(content);

    std::string intermediate;

    // Tokenizing w.r.t. space '&'
    while (getline(check1, intermediate, '&')) {
      tokens.push_back(intermediate);
    }
  }
  else {
    tokens.push_back(contentFile.c_str());
  }

  std::stringstream check2(tokens[0]);
  std::string       line;
  std::string       outFileLocal = "/tmp/ndmspc-merged-" + std::to_string(gSystem->GetPid()) + ".root";

  TFileMerger m(kFALSE);
  m.OutputFile(TString::Format("%s%s", outFileLocal.c_str(), fileOpt.Data()));
  // m.AddObjectNames("results");
  // m.AddObjectNames("content");
  // Int_t default_mode = TFileMerger::kAll | TFileMerger::kIncremental;
  // Int_t mode         = default_mode | TFileMerger::kOnlyListed;
  while (std::getline(check2, line)) {

    Printf("Adding file '%s' ...", line.data());
    if (!outHost.empty()) {
      m.AddFile(TString::Format("root://%s/%s", outHost.c_str(), line.c_str()).Data());
    }
    else {
      m.AddFile(line.c_str());
    }
  }

  Printf("Merging ...");
  m.Merge();
  // m.PartialMerge(mode);

  Printf("Copy '%s' to '%s' ...", outFileLocal.c_str(), outFile.c_str());
  TFile::Cp(outFileLocal.c_str(), outFile.c_str());
  std::string rm = "rm -f " + outFileLocal;
  Printf("Doing '%s' ...", rm.c_str());
  gSystem->Exec(rm.c_str());
  Printf("Output: '%s'", outFile.c_str());
  Printf("Done ...");
  return 0;
}
