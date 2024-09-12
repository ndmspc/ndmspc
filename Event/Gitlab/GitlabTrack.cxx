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

  Printf("[%s] state=%s author=%d project=%d ", opt.Data(), fState.data(), fAuthorID, fProjectID);
}

void Track::Clear(Option_t *)
{
  ///
  /// Reseting track to default values
  ///

  fState     = "";
  fAuthorID  = 0;
  fProjectID = 0;
}

} // namespace Gitlab
} // namespace NdmSpc
