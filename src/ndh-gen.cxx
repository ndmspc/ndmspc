#include <getopt.h>
#include <TFile.h>
#include <TH2.h>
#include "HnSparse.h"
#include "HnSparseStress.h"
#include "ndmspc.h"

void version()
{
  Printf("%s v%d.%d.%d-%s", NDMSPC_NAME, NDMSPC_VERSION_MAJOR(NDMSPC_VERSION), NDMSPC_VERSION_MINOR(NDMSPC_VERSION),
         NDMSPC_VERSION_PATCH(NDMSPC_VERSION), NDMSPC_VERSION_RELEASE);
}

[[noreturn]] void help(int rc = 0)
{
  version();
  Printf("\nUsage: [OPTION]...");
  Printf("\nOptions:");
  Printf("       -d, --dimentsions[=VALUE]    number of dimensions (default: 2)");
  Printf("       -b, --bins[=VALUE]           number of bins per axis (default: 5)");
  Printf("       -f, --fill[=VALUE]           fill size (default : 1e5)");
  Printf("       -s, --start[=VALUE]          start (default : 0)");
  Printf("       -r, --reserve[=VALUE]        reserve bins (default : 0 - nothing is reserved)");
  Printf("       -p, --print-refresh[=VALUE]  print refresh (default : 1)");
  Printf("       -c, --chunk[=VALUE]          chunk size (default : 1024*16)");
  Printf("       -o, --output[=VALUE]         output filename (default: \"\")");
  Printf("       -z, --fill-random            fill random");
  Printf("\n       -h, --help                   display this help and exit");
  Printf("       -v, --version                output version information and exit");
  Printf("       -x, --debug[=VALUE]          debug level");
  Printf("\nExamples:");
  Printf("       %s-gen -s 1e5", NDMSPC_NAME);
  Printf("                                    Generate default histogram with 1e5 entries");
  Printf("\nReport bugs at <https://gitlab.com/ndmspc/ndmspc>");
  Printf("General help using GNU software: <https://www.gnu.org/gethelp/>");

  exit(rc);
}
int main(int argc, char ** argv)
{

  // ***** Default values START *****
  /// Config file
  Int_t       debug         = 0;
  Int_t       nPrintRefresh = 1;
  std::string filename      = "";

  int         nDim      = 2;
  int         nBins     = 5;
  Long64_t    nFill     = 1e5;
  Long64_t    startFill = 0;
  std::string start_str;
  Int_t       chunkSize     = 1024 * 16;
  Long64_t    nBinsReserved = 0;
  bool        fillRandom    = false;
  // ***** Default values END *****

  std::string   shortOpts  = "hvzd:b:f:s:o:x:r:p:c:W;";
  struct option longOpts[] = {{"help", no_argument, nullptr, 'h'},
                              {"version", no_argument, nullptr, 'v'},
                              {"dims", required_argument, nullptr, 'd'},
                              {"bins", required_argument, nullptr, 'b'},
                              {"fill", required_argument, nullptr, 'f'},
                              {"start", required_argument, nullptr, 's'},
                              {"fill-random", no_argument, nullptr, 'z'},
                              {"output", required_argument, nullptr, 'o'},
                              {"debug", required_argument, nullptr, 'x'},
                              {"reserve", required_argument, nullptr, 'r'},
                              {"print-refresh", required_argument, nullptr, 'p'},
                              {"chunk", required_argument, nullptr, 'c'},
                              {nullptr, 0, nullptr, 0}};

  int nextOption = 0;
  do {
    nextOption = getopt_long(argc, argv, shortOpts.c_str(), longOpts, nullptr);
    switch (nextOption) {
    case -1:
    case 0: break;
    case 'h': help();
    case 'v':
      version();
      exit(0);
      break;
    case 'd': nDim = atoi(optarg); break;
    case 'b': nBins = atoi(optarg); break;
    case 'f': nFill = (Long64_t)atof(optarg); break;
    case 's': start_str = optarg; break;
    case 'o': filename = optarg; break;
    case 'x': debug = atoi(optarg); break;
    case 'z': fillRandom = true; break;
    case 'r': nBinsReserved = (Long64_t)atof(optarg); break;
    case 'p': nPrintRefresh = (Int_t)atof(optarg); break;
    case 'c': chunkSize = (Int_t)atof(optarg); break;
    default: help(1);
    }
  } while (nextOption != -1);

  // Handling start (supports 1x, 2x, ...)
  if (!start_str.empty()) {
    if (start_str[start_str.length() - 1] == 'x') {
      start_str.pop_back();
      startFill = (Long64_t)(atof(start_str.data()) * nFill);
    }
    else {
      startFill = (Long64_t)atof(start_str.data());
    }
  }

  version();

  double min = -(Double_t)nBins / 2;
  double max = (Double_t)nBins / 2;

  Int_t    bins[nDim];
  Double_t mins[nDim];
  Double_t maxs[nDim];
  for (Int_t i = 0; i < nDim; i++) {
    bins[i] = nBins;
    mins[i] = min;
    maxs[i] = max;
  }

  NdmSpc::Ndh::HnSparseD * h =
      new NdmSpc::Ndh::HnSparseD("hTest", "Testing histogram", nDim, bins, mins, maxs, chunkSize);
  if (nBinsReserved) h->ReserveBins(nBinsReserved);
  NdmSpc::Ndh::HnSparseStress stress;
  stress.SetDebugLevel(debug);
  stress.SetPrintRefresh(nPrintRefresh);
  stress.SetRandomFill(fillRandom);
  Printf("Starting to fill at %lld random=%d...", startFill, fillRandom);
  if (!stress.Generate(h, nFill, startFill)) return 1;
  h->Print();
  Long64_t nBinsSizeBytes = sizeof(Double_t) * h->GetNbins();

  if (!filename.empty()) {
    Printf("Saving output to file '%s' ...", filename.data());
    TFile * f = TFile::Open(filename.data(), "RECREATE");
    h->Write();
    Printf("Memory : %03.2f MB (%lld B) File: %03.2f MB (%lld B)", (double)nBinsSizeBytes / (1024 * 1024),
           nBinsSizeBytes, (double)f->GetFileBytesWritten() / (1024 * 1024), f->GetFileBytesWritten());
    f->Close();
  }
  else {
    Printf("Memory : %03.2f MB (%lld B)", (double)nBinsSizeBytes / (1024 * 1024), nBinsSizeBytes);
  }

  return 0;
}
