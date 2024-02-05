#include <TFile.h>
#include <TFileMerger.h>
#include <TObjArray.h>
#include <TObjString.h>
#include <TString.h>
#include <TSystem.h>
#include <TUrl.h>
#include <sstream>

int NdmspcMerge(TString findDirectory = "root://eos.ndmspc.io//eos/ndmspc/scratch/test/tmp",
                TString outfile = "/tmp/merged.root", TString contentFile = "content.root",
                TString objectsToMerge = "results", TString fileOpt = "?remote=1")
{

    TUrl    url(findDirectory);
    TString outHost        = url.GetHost();
    TString inputDirectory = url.GetFile();

    TString findUrl = TString::Format(
        "root://%s//proc/user/?mgm.cmd=find&mgm.find.match=%s&mgm.path=%s&mgm.format=json&mgm.option=f&filetype=raw",
        outHost.Data(), contentFile.Data(), inputDirectory.Data());

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
    // Vector of string to save tokens
    std::vector<std::string> tokens;

    // stringstream class check1
    std::stringstream check1(content);

    std::string intermediate;

    // Tokenizing w.r.t. space '&'
    while (getline(check1, intermediate, '&')) {
        tokens.push_back(intermediate);
    }

    std::stringstream check2(tokens[0]);
    std::string       line;

    TFileMerger m(kFALSE);
    m.AddObjectNames(objectsToMerge.Data());
    m.OutputFile(TString::Format("%s%s", outfile.Data(), fileOpt.Data()));
    Int_t default_mode = TFileMerger::kAll | TFileMerger::kIncremental;
    Int_t mode         = default_mode | TFileMerger::kOnlyListed;
    while (std::getline(check2, line)) {

        Printf("Adding file '%s' ...", line.data());
        m.AddFile(TString::Format("root://%s/%s", outHost.Data(), line.data()).Data());
    }

    Printf("Merging ...");
    m.PartialMerge(mode);
    Printf("Output: '%s'", outfile.Data());
    Printf("Done ...");
    return 0;
}