#include <TString.h>
#include <THttpCallArg.h>
#include "CloudEvent.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::CloudEvent);
/// \endcond

namespace NdmSpc {

CloudEvent::CloudEvent(std::string id, std::string source, std::string specVersion, std::string type)
    : TObject(), fId(id), fSource(source), fSpecVersion(specVersion), fType(type)
{
}
CloudEvent::CloudEvent(THttpCallArg * arg)
{
  HandleCloudEventRequest(arg);
}
CloudEvent::~CloudEvent() {}

void CloudEvent::HandleCloudEventRequest(THttpCallArg * arg)
{
  Clear();
  if (!arg->GetRequestHeader("Content-Type").CompareTo("application/json")) {
    fId          = arg->GetRequestHeader("Ce-Id").Data();
    fSource      = arg->GetRequestHeader("Ce-Source").Data();
    fSpecVersion = arg->GetRequestHeader("Ce-Specversion").Data();
    fType        = arg->GetRequestHeader("Ce-Type").Data();
  }
  else if (!arg->GetRequestHeader("content-type").CompareTo("application/json")) {
    fId          = arg->GetRequestHeader("ce-id").Data();
    fSource      = arg->GetRequestHeader("ce-source").Data();
    fSpecVersion = arg->GetRequestHeader("ce-specversion").Data();
    fType        = arg->GetRequestHeader("ce-type").Data();
  }
  else if (!arg->GetRequestHeader("Content-Type").CompareTo("application/cloudevents+json") ||
           !arg->GetRequestHeader("content-type").CompareTo("application/cloudevents+json")) {
  }
  else {
    fIsValid = false;
    return;
  }
  std::string postDataString(static_cast<const char *>(arg->GetPostData()), arg->GetPostDataLength());
  json        j = json::parse(postDataString);

  fData = j.dump().c_str();
  if (j["datacontenttype"].is_null()) {
    fDataContentType = "application/json";
  }
  else {
    fDataContentType = j["datacontenttype"].get<std::string>();
    fData            = j["data"].get<std::string>();
  }

  if (fId.empty() && !j["id"].is_null()) fId = j["id"].get<std::string>();
  if (fSource.empty() && !j["source"].is_null()) fSource = j["source"].get<std::string>();
  if (fSpecVersion.empty() && !j["specversion"].is_null()) fSpecVersion = j["specversion"].get<std::string>();
  if (fType.empty() && !j["type"].is_null()) fType = j["type"].get<std::string>();

  if (fId.empty() || fSource.empty() || fSpecVersion.empty() || fType.empty()) {
    fIsValid = false;
  }
}
std::string CloudEvent::GetInfo() const
{

  return TString::Format("CloudEvent: id=%s source=%s specVersion=%s type=%s datacontenttype=%s data=%s", fId.c_str(),
                         fSource.c_str(), fSpecVersion.c_str(), fType.c_str(), fDataContentType.c_str(), fData.c_str())
      .Data();
}
void CloudEvent::Clear(Option_t * opt)
{
  fId.clear();
  fSource.clear();
  fSpecVersion.clear();
  fType.clear();
  fData.clear();
  fDataContentType.clear();
  fIsValid = false;
}
void CloudEvent::Print(Option_t * opt) const
{
  Printf("%s", GetInfo().c_str());
}
} // namespace NdmSpc
