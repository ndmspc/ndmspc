#ifndef Ndmspc_NMacroBuilder_H
#define Ndmspc_NMacroBuilder_H

#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h" ///< provides the global `json` (nlohmann) alias

namespace Ndmspc {

/**
 * @struct NMacroStep
 * @brief One step of a combination, as the tree holds it: what was run, with what, and what it recorded.
 */
struct NMacroStep {
  std::string action; ///< The handler key (e.g. "ndmspc/ngnt/reshape")
  json        params; ///< The node's own arguments
  /**
   * What the step recorded for itself (its node's `state`), for a line whose recipe reads
   * `{{ state.<key> }}` - the drill a step is on, the object a browser step picked. A form never sends this,
   * which is why it is apart from the arguments.
   */
  json        state{};
};

/**
 * @struct NMacroDocument
 * @brief A combination written out as a runnable ROOT macro.
 */
struct NMacroDocument {
  std::string name; ///< The function the macro defines ("ngnt_1"), and so the file ("ngnt_1.C")
  std::string text; ///< The macro's source
};

/**
 * @brief Write a combination out as a ROOT macro its user can run locally.
 *
 * What each step *is* in C++ is declared by the tool that owns it (`NMcpToolInfo::macroLines`, one entry per
 * line, each the same `{{ arg }}` template `label` uses, filled from the step's own arguments), so this needs
 * no list of tool names and a new tool is exported by saying what its step makes. A step whose action
 * declares nothing is written as a comment naming it, and never dropped silently.
 *
 * The result is the steps and nothing else: the `#include`s their tools asked for, then one line per
 * step in the order the combination runs them. What was on screen - the pads, a tab, a drill-down -
 * is the viewer's, not the analysis', and is not reproduced.
 *
 * @param steps The combination's steps, root first - what the tree holds for the path (see
 *        `NInstanceTree::Path`, `Action` and `Params`).
 * @param sessionLabel What the session is called ("ngnt 1"), for the macro's name and its first line;
 *        a name that is not an identifier is reduced to one.
 * @return The macro's source, and the identifier both its function and its file go by.
 */
NMacroDocument BuildCombinationMacro(const std::vector<NMacroStep> & steps, const std::string & sessionLabel);

} // namespace Ndmspc
#endif
