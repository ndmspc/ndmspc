#ifndef Ndmspc_NBaseActions_H
#define Ndmspc_NBaseActions_H

namespace Ndmspc {

/**
 * @brief Registers the server's own base actions, and their MCP metadata, into the handler map.
 *
 * The base actions (`health`, `state`) describe and drive the server itself - its workspace, its
 * inspector schema, its heartbeat - so they are framework code, alongside the room actions
 * NRoomRouter registers, rather than a ROOT macro a deployment has to remember to load.
 *
 * They are registered into `gNdmspcHttpHandlers` (and their metadata into `gNdmspcMcpTools`), which
 * the CLI wires up before this is called, so the actions are served both as `/api/<action>` and as
 * MCP tools. Call it before loading the macros: a macro that defines one of these names then
 * overrides it, which is what a deployment extending `state` would expect.
 *
 * A server started with rooms (`ndmspc-server --rooms`) does not call this at all: the room router
 * serves the room actions and nothing else (see NRoomRouter), so there is no tool to call on the
 * entry.
 *
 * @return kTRUE when the actions were registered; kFALSE when there is no handler map to register
 *         into (gNdmspcHttpHandlers is null - a process that never wired a server).
 */
bool RegisterBaseActions();

} // namespace Ndmspc

#endif
