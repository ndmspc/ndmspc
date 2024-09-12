#if defined(__CINT__) || defined(__ROOTCLING__)
#include <TString.h>
#include <TFile.h>
#include <TTree.h>
#include <TSystemDirectory.h>
#include <GitlabEvent.h>
#include <GitlabTrack.h>
#endif

void import(std::string basedir = "./data/gitlab/6534672/day", std::string filename = "gitlab.root", int refresh = 100)
{
  TH1::AddDirectory(false);
  TFile * f = TFile::Open(filename.data(), "RECREATE");
  if (!f) {
    Printf("Error: File '%s' cannot be opened !!!", filename.data());
    return;
  }

  TH1S * authors  = new TH1S("authors", "Authors", 0, 0, 0);
  TH1S * projects = new TH1S("projects", "Projects", 0, 0, 0);
  /*authors->SetNameTitle("authors", "Authors");*/
  /*projects->SetNameTitle("projects", "Projects");*/

  NdmSpc::Gitlab::Track * t;
  NdmSpc::Gitlab::Event * ev = new NdmSpc::Gitlab::Event(0);
  ev->SetListOfAuthors(authors);
  ev->SetListOfProjects(projects);

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

  if (authors->GetXaxis()->GetNbins() > 0 && projects->GetXaxis()->GetNbins() > 0) {
    int         count = 0;
    std::string s;
    for (Int_t i = 1; i < authors->GetXaxis()->GetNbins(); i++) {
      s = authors->GetXaxis()->GetBinLabel(i);
      if (!s.empty()) {
        count++;
      }
      else {
        break;
      }
    }
    authors->GetXaxis()->Set(count, 0, count);
    count = 0;
    for (int i = 1; i < projects->GetXaxis()->GetNbins(); i++) {
      s = projects->GetXaxis()->GetBinLabel(i);
      if (!s.empty()) {
        count++;
      }
      else {
        break;
      }
    }
    projects->GetXaxis()->Set(count, 0, count);

    tree.GetUserInfo()->Add(authors->Clone());
    tree.GetUserInfo()->Add(projects->Clone());
    for (Int_t i = 1; i < authors->GetXaxis()->GetNbins(); i++) {
      Printf("author label [%d] : %s", i, authors->GetXaxis()->GetBinLabel(i));
    }
    for (Int_t i = 1; i < projects->GetXaxis()->GetNbins(); i++) {
      Printf("project label [%d] : %s", i, projects->GetXaxis()->GetBinLabel(i));
    }
  }

  // f->Write("", TObject::kOverwrite);
  tree.Write();
  f->Close();
  delete ev;
}
