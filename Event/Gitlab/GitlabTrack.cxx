#include <TString.h>
#include <TRandom.h>
#include <TMath.h>
#include "GitlabTrack.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Gitlab::Track);
/// \endcond

namespace NdmSpc {
namespace Gitlab {

Track::Track() : TObject(), fState(), fAuthorID(0), fProjectID(0)
{
  ///
  /// A constructor
  ///
}

Track::~Track()
{
  ///
  /// A destructor
  ///
}

void Track::BuildRandom()
{
  ///
  /// Building random event
  ///
}

void Track::Print(Option_t * option) const
{
  ///
  /// Printing track info
  ///
  TString opt = option;

  Printf("[%s] state=%s author_id=%d (%s) project_id=%d (%s) milestone=%d (%s)", opt.Data(), fState.data(), fAuthorID,
         fAuthor.c_str(), fProjectID, fProject.c_str(), fMilestoneID, fMilestone.c_str());
}

void Track::Clear(Option_t *)
{
  ///
  /// Reseting track to default values
  ///

  fState       = "";
  fAuthorID    = 0;
  fAuthor      = "";
  fProjectID   = 0;
  fProject     = "";
  fMilestoneID = 0;
  fMilestone   = "";
}

} // namespace Gitlab
} // namespace NdmSpc
