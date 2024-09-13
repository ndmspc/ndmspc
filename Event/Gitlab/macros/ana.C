#if defined(__CINT__) || defined(__ROOTCLING__)
#include <Riostream.h>
#include <TString.h>
#include <TFile.h>
#include <TChain.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <GitlabEvent.h>
#include <GitlabTrack.h>
#endif

enum GitlabState { kOpened = 1, kClosed = 2, kMerged = 3 };

Int_t GetStateId(std::string s);
Int_t GetId(std::vector<int> & v, int id);

void FixMinMax(TH1 * h);
// eos -b find -f -name "ndhep.root" --xurl /eos/alike.saske.sk/scratch/gp/test/
TChain * GetChain(TString fname, TString treename = "ndhepTree");
void     CopyAxis(TAxis * in, TAxis * out);

// input can be list of files (ends with .txt) or single root file (ends with .root)
void ana(TString input = "gitlab.root", TString filenameOut = "gitlab_hist.root", int refresh = 1)
{
  TH1::AddDirectory(kFALSE);
  TH1::SetDefaultSumw2(kTRUE);

  TChain * ch = nullptr;
  if (input.EndsWith(".txt")) {
    ch = GetChain(input, "gitlabTree");
  }
  else {
    ch = new TChain("gitlabTree");
    ch->AddFile(input.Data());
  }
  if (!ch) return;

  NdmSpc::Gitlab::Event * ev = nullptr;
  ch->SetBranchAddress("Event", &ev);

  int     nTypes = 2;
  TAxis * types  = new TAxis(nTypes, 0, (double)nTypes);
  types->SetNameTitle("types", "Issie types");
  types->SetBinLabel(1, "issue");
  types->SetBinLabel(2, "merge request");

  int     nStates = 3;
  TAxis * states  = new TAxis(nStates, 0, (double)nStates);
  states->SetNameTitle("states", "State type");
  states->SetBinLabel(1, "opened");
  states->SetBinLabel(2, "closed");
  states->SetBinLabel(3, "merged");

  ch->GetEntry(0);
  TH1S *  authorsHist    = (TH1S *)ch->GetTree()->GetUserInfo()->FindObject("authors");
  TAxis * authors        = authorsHist->GetXaxis();
  int     nAuthors       = authors->GetNbins();
  TH1S *  projectsHist   = (TH1S *)ch->GetTree()->GetUserInfo()->FindObject("projects");
  TAxis * projects       = projectsHist->GetXaxis();
  int     nProjects      = projects->GetNbins();
  TH1S *  milestonesHist = (TH1S *)ch->GetTree()->GetUserInfo()->FindObject("milestones");
  TAxis * milestones     = milestonesHist->GetXaxis();
  int     nMilestones    = milestones->GetNbins();

  NdmSpc::Gitlab::Track * t;

  const Int_t nAxis       = 5;
  Int_t       bins[nAxis] = {nTypes, nStates, nAuthors, nProjects, nMilestones};
  Double_t    xmin[nAxis] = {0., 0., 0., 0., 0.};
  Double_t    xmax[nAxis] = {(double)nTypes, (double)nStates, (double)nAuthors, (double)nProjects, (double)nMilestones};
  Double_t    val[nAxis];
  int         uid, pid;
  THnSparse * h = new THnSparseD("hGitlabData", "N dimensional histogram of Gitlab data", nAxis, bins, xmin, xmax);
  h->GetAxis(0)->SetNameTitle("type", "Issue or Merge request");
  h->GetAxis(1)->SetNameTitle("state", "Issue or Merge request state");
  h->GetAxis(2)->SetNameTitle("user", "User");
  h->GetAxis(3)->SetNameTitle("project", "Project");
  h->GetAxis(4)->SetNameTitle("milestone", "Milestone");

  CopyAxis(types, h->GetAxis(0));
  CopyAxis(states, h->GetAxis(1));
  CopyAxis(authors, h->GetAxis(2));
  CopyAxis(projects, h->GetAxis(3));
  CopyAxis(milestones, h->GetAxis(4));

  TH1D * hUsers = new TH1D("hUsers", "User distribution", nAuthors, 0., (double)nAuthors);
  hUsers->GetXaxis()->SetTitle("Users");
  CopyAxis(authors, hUsers->GetXaxis());

  TH1D * hProjects = new TH1D("hProjects", "Project distribution", nProjects, 0., (double)nProjects);
  hProjects->GetXaxis()->SetTitle("Projects");
  CopyAxis(projects, hProjects->GetXaxis());

  TH2D * hUsersVsProjects = new TH2D("hUsersVsProjects", "User vs Project distribution", nAuthors, 0., (double)nAuthors,
                                     nProjects, 0., (double)nProjects);
  hUsersVsProjects->GetXaxis()->SetTitle("Users");
  hUsersVsProjects->GetYaxis()->SetTitle("Projects");
  CopyAxis(authors, hUsersVsProjects->GetXaxis());
  CopyAxis(projects, hUsersVsProjects->GetYaxis());

  TH3D * hUsersVsProjectsVsState =
      new TH3D("hUsersVsProjectsVsState", "User vs Project vs State distribution", nAuthors, 0., (double)nAuthors,
               nProjects, 0., (double)nProjects, nStates, 0., (double)nStates);
  hUsersVsProjectsVsState->GetXaxis()->SetTitle("Users");
  hUsersVsProjectsVsState->GetYaxis()->SetTitle("Projects");
  hUsersVsProjectsVsState->GetZaxis()->SetTitle("State");
  CopyAxis(authors, hUsersVsProjectsVsState->GetXaxis());
  CopyAxis(projects, hUsersVsProjectsVsState->GetYaxis());
  CopyAxis(states, hUsersVsProjectsVsState->GetZaxis());

  TH3D * hUsersVsProjectsVsMilestone =
      new TH3D("hUsersVsProjectsVsMilestone", "User vs Project vs Milestone distribution", nAuthors, 0.,
               (double)nAuthors, nProjects, 0., (double)nProjects, nMilestones, 0., (double)nMilestones);
  hUsersVsProjectsVsMilestone->GetXaxis()->SetTitle("Users");
  hUsersVsProjectsVsMilestone->GetYaxis()->SetTitle("Projects");
  hUsersVsProjectsVsMilestone->GetZaxis()->SetTitle("Milestone");
  CopyAxis(authors, hUsersVsProjectsVsMilestone->GetXaxis());
  CopyAxis(projects, hUsersVsProjectsVsMilestone->GetYaxis());
  CopyAxis(milestones, hUsersVsProjectsVsMilestone->GetZaxis());

  for (int iEvent = 0; iEvent < ch->GetEntries(); iEvent++) {
    ch->GetEntry(iEvent);
    if (iEvent % refresh == 0) ev->Print();
    for (int iIssue = 0; iIssue < ev->GetNIssues(); iIssue++) {
      // if (!iIssue) Printf("Issues:");
      t = ev->GetIssue(iIssue);
      t->Print("Issue");

      val[0] = 0.0;
      val[1] = h->GetAxis(1)->GetBinCenter(h->GetAxis(1)->FindBin(t->GetState().data()));
      val[2] = h->GetAxis(2)->GetBinCenter(h->GetAxis(2)->FindBin(t->GetAuthor().data()));
      val[3] = h->GetAxis(3)->GetBinCenter(h->GetAxis(3)->FindBin(t->GetProject().data()));
      val[4] = h->GetAxis(4)->GetBinCenter(h->GetAxis(4)->FindBin(t->GetMilestone().data()));
      h->Fill(val);
      hUsers->Fill(val[2]);
      hProjects->Fill(val[3]);
      hUsersVsProjects->Fill(val[2], val[3]);
      hUsersVsProjectsVsState->Fill(val[2], val[3], val[1]);
      hUsersVsProjectsVsMilestone->Fill(val[2], val[3], val[4]);
    }

    for (int iMR = 0; iMR < ev->GetNMergeRequests(); iMR++) {
      // if (!iMR) Printf("MRs:");
      t = ev->GetMergeRequest(iMR);

      t->Print("MR");
      val[0] = 1.0;
      val[1] = h->GetAxis(1)->GetBinCenter(h->GetAxis(1)->FindBin(t->GetState().data()));
      val[2] = h->GetAxis(2)->GetBinCenter(h->GetAxis(2)->FindBin(t->GetAuthor().data()));
      val[3] = h->GetAxis(3)->GetBinCenter(h->GetAxis(3)->FindBin(t->GetProject().data()));
      val[4] = h->GetAxis(4)->GetBinCenter(h->GetAxis(4)->FindBin(t->GetMilestone().data()));
      h->Fill(val);
      hUsers->Fill(val[2]);
      hProjects->Fill(val[3]);
      hUsersVsProjects->Fill(val[2], val[3]);
      hUsersVsProjectsVsState->Fill(val[2], val[3], val[1]);
      hUsersVsProjectsVsMilestone->Fill(val[2], val[3], val[4]);
    }
  }

  /*delete ev;*/
  TFile * fOut = TFile::Open(filenameOut.Data(), "RECREATE");
  h->Write();
  // FixMinMax(hUsers);
  hUsers->Write();
  // FixMinMax(hProjects);
  hProjects->Write();
  // FixMinMax(hUsersVsProjects);
  hUsersVsProjects->Write();
  // FixMinMax(hUsersVsProjectsVsState);
  hUsersVsProjectsVsState->Write();
  hUsersVsProjectsVsMilestone->Write();
  // fOut->WriteObject(&users, "users");
  // fOut->WriteObject(&projects, "projects");
  fOut->Close();

  // Printf("\nInfo:");
  // int i = 0;
  // Printf("  Users:");
  // for (auto & id : users) {
  //     Printf("    %d %d", i++, id);
  // }
  // i = 0;
  // Printf("  Projects:");
  // for (auto & id : projects) {
  //     Printf("    %d %d", i++, id);
  // }
}

