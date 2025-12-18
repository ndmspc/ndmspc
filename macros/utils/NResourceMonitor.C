#include <NGnTree.h>
#include <NBinningDef.h>
#include <NGnNavigator.h>
#include <NLogger.h>
#include "THnSparse.h"
void NResourceMonitor(std::string filename, std::string binningName = "",
                      const std::string & ws = "ws://localhost:8080/ws/root.websocket")
{

  Ndmspc::NGnTree *     ngnt       = Ndmspc::NGnTree::Open(filename.c_str());
  Ndmspc::NBinningDef * binningDef = ngnt->GetBinning()->GetDefinition(binningName);
  TList *               outputs    = ngnt->GetOutput(binningName);

  THnSparse * hnsMesourceMonitor = (THnSparse *)outputs->FindObject("resource_monitor");
  if (hnsMesourceMonitor == nullptr) {
    NLogError("NResourceMonitor: THnSparse 'resource_monitor' not found in outputs for binning '%s' !!!",
              binningName.c_str());
    return;
  }

  hnsMesourceMonitor->Print();
  //
  TString           tmpFile  = "/tmp/NResourceMonitor_ngnt_tmp.root";
  Ndmspc::NGnTree * ngntStat = new Ndmspc::NGnTree(hnsMesourceMonitor, "stat", tmpFile.Data());
  ngntStat->Close(true);

  ngntStat = Ndmspc::NGnTree::Open(tmpFile.Data());

  Ndmspc::NGnNavigator * nav = ngntStat->Reshape("", {});
  if (nav == nullptr) {
    NLogError("NResourceMonitor: Failed to reshape navigator for resource monitor !!!");
    return;
  }
  // tmpFile.ReplaceAll(".root", ".json");
  // nav->Export(tmpFile.Data(), {}, ws);

  nav->Draw();

  ngnt->Close();
}
