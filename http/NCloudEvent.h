#ifndef NdmspcHttpNCloudEvent_H
#define NdmspcHttpNCloudEvent_H

#include <TObject.h>
#include <nlohmann/json.hpp>
#include <string>
using json = nlohmann::json;

class THttpCallArg;
namespace Ndmspc {

///
/// \class NCloudEvent
///
/// \brief NCloudEvent object
///	\author Martin Vala <mvala@cern.ch>
///
///
class NCloudEvent : public TObject {
  public:
  NCloudEvent(std::string id = "0", std::string source = "unknown", std::string specVersion = "1.0",
             std::string type = "unknown");
  NCloudEvent(THttpCallArg * arg);
  virtual ~NCloudEvent();

  virtual void Clear(Option_t * opt = "");
  virtual void Print(Option_t * opt = "") const;
  bool         IsValid() const { return fIsValid; }

  std::string GetId() const { return fId; }
  std::string GetSource() const { return fSource; }
  std::string GetSpecVersion() const { return fSpecVersion; }
  std::string GetType() const { return fType; }
  std::string GetDatacontentType() const { return fDataContentType; }
  std::string GetData() const { return fData; }
  std::string GetInfo() const;

  void HandleNCloudEventRequest(THttpCallArg * arg);
  void SetId(std::string id) { fId = id; }
  void SetSource(std::string source) { fSource = source; }
  void SetSpecVersion(std::string specVersion) { fSpecVersion = specVersion; }
  void SetDatacontentType(std::string datacontenttype) { fDataContentType = datacontenttype; }
  void SetType(std::string type) { fType = type; }
  void SetData(std::string data) { fData = data; }

  private:
  bool        fIsValid{false};
  std::string fId{};
  std::string fSource{}; // URI-reference
  std::string fSpecVersion{};
  std::string fType{};
  std::string fDataContentType{};

  std::string fData{};

  /// \cond CLASSIMP
  ClassDef(NCloudEvent, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