TChain * GetChain(TString fname, TString treename)
{
  TChain * ch = new TChain(treename.Data());

  if (fname.EndsWith(".root")) {
    ch->AddFile(TString::Format("%s", fname.Data()).Data());
  }
  else {

    std::string   line;
    std::ifstream f(fname.Data());
    if (f.is_open()) {
      while (std::getline(f, line)) {
        ch->AddFile(TString::Format("%s", line.data()).Data());
      }
      f.close();
    }
  }

  return ch;
}

Int_t GetStateId(std::string s)
{
  if (s == "opened")
    return kOpened;
  else if (s == "merged")
    return kMerged;
  else if (s == "closed")
    return kClosed;

  return -1;
}

Int_t GetId(std::vector<int> & v, int id)
{

  std::vector<int>::iterator it;
  if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
  it    = std::find(v.begin(), v.end(), id);
  int i = std::distance(v.begin(), it);
  // Printf("%d %d", i, *it);/
  return i;
}

void FixMinMax(TH1 * h)
{
  if (!h) return;
  // h->SetMinimum(h->GetMinimum());
  // h->SetMinimum(0);
  h->SetMaximum(h->GetMaximum() * 1.5);
}

void CopyAxis(TAxis * in, TAxis * out)
{
  if (!in || !out) return;

  Printf("name_in=%s name_out=%s", in->GetName(), out->GetName());
  Printf("[0] bins=%d min=%f max=%f", in->GetNbins(), in->GetXmin(), in->GetXmax());
  Printf("[1] bins=%d min=%d max=%d", in->GetNbins(), 0, in->GetNbins());
  out->Set(in->GetNbins(), 0, in->GetNbins());
  for (Int_t i = 1; i <= in->GetNbins(); i++) {

    Printf("%d label=%s", i, in->GetBinLabel(i));
    out->SetBinLabel(i, in->GetBinLabel(i));
  }
}
