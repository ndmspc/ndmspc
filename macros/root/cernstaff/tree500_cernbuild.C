/// \file
/// \ingroup tutorial_tree
/// \notebook -nodraw
/// Read data (CERN staff) from an ascii file and create a root file with a Tree.
///
/// \macro_code
/// \macro_output
///
/// \author Rene Brun

#include <set>
#include <TSystem.h>
#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>
#include <THnSparse.h>
#include <TAxis.h>
TFile * tree500_cernbuild(Int_t getFile = 0, Int_t print = 1)
{
  Int_t  Category;
  UInt_t Flag;
  Int_t  Age;
  Int_t  Service;
  Int_t  Children;
  Int_t  Grade;
  Int_t  Step;
  Int_t  Hrweek;
  Int_t  Cost;
  Char_t Division[4];
  Char_t Nation[3];

  // The input file cern.dat is a copy of the CERN staff data base from 1988
  TString filename = "cernstaff.root";
  TString dir      = gROOT->GetTutorialDir();
  dir.Append("/io/tree/");
  dir.ReplaceAll("/./", "/");
  FILE * fp = fopen(Form("%scernstaff.dat", dir.Data()), "r");
  if (!fp) {
    fp = fopen("cernstaff.dat", "r");
  }
  if (!fp) {
    printf("Cannot open cernstaff.dat file\n");
    return 0;
  }

  TFile * hfile = 0;
  if (getFile) {
    // if the argument getFile =1 return the file "cernstaff.root"
    // if the file does not exist, it is created
    if (!gSystem->AccessPathName(dir + "cernstaff.root", kFileExists)) {
      hfile = TFile::Open(dir + "cernstaff.root"); // in $ROOTSYS/tutorials/io/tree
      if (hfile) return hfile;
    }
    // otherwise try $PWD/cernstaff.root
    if (!gSystem->AccessPathName("cernstaff.root", kFileExists)) {
      hfile = TFile::Open("cernstaff.root"); // in current dir
      if (hfile) return hfile;
    }
  }
  // no cernstaff.root file found. Must generate it !
  // generate cernstaff.root in $ROOTSYS/tutorials/io/tree if we have write access
  if (gSystem->AccessPathName(".", kWritePermission)) {
    printf("you must run the script in a directory with write access\n");
    return 0;
  }
  hfile     = TFile::Open(filename, "RECREATE");
  auto tree = new TTree("T", "CERN 1988 staff data");
  tree->Branch("Category", &Category, "Category/I");
  tree->Branch("Flag", &Flag, "Flag/i");
  tree->Branch("Age", &Age, "Age/I");
  tree->Branch("Service", &Service, "Service/I");
  tree->Branch("Children", &Children, "Children/I");
  tree->Branch("Grade", &Grade, "Grade/I");
  tree->Branch("Step", &Step, "Step/I");
  tree->Branch("Hrweek", &Hrweek, "Hrweek/I");
  tree->Branch("Cost", &Cost, "Cost/I");
  tree->Branch("Division", Division, "Division/C");
  tree->Branch("Nation", Nation, "Nation/C");

  const Int_t nDim   = 11;
  int maximums[nDim] = {-kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, -kMaxInt, 0, 0};

  std::set<std::string> divisions;
  std::set<std::string> nations;

  char line[80];
  while (fgets(line, 80, fp)) {
    sscanf(&line[0], "%d %d %d %d %d %d %d  %d %d %s %s", &Category, &Flag, &Age, &Service, &Children, &Grade, &Step,
           &Hrweek, &Cost, Division, Nation);
    tree->Fill();

    int FlagInt = (int)Flag;
    if (Category > maximums[0]) maximums[0] = Category;
    if (FlagInt > maximums[1]) maximums[1] = FlagInt;
    if (Age > maximums[2]) maximums[2] = Age;
    if (Service > maximums[3]) maximums[3] = Service;
    if (Children > maximums[4]) maximums[4] = Children;
    if (Grade > maximums[5]) maximums[5] = Grade;
    if (Step > maximums[6]) maximums[6] = Step;
    if (Hrweek > maximums[7]) maximums[7] = Hrweek;
    if (Cost > maximums[8]) maximums[8] = Cost;

    // Printf("Category=%d, Flag=%u, Age=%d, Service=%d, Children=%d, Grade=%d, Step=%d, Hrweek=%d, Cost=%d, "
    //        "Division=%s, Nation=%s",
    //        Category, Flag, Age, Service, Children, Grade, Step, Hrweek, Cost, Division, Nation);

    divisions.insert(Division);
    nations.insert(Nation);
  }
  maximums[9]  = divisions.size() - 1;
  maximums[10] = nations.size() - 1;

  // Let's create THnSparse object to store tree reprezentation

  Int_t    bins[nDim];
  Double_t xmin[nDim], xmax[nDim];
  for (Int_t i = 0; i < nDim; ++i) {
    bins[i] = maximums[i] + 1;
    xmin[i] = 0;
    xmax[i] = maximums[i] + 1;
    // if (i == 0 || i == 8) {
    //   // bins[i] /= 10;
    // }
    // else {
    //   xmin[i] = 0;
    //   xmax[i] = maximums[i] + 1;
    // }
  }

  THnSparse * hsparse = new THnSparseF("hsparse", "CERN 1988 staff data", nDim, bins, xmin, xmax);
  if (hsparse == nullptr) {
    printf("Error creating THnSparse object\n");
    fclose(fp);
    delete hfile;
    return nullptr;
  }
  // Set axis names
  hsparse->GetAxis(0)->SetName("Category");
  hsparse->GetAxis(1)->SetName("Flag");
  hsparse->GetAxis(2)->SetName("Age");
  hsparse->GetAxis(3)->SetName("Service");
  hsparse->GetAxis(4)->SetName("Children");
  hsparse->GetAxis(5)->SetName("Grade");
  hsparse->GetAxis(6)->SetName("Step");
  hsparse->GetAxis(7)->SetName("Hrweek");
  hsparse->GetAxis(8)->SetName("Cost");
  hsparse->GetAxis(9)->SetName("Division");
  hsparse->GetAxis(10)->SetName("Nation");

  // set labels for division and nation axes
  int i = 1;
  for (const std::string & value : divisions) {
    hsparse->GetAxis(9)->SetBinLabel(i++, value.c_str());
  }
  i = 1;
  for (const std::string & value : nations) {
    hsparse->GetAxis(10)->SetBinLabel(i++, value.c_str());
  }

  Double_t values[nDim];
  // loop over tree entries
  for (Long64_t i = 0; i < tree->GetEntries(); ++i) {
    tree->GetEntry(i);
    // print some information
    // if (i < 10) {
    //   printf("Entry %lld: Category=%d, Flag=%u, Age=%d, Service=%d, Children=%d, Grade=%d, Step=%d, Hrweek=%d, "
    //          "Cost=%d, Division=%s, Nation=%s\n",
    //          i, Category, Flag, Age, Service, Children, Grade, Step, Hrweek, Cost, Division, Nation);
    // }
    values[0] = Category;
    values[1] = Flag;
    values[2] = Age;
    values[3] = Service;
    values[4] = Children;
    values[5] = Grade;
    values[6] = Step;
    values[7] = Hrweek;
    values[8] = Cost;
    values[9] =
        divisions.find(Division) != divisions.end() ? std::distance(divisions.begin(), divisions.find(Division)) : 0;
    values[10] = nations.find(Nation) != nations.end() ? std::distance(nations.begin(), nations.find(Nation)) : 0;
    hsparse->Fill(values);
  }

  if (print) tree->Print();
  tree->Write();

  if (hsparse) {
    hsparse->Write();
    if (print) hsparse->Print();
  }
  else {
    printf("Error creating THnSparse object\n");
  }

  fclose(fp);
  delete hfile;
  if (getFile) {
    // we come here when the script is executed outside $ROOTSYS/tutorials/io/tree
    hfile = TFile::Open(filename);
    return hfile;
  }
  return 0;
}
