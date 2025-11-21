#ifndef NdmspcHttpNCloudEvent_H
#define NdmspcHttpNCloudEvent_H

#include <TObject.h>
#include <nlohmann/json.hpp>
#include <string>
using json = nlohmann::json;

class THttpCallArg;
namespace Ndmspc {

/**
 * @class NCloudEvent
 * @brief Represents a CloudEvent object for HTTP communication.
 *
 * NCloudEvent encapsulates the CloudEvent specification fields and provides
 * methods for manipulating and accessing event data. It supports construction
 * from explicit parameters or from an HTTP call argument, and offers utilities
 * for validation, printing, and event handling.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NCloudEvent : public TObject {
  public:
  /**
   * @brief Constructs a new NCloudEvent with specified parameters.
   * @param id Event identifier (default: "0").
   * @param source Event source URI (default: "unknown").
   * @param specVersion CloudEvent specification version (default: "1.0").
   * @param type Event type (default: "unknown").
   */
  NCloudEvent(std::string id = "0", std::string source = "unknown", std::string specVersion = "1.0",
              std::string type = "unknown");

  /**
   * @brief Constructs a new NCloudEvent from an HTTP call argument.
   * @param arg Pointer to THttpCallArg containing event data.
   */
  NCloudEvent(THttpCallArg * arg);

  /**
   * @brief Destroys the NCloudEvent instance.
   */
  virtual ~NCloudEvent();

  /**
   * @brief Clears the event data.
   * @param opt Optional parameter for clear operation.
   */
  virtual void Clear(Option_t * opt = "");

  /**
   * @brief Prints the event details.
   * @param opt Optional parameter for print operation.
   */
  virtual void Print(Option_t * opt = "") const;

  /**
   * @brief Checks if the event is valid.
   * @return True if valid, false otherwise.
   */
  bool IsValid() const { return fIsValid; }

  /**
   * @brief Gets the event identifier.
   * @return Event ID string.
   */
  std::string GetId() const { return fId; }

  /**
   * @brief Gets the event source URI.
   * @return Source URI string.
   */
  std::string GetSource() const { return fSource; }

  /**
   * @brief Gets the CloudEvent specification version.
   * @return Specification version string.
   */
  std::string GetSpecVersion() const { return fSpecVersion; }

  /**
   * @brief Gets the event type.
   * @return Event type string.
   */
  std::string GetType() const { return fType; }

  /**
   * @brief Gets the event data content type.
   * @return Data content type string.
   */
  std::string GetDatacontentType() const { return fDataContentType; }

  /**
   * @brief Gets the event data payload.
   * @return Data payload string.
   */
  std::string GetData() const { return fData; }

  /**
   * @brief Gets additional event information.
   * @return Info string.
   */
  std::string GetInfo() const;

  /**
   * @brief Handles an HTTP request for a CloudEvent.
   * @param arg Pointer to THttpCallArg containing request data.
   */
  void HandleNCloudEventRequest(THttpCallArg * arg);

  /**
   * @brief Sets the event identifier.
   * @param id Event ID string.
   */
  void SetId(std::string id) { fId = id; }

  /**
   * @brief Sets the event source URI.
   * @param source Source URI string.
   */
  void SetSource(std::string source) { fSource = source; }

  /**
   * @brief Sets the CloudEvent specification version.
   * @param specVersion Specification version string.
   */
  void SetSpecVersion(std::string specVersion) { fSpecVersion = specVersion; }

  /**
   * @brief Sets the event data content type.
   * @param datacontenttype Data content type string.
   */
  void SetDatacontentType(std::string datacontenttype) { fDataContentType = datacontenttype; }

  /**
   * @brief Sets the event type.
   * @param type Event type string.
   */
  void SetType(std::string type) { fType = type; }

  /**
   * @brief Sets the event data payload.
   * @param data Data payload string.
   */
  void SetData(std::string data) { fData = data; }

  private:
  bool        fIsValid{false};    ///< Validity flag for the event
  std::string fId{};              ///< Event identifier
  std::string fSource{};          ///< Event source URI-reference
  std::string fSpecVersion{};     ///< CloudEvent specification version
  std::string fType{};            ///< Event type
  std::string fDataContentType{}; ///< Event data content type
  std::string fData{};            ///< Event data payload

  /// \cond CLASSIMP
  ClassDef(NCloudEvent, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
