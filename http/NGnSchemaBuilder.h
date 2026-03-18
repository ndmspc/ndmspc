#ifndef Ndmspc_NGnSchemaBuilder_H
#define Ndmspc_NGnSchemaBuilder_H

#include "NUtils.h"
#include <string>
#include <vector>

namespace Ndmspc {

///
/// \class NGnSchemaBuilder
/// \brief Fluent builder for JSON Schema objects used in workspace definitions.
///
/// Example:
/// \code
///   auto schema = NGnSchemaBuilder()
///     .Hint("Axes: [0] pt  [1] eta")
///     .String("file").Default("data.root")
///     .Number("margin").Default(1.0)
///     .Select("mode", {"V","VE","D"}).Default("V")
///     .MultiSelect("parameters", paramNames).Default(json::array({paramNames.front()}))
///     .Array("levels").Items("array").ItemItems("integer").Default(defaultLevels)
///     .Build();
/// \endcode
///
class NGnSchemaBuilder {

public:
  explicit NGnSchemaBuilder(const std::string & type = "object") { fSchema["type"] = type; }

  NGnSchemaBuilder & Hint(const std::string & hint)
  {
    fSchema["hint"] = hint;
    return *this;
  }

  // --- Property initiators (each returns *this for chaining) ---

  NGnSchemaBuilder & String(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "string";
    return *this;
  }

  NGnSchemaBuilder & Number(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "number";
    return *this;
  }

  NGnSchemaBuilder & Integer(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "integer";
    return *this;
  }

  NGnSchemaBuilder & Array(const std::string & name)
  {
    fCurrentProp                        = name;
    fSchema["properties"][name]["type"] = "array";
    return *this;
  }

  NGnSchemaBuilder & Select(const std::string & name, const std::vector<std::string> & options)
  {
    fCurrentProp                          = name;
    fSchema["properties"][name]["type"]   = "string";
    fSchema["properties"][name]["format"] = "select";
    fSchema["properties"][name]["enum"]   = options;
    return *this;
  }

  NGnSchemaBuilder & MultiSelect(const std::string & name, const std::vector<std::string> & options)
  {
    fCurrentProp                                       = name;
    fSchema["properties"][name]["type"]                = "array";
    fSchema["properties"][name]["format"]              = "multiselect";
    fSchema["properties"][name]["items"]["type"]        = "string";
    fSchema["properties"][name]["items"]["enum"]        = options;
    return *this;
  }

  // --- Modifiers for the current property ---

  NGnSchemaBuilder & Default(const json & val)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["default"] = val;
    return *this;
  }

  NGnSchemaBuilder & Format(const std::string & fmt)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["format"] = fmt;
    return *this;
  }

  NGnSchemaBuilder & Description(const std::string & desc)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["description"] = desc;
    return *this;
  }

  NGnSchemaBuilder & Enum(const std::vector<std::string> & values)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["enum"] = values;
    return *this;
  }

  NGnSchemaBuilder & Items(const std::string & type)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["type"] = type;
    return *this;
  }

  NGnSchemaBuilder & ItemItems(const std::string & type)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["items"]["type"] = type;
    return *this;
  }

  NGnSchemaBuilder & ItemsEnum(const std::vector<std::string> & values)
  {
    if (!fCurrentProp.empty()) fSchema["properties"][fCurrentProp]["items"]["enum"] = values;
    return *this;
  }

  // --- Build the final schema ---
  json Build() const { return fSchema; }

  // --- Static helpers for post-build updates ---

  /// Update one property default in an existing schema.
  static void SetDefault(json & schema, const std::string & prop, const json & val)
  {
    schema["properties"][prop]["default"] = val;
  }

private:
  json        fSchema;
  std::string fCurrentProp;
};

} // namespace Ndmspc
#endif
