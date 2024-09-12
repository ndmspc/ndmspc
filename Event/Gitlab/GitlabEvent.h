#ifndef GitlabEvent_H
#define GitlabEvent_H

#include <TObject.h>
#include <TH1S.h>
#include <TClonesArray.h>
#include <TDatime.h>
#include "GitlabTrack.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace NdmSpc {
namespace Gitlab {
///
/// \class Event
///
/// \brief Event object
/// \author Martin Vala <mvala@cern.ch>
///

class Event : public TObject {

  public:
  Event();
  Event(Long64_t id);
  virtual ~Event();

  /// \fn Long64_t GetID() const
  /// Event ID
  /// \return event ID
  ///
  /// \fn void SetID(Long64_t id)
  /// \param id Event ID
  ///
  /// Sets event ID
  ///
  /// \fn TDatime GetDateTime() const
  /// Event date and time
  /// \return date and time
  ///
  /// \fn void SetTimeDate(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t min, Int_t sec)
  /// \param year Year of event
  /// \param month Month of event
  /// \param day Day of event
  /// \param hour Hour of event
  /// \param min Minute of event
  /// \param sec Second of event
  ///
  /// Sets Date and time of event
  ///
  /// \fn Long64_t GetNIssues() const
  /// \return number of tracks
  ///
  /// \fn Track *GetIssue(Long64_t id)
  /// \param id Track ID
  /// \return Track with id
  ///
  /// \fn Long64_t GetNMergeRequests() const
  /// \return number of tracks
  ///
  /// \fn Track *GetMergeRequest(Long64_t id)
  /// \param id Track ID
  /// \return Track with id
  ///

  /// \fn TAxis GetListOfAuthors() const
  /// Returns List of authors as TAxis
  /// \return Axis object
  ///
  /// \fn void SetListOfAuthors(TAxis *a)
  /// \param a Axis object
  ///

  /// \fn TAxis GetListOfProjects() const
  /// Returns List of Projects as TAxis
  /// \return Axis object
  ///
  /// \fn void SetListOfProjects(TAxis *a)
  /// \param a Axis object
  ///

  ///
  /// \fn std::string ParseProjectName(std::string in) const;
  /// \return Parsed project name

  Long64_t GetID() const { return fID; }
  TDatime  GetDateTime() const { return fDateTime; }

  void SetID(Long64_t id) { fID = id; }
  void SetTimeDate(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t min, Int_t sec);

  Long64_t GetNIssues() const { return fNIssues; }
  Track *  GetIssue(Long64_t id) { return (Track *)fIssues->At(id); }
  Track *  AddIssue();

  Long64_t GetNMergeRequests() const { return fNMergeRequests; }
  Track *  GetMergeRequest(Long64_t id) { return (Track *)fMergeRequests->At(id); }
  Track *  AddMergeRequest();

  void SetListOfAuthors(TH1S * a) { fAuthors = a; }
  void SetListOfProjects(TH1S * a) { fProjects = a; }

  TH1S * GetListOfAuthors() const { return fAuthors; }
  TH1S * GetListOfProjects() const { return fProjects; }

  bool FillGitlabFromJson(std::string issues, std::string mergrerequests);
  bool FillIssuesFromJson(const json root);
  bool FillMergeRequestsFromJson(const json root);

  virtual void Print(Option_t * option = "") const;
  virtual void Clear(Option_t * option = "");

  private:
  Long64_t fID;             ///< ID of event
  TDatime  fDateTime;       ///< Time of event
  Int_t    fNIssues;        ///< Number of Issues
  Int_t    fNMergeRequests; ///< Number of MergeRequests

  /// Array with all issues
  TClonesArray * fIssues; //->
  /// Array with all merge requests
  TClonesArray * fMergeRequests; //->

  TH1S * fAuthors{nullptr};  ///! List of authors in current event
  TH1S * fProjects{nullptr}; ///! List of projects in current event

  // TODO
  /// Copy constructor
  Event(const Event &);             /// not implemented
  Event & operator=(const Event &); /// not implemented

  std::string ParseProjectName(std::string in, char d = '!') const;
  void        FillAuthorProjectAxis(std::string author, std::string project);

  /// \cond CLASSIMP
  ClassDef(Event, 1);
  /// \endcond
}; // namespace TObject
} // namespace Gitlab
} // namespace NdmSpc
#endif
