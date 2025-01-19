#ifndef GitlabTrack_H
#define GitlabTrack_H

#include <TObject.h>
namespace Ndmspc {
namespace Gitlab {

///
/// \class Track
///
/// \brief Track object
///	\author Martin Vala <mvala@cern.ch>
///

class Track : public TObject {

  public:
  Track();
  virtual ~Track();

  /// \fn Int_t GetState() const
  /// Returns state
  /// \return fState state
  ///
  /// \fn void SetState(Int_t t)
  /// \param t Type of state
  ///

  /// \fn Int_t GetAuthorID() const
  /// Returns author id
  /// \return fAuthorID author id
  ///
  /// \fn void SetAuthorID(Int_t id)
  /// \param id Author ID
  ///

  /// \fn std::string GetAuthor() const
  /// Returns author
  /// \return fAuthor author name
  ///
  /// \fn void SetAuthor(std::string name)
  /// \param name Author name
  ///

  /// \fn Int_t GetProjectID() const
  /// Returns Project ID
  /// \return id Project id
  ///
  /// \fn void SetProjectID(Int_t id)
  /// \param id Project ID
  ///

  /// \fn std::string GetProject() const
  /// Returns Project
  /// \return fProject Project name
  ///
  /// \fn void SetProject(std::string name)
  /// \param name Project name
  ///

  /// \fn Int_t GetMilestoneID() const
  /// Returns Milestone ID
  /// \return id Milestone id
  ///
  /// \fn void SetMilestoneID(Int_t id)
  /// \param id Milestone ID
  ///

  /// \fn std::string GetMilestone() const
  /// Returns Milestone
  /// \return fMilestone Milestone name
  ///
  /// \fn void SetMilestone(std::string name)
  /// \param name Milestone name
  ///

  std::string GetState() const { return fState; }
  void        SetState(std::string t) { fState = std::move(t); }
  Int_t       GetAuthorID() const { return fAuthorID; }
  void        SetAuthorID(Int_t id) { fAuthorID = id; }
  std::string GetAuthor() const { return fAuthor; }
  void        SetAuthor(std::string name) { fAuthor = name; }
  Int_t       GetProjectID() const { return fProjectID; }
  void        SetProjectID(Int_t id) { fProjectID = id; }
  std::string GetProject() const { return fProject; }
  void        SetProject(std::string name) { fProject = name; }
  Int_t       GetMilestoneID() const { return fMilestoneID; }
  void        SetMilestoneID(Int_t id) { fMilestoneID = id; }
  std::string GetMilestone() const { return fMilestone; }
  void        SetMilestone(std::string name) { fMilestone = name; }

  virtual void Print(Option_t * option = "") const;
  virtual void Clear(Option_t * option = "");

  void BuildRandom();

  private:
  std::string fState;       ///< State of track
  Int_t       fAuthorID;    ///< Author id
  std::string fAuthor;      ///! Author name
  Int_t       fProjectID;   ///< Project ID
  std::string fProject;     ///! Project name
  Int_t       fMilestoneID; ///< Milestone ID
  std::string fMilestone;   ///! Milestone name

  // TODO
  /// Copy constructor
  Track(const Track &);             /// not implemented
  Track & operator=(const Track &); /// not implemented

  /// \cond CLASSIMP
  ClassDef(Track, 1);
  /// \endcond
};
} // namespace Gitlab
} // namespace Ndmspc
#endif
