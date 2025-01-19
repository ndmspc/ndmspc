#include <fstream>
#include <sstream>
#include <TString.h>
#include <TRandom.h>
#include "GitlabEvent.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Gitlab::Event);
/// \endcond

namespace Ndmspc {
namespace Gitlab {

Event::Event() : TObject(), fID(0), fNIssues(0), fNMergeRequests(0), fIssues(0), fMergeRequests(0)
{
  ///
  /// Default constructor
  ///
}

Event::Event(Long64_t id) : TObject(), fID(id), fNIssues(0), fNMergeRequests(0), fIssues(0), fMergeRequests(0)
{
  ///
  /// A constructor
  ///
  fIssues        = new TClonesArray("Ndmspc::Gitlab::Track");
  fMergeRequests = new TClonesArray("Ndmspc::Gitlab::Track");
  fAuthors       = new TH1S("authors", "Authors", 0, 0, 0);
  fProjects      = new TH1S("projects", "Projects", 0, 0, 0);
  fMilestones    = new TH1S("milestones", "Milestones", 0, 0, 0);
  fMilestones->GetXaxis()->FindBin("none");
  gRandom->SetSeed(0);
}

Event::~Event()
{
  ///
  /// A destructor
  ///

  SafeDelete(fIssues);
  SafeDelete(fMergeRequests);
  SafeDelete(fAuthors);
  SafeDelete(fProjects);
  SafeDelete(fMilestones);
}

Track * Event::AddIssue()
{
  ///
  /// Adds Issue to event
  ///
  return (Track *)fIssues->ConstructedAt(fNIssues++);
}

Track * Event::AddMergeRequest()
{
  ///
  /// Adds Merge requests to event
  ///
  return (Track *)fMergeRequests->ConstructedAt(fNMergeRequests++);
}

bool Event::FillGitlabFromJson(std::string issues, std::string mergrerequests)
{
  ///
  /// Import gitlab info from json input
  ///

  json iss, mrs;
  // Json::String errs;
  std::string errs;

  if (!issues.empty()) {
    Printf("Processing '%s' ...", issues.data());
    std::ifstream fileIssues(issues.data());
    if (!fileIssues.is_open()) {
      Printf("Error: Unable to open file '%s' !!!", issues.data());
      return false;
    }

    iss = json::parse(fileIssues);
    FillIssuesFromJson(iss);
  }

  if (!mergrerequests.empty()) {
    Printf("Processing '%s' ...", mergrerequests.data());
    std::ifstream fileMRs(mergrerequests);
    if (!fileMRs.is_open()) {
      Printf("Error: Unable to open file '%s' !!!", mergrerequests.data());
      return false;
    }

    mrs = json::parse(fileMRs);
    FillMergeRequestsFromJson(mrs);
  }
  return true;
}
bool Event::FillIssuesFromJson(const json root)
{
  ///
  /// Import gitlab issues info from json input
  ///

  for (const auto & jv : root) {
    Track * t = AddIssue();
    t->SetState(jv["state"].get<std::string>());
    t->SetProjectID(jv["project_id"].get<int>());
    t->SetAuthorID(jv["author"]["id"].get<int>());
    t->SetProject(ParseProjectName(jv["references"]["full"].get<std::string>(), '#'));
    t->SetAuthor(jv["author"]["username"].get<std::string>());
    if (!jv["milestone"].is_null()) {
      t->SetMilestoneID(jv["milestone"]["id"].get<int>());
      t->SetMilestone(jv["milestone"]["title"].get<std::string>());
    }
    else {
      t->SetMilestoneID(-1);
      t->SetMilestone("none");
    }
    fAuthors->GetXaxis()->FindBin(t->GetAuthor().c_str());
    fProjects->GetXaxis()->FindBin(t->GetProject().c_str());
    fMilestones->GetXaxis()->FindBin(t->GetMilestone().c_str());
    Printf("Issue %d project [%s] author [%s] state [%s]", jv["iid"].get<int>(), t->GetProject().data(),
           t->GetAuthor().data(), t->GetState().c_str());
  }

  return true;
}
bool Event::FillMergeRequestsFromJson(const json root)
{
  ///
  /// Import gitlab merge requests info from json input
  ///

  for (const auto & jv : root) {
    Track * t = AddMergeRequest();
    t->SetState(jv["state"].get<std::string>());
    t->SetProjectID(jv["project_id"].get<int>());
    t->SetAuthorID(jv["author"]["id"].get<int>());
    t->SetProject(ParseProjectName(jv["references"]["full"].get<std::string>(), '!'));
    t->SetAuthor(jv["author"]["username"].get<std::string>());
    if (!jv["milestone"].is_null()) {
      t->SetMilestoneID(jv["milestone"]["id"].get<int>());
      t->SetMilestone(jv["milestone"]["title"].get<std::string>());
    }
    else {
      t->SetMilestoneID(-1);
      t->SetMilestone("none");
    }
    fAuthors->GetXaxis()->FindBin(t->GetAuthor().c_str());
    fProjects->GetXaxis()->FindBin(t->GetProject().c_str());
    fMilestones->GetXaxis()->FindBin(t->GetMilestone().c_str());
    Printf("MR %d project [%s] author [%s] state [%s]", jv["iid"].get<int>(), t->GetProject().data(),
           t->GetAuthor().data(), t->GetState().c_str());
  }

  return true;
}

void Event::Print(Option_t * option) const
{
  ///
  /// Printing event info
  ///
  Printf("id=%lld nIssues=%d nMergeRequests=%d", fID, fNIssues, fNMergeRequests);

  // if (!fTracks) return;

  // TString str(option);
  // str.ToLower();
  // if (str.Contains("all")) {
  //     Track * t;
  //     for (Int_t i = 0; i < fTracks->GetEntries(); i++) {
  //         t = (Track *)fTracks->At(i);
  //         t->Print();
  //     }
  // }
}
void Event::ShrinkMappingHistograms(bool verbose)
{
  ///
  /// Shrink mapping histograms
  ///
  ShrinkHistogram("autors", fAuthors, verbose);
  ShrinkHistogram("projects", fProjects, verbose);
  ShrinkHistogram("milestoness", fMilestones, verbose);
}

void Event::Clear(Option_t *)
{
  ///
  /// Reseting event to default values and clear all tracks
  ///
  fID = 0;
  // fDateTime.Reset();

  fNIssues = 0;
  fIssues->Clear("C");
  fNMergeRequests = 0;
  fMergeRequests->Clear("C");
}

void Event::SetTimeDate(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t min, Int_t sec)
{
  ///
  /// Sets Date and time of event
  ///
  fDateTime.Set(year, month, day, hour, min, sec);
}

std::string Event::ParseProjectName(std::string in, char d) const
{
  ///
  /// Parse project name
  ///
  std::string       s;
  std::stringstream ss(in);
  std::getline(ss, s, d);

  return std::move(s);
}
void Event::ShrinkHistogram(const char * name, TH1 * h, bool verbose)
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
} // namespace Gitlab

} // namespace Ndmspc
