#ifndef Ndmspc_NGnSchemaBuilder_H
#define Ndmspc_NGnSchemaBuilder_H

#include <string>
#include <vector>
#include "ndmspc/core/NUtils.h"

namespace Ndmspc {

///
/// \class NSchemaBuilder
/// \brief Fluent builder for JSON Schema objects used in workspace definitions.
///
/// Example:
/// \code
///   auto schema = NSchemaBuilder()
///     .Hint("Axes: [0] pt  [1] eta")
///     .String("file").Default("data.root")
///     .Number("margin").Default(1.0)
///     .Boolean("averages").Default(true)
///     .Select("mode", {"V","VE","D"}).Default("V")
///     .MultiSelect("parameters", paramNames).Default(json::array({paramNames.front()}))
///     .Array("levels").Items("array").ItemItems("integer").Default(defaultLevels)
///     .Build();
/// \endcode
///
class NSchemaBuilder {

public:
  /**
   * @brief Constructor.
   * @param type JSON Schema root type (default: "object").
   */
  explicit NSchemaBuilder(const std::string & type = "object") { fSchema["type"] = type; }

  /**
   * @brief Set the schema "hint" string.
   * @param hint Hint text shown to UI clients.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Hint(const std::string & hint)
  {
    fSchema["hint"] = hint;
    return *this;
  }

  // --- Property initiators (each returns *this for chaining) ---

  /**
   * @brief Start a string property.
   * @param name Property name.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & String(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "string";
    return *this;
  }

  /**
   * @brief Start a number property.
   * @param name Property name.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Number(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "number";
    return *this;
  }

  /**
   * @brief Start an integer property.
   * @param name Property name.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Integer(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "integer";
    return *this;
  }

  /**
   * @brief Start a boolean property.
   * @param name Property name.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Boolean(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "boolean";
    return *this;
  }

  /**
   * @brief Start an array property.
   * @param name Property name.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Array(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "array";
    return *this;
  }

  /**
   * @brief Start a single-select string property with a "select" format.
   * @param name Property name.
   * @param options Allowed values.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Select(const std::string & name, const std::vector<std::string> & options)
  {
    fCurrentProp                          = name;
    fSchema["properties"][name]["type"]   = "string";
    fSchema["properties"][name]["format"] = "select";
    fSchema["properties"][name]["enum"]   = options;
    return *this;
  }

  /**
   * @brief Start a multi-select array-of-strings property with a "multiselect" format.
   * @param name Property name.
   * @param options Allowed values.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & MultiSelect(const std::string & name, const std::vector<std::string> & options)
  {
    fCurrentProp                                       = name;
    fSchema["properties"][name]["type"]                = "array";
    fSchema["properties"][name]["format"]              = "multiselect";
    fSchema["properties"][name]["items"]["type"]        = "string";
    fSchema["properties"][name]["items"]["enum"]        = options;
    return *this;
  }

  // --- Modifiers for the current property ---

  /**
   * @brief Set the "default" of the current property.
   * @param val Default value.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Default(const json & val)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["default"] = val;
    return *this;
  }

  /**
   * @brief Set the "format" of the current property.
   * @param fmt Format string.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Format(const std::string & fmt)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["format"] = fmt;
    return *this;
  }

  /**
   * @brief Set the "description" of the current property.
   * @param desc Description string.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Description(const std::string & desc)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["description"] = desc;
    return *this;
  }

  /**
   * @brief Set the "enum" of the current property.
   * @param values Allowed values.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Enum(const std::vector<std::string> & values)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["enum"] = values;
    return *this;
  }

  /**
   * @brief Set the item "type" of the current array property.
   * @param type Item type.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & Items(const std::string & type)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["type"] = type;
    return *this;
  }

  /**
   * @brief Set the nested item "type" of the current array-of-arrays property.
   * @param type Nested item type.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & ItemItems(const std::string & type)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["items"]["type"] = type;
    return *this;
  }

  /**
   * @brief Set the item "enum" of the current array property.
   * @param values Allowed item values.
   * @return Reference to this builder for chaining.
   */
  NSchemaBuilder & ItemsEnum(const std::vector<std::string> & values)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["enum"] = values;
    return *this;
  }

  // --- Build the final schema ---
  /// @brief Get the built JSON Schema.
  /// @return The schema object.
  json Build() const { return fSchema; }

  // --- Static helpers for post-build updates ---

  /// Set the `default` of a property that the schema already declares.
  ///
  /// A property missing from the schema means the caller forgot to build it
  /// first. Writing the default anyway would create a stub carrying only
  /// `default` and silently losing its `type`, which breaks the clients that
  /// render the schema. Warn and leave the schema untouched instead.
  static void SetDefault(json & schema, const std::string & prop, const json & val)
  {
    if (!schema.contains("properties") || !schema["properties"].contains(prop)) {
      NLogWarning("NSchemaBuilder::SetDefault: property '%s' is not declared in the schema, ignoring default",
                  prop.c_str());
      return;
    }
    schema["properties"][prop]["default"] = val;
  }

private:
  json        fSchema;      ///< Schema under construction
  std::string fCurrentProp; ///< Property modified by the modifier methods
};

} // namespace Ndmspc
#endif
