#include "WebSocketHandler.h"
#include "THttpCallArg.h"
#include "TDatime.h"
#include "TTimer.h"

namespace Ndmspc {
WebSocketHandler::WebSocketHandler(const char * name, const char * title) : THttpWSHandler(name, title) {}
WebSocketHandler::~WebSocketHandler() {}

Bool_t WebSocketHandler::ProcessWS(THttpCallArg * arg)
{
  if (!arg || (arg->GetWSId() == 0)) return kTRUE;

  // printf("Method %s\n", arg->GetMethod());

  if (arg->IsMethod("WS_CONNECT")) {
    // accept only if connection not established
    return fWSId == 0;
  }

  if (arg->IsMethod("WS_READY")) {
    fWSId = arg->GetWSId();
    printf("Client connected %d\n", fWSId);
    return kTRUE;
  }

  if (arg->IsMethod("WS_CLOSE")) {
    fWSId = 0;
    printf("Client disconnected\n");
    return kTRUE;
  }

  if (arg->IsMethod("WS_DATA")) {
    TString str;
    str.Append((const char *)arg->GetPostData(), arg->GetPostDataLength());
    printf("Client msg: %s\n", str.Data());
    TDatime now;
    SendCharStarWS(arg->GetWSId(), TString::Format("Server replies:%s server counter:%d", now.AsString(), fServCnt++));
    return kTRUE;
  }

  return kFALSE;
}

/// per timeout sends data portion to the client
Bool_t WebSocketHandler::HandleTimer(TTimer *)
{
  TDatime now;
  if (fWSId)
    SendCharStarWS(fWSId, TString::Format("Server sends data:%s server counter:%d", now.AsString(), fServCnt++));
  return kTRUE;
}

} // namespace Ndmspc
