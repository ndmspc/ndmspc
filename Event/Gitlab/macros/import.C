#if defined(__CINT__) || defined(__ROOTCLING__)
#include <TString.h>
#include <TFile.h>
#include <TTree.h>
#include <TSystemDirectory.h>
#include <GitlabEvent.h>
#include <GitlabTrack.h>
#endif

void ShrinkHistogram(const char * name, TH1 * h, bool verbose = true);
void import(std::string basedir = "./data/gitlab/6534672/day", std::string filename = "gitlab.root", int refresh = 100)
{
  TH1::AddDirectory(false);
  TFile * f = TFile::Open(filename.data(), "RECREATE");
  if (!f) {
    Printf("Error: File '%s' cannot be opened !!!", filename.data());
    return;
  }

  NdmSpc::Gitlab::Track * t;
  NdmSpc::Gitlab::Event * ev = new NdmSpc::Gitlab::Event(0);

  TTree tree("gitlabTree", "Ndhep Gitlab Tree");
  tree.Branch("Event", &ev);

  Int_t            iEvent = 0;
  TSystemDirectory dir(basedir.data(), basedir.data());
  TList *          dirs = dir.GetListOfFiles();
  dirs->Print();
  if (dirs) {
    dirs->Sort();
    TSystemFile * dir;
    TString       dname;
    TIter         next(dirs);
    while ((dir = (TSystemFile *)next())) {
      dname = dir->GetName();
      if (dir->IsDirectory() && !dname.BeginsWith(".")) {
        Printf("%s", dname.Data());
        ev->SetID(iEvent);

        ev->FillGitlabFromJson(TString::Format("%s/%s/issues.json", basedir.data(), dname.Data()).Data(),
                               TString::Format("%s/%s/mr.json", basedir.data(), dname.Data()).Data());
        // if (iEvent % refresh == 0)
        ev->Print();
        /*if (iEvent > 10) return;*/

        // for (auto & u : ev->GetListOfAuthors()) {
        //     Printf("%s", u.data());
        // }
        // for (auto & u : ev->GetListOfAuthors()) {
        //     Printf("%s", u.data());
        // }

        tree.Fill();
        ev->Clear();
        iEvent++;
      }
    }
  }

  ev->ShrinkMappingHistograms(true);

  tree.GetUserInfo()->Add(ev->GetListOfAuthors()->Clone());
  tree.GetUserInfo()->Add(ev->GetListOfProjects()->Clone());
  tree.GetUserInfo()->Add(ev->GetListOfMilestones()->Clone());

  // f->Write("", TObject::kOverwrite);
  tree.Write();
  f->Close();
  delete ev;
}
void ShrinkHistogram(const char * name, TH1 * h, bool verbose)
{
  if (!h) return;
  if (h->GetXaxis()->GetNbins() <= 0) return;
  int         count = 0;
  std::string s;
  for (int i = 1; i < h->GetXaxis()->GetNbins(); i++) {
    s = h->GetXaxis()->GetBinLabel(i);
    if (!s.empty()) {
      count++;
      Printf("%s label [%d] : %s", name, count, h->GetXaxis()->GetBinLabel(i));
    }
    else {
      break;
    }
  }
  h->GetXaxis()->Set(count, 0, count);
}
