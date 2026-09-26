// Deprecated: the actions this macro used to define are built into the server.
//
// `health` and `state` are framework code now (Ndmspc::RegisterBaseActions, which every server
// registers when it starts), so nothing has to load this macro - and `debug`, which this macro also
// carried, is gone (the `/api/debug` echo helper is no longer served).
//
// It is kept, and kept working, because deployments and examples name it in their `-m` lists (the
// image CMD, etc/systemd/http.ngnt, http/examples/mcp/...): a list that still names it gets these
// actions exactly as before. New lists should not name it - name only the tools wanted.
//
// Usage: ndmspc-server -m "<dir>/toolNgnt.C"

#include <ndmspc/http/NBaseActions.h>

void toolBase()
{
  Ndmspc::RegisterBaseActions();
}
