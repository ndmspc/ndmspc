#include "NMacroBuilder.h"

#include <algorithm>
#include <cctype>

#include "ndmspc/http/NHttpServer.h"   ///< gNdmspcMcpTools / NMcpToolInfo (each step's own recipe)
#include "ndmspc/http/NInstanceTree.h" ///< RenderTemplate

namespace Ndmspc {

namespace {

/// @brief The recipe a tool declares for one of its actions, or nullptr when it declares none.
const NMcpToolInfo * ToolInfo(const std::string & action)
{
  if (gNdmspcMcpTools == nullptr) return nullptr;
  const auto info = gNdmspcMcpTools->find(action);
  return info == gNdmspcMcpTools->end() ? nullptr : &info->second;
}

/// @brief A session's name as the identifier a macro's function has to be: ROOT names a macro after it.
std::string MacroIdentifier(const std::string & label)
{
  std::string out;
  for (const char c : label) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0) out.push_back(c);
    else if (!out.empty() && out.back() != '_') out.push_back('_');
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) return "combination";
  if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) out.insert(0, "c_");
  return out;
}

/// @brief Split a recipe into its lines (a step may be several).
std::vector<std::string> SplitLines(const std::string & text)
{
  std::vector<std::string> lines;
  std::size_t              pos = 0;
  while (pos <= text.size()) {
    const std::size_t end  = text.find('\n', pos);
    const bool        last = end == std::string::npos;
    lines.push_back(text.substr(pos, last ? std::string::npos : end - pos));
    if (last) break;
    pos = end + 1;
  }
  while (!lines.empty() && lines.back().empty()) lines.pop_back(); // a trailing newline adds no line
  return lines;
}

/// @brief One recipe as the macro's body, indented to sit inside the function.
void AppendBody(std::string & body, const std::string & recipe)
{
  for (const auto & line : SplitLines(recipe)) body += "  " + line + "\n";
}

} // namespace

NMacroDocument BuildCombinationMacro(const std::vector<NMacroStep> & steps, const std::string & sessionLabel)
{
  NMacroDocument doc;
  doc.name = MacroIdentifier(sessionLabel);

  std::vector<std::string> includes;
  std::string              body;

  for (const auto & step : steps) {
    const NMcpToolInfo * info = ToolInfo(step.action);
    if (info == nullptr || (info->macroLines.empty() && !info->macroSilent)) {
      // A step no tool describes in code is still a step: it is named for what it is, so a macro never
      // silently loses part of the combination it was written from. A step that says it writes nothing
      // (`macroSilent`) is not a gap - it is there for what it recorded (see `state`).
      AppendBody(body, "// " + step.action + " " + step.params.dump());
      continue;
    }
    for (const auto & line : info->macroLines) {
      AppendBody(body, RenderTemplate(line, step.params, NTemplateStyle::Cpp, step.state));
    }
    for (const auto & include : info->macroIncludes) {
      if (std::find(includes.begin(), includes.end(), include) == includes.end()) includes.push_back(include);
    }
  }
  std::sort(includes.begin(), includes.end());

  std::string out;
  out += "// " + (sessionLabel.empty() ? doc.name : sessionLabel) + ": the room's active combination\n";
  out += "// Written read-only - nothing in the room was changed.\n";
  if (!includes.empty()) {
    out += "\n";
    for (const auto & include : includes) out += "#include <" + include + ">\n";
  }
  out += "\nvoid " + doc.name + "()\n{\n" + body + "}\n";

  doc.text = std::move(out);
  return doc;
}

} // namespace Ndmspc
