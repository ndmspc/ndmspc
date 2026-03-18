#include <algorithm>
#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NUtils.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>

void httpNgntHnSparse()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  handlers["open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                        std::map<std::string, TObject *> &) {
    auto    server = Ndmspc::gNGnHttpServer;
    TFile * file   = (TFile *)server->GetInputObject("file");
    NLogDebug("Received HTTP request for 'open' action with method: %s httpIn=%s", method.c_str(),
              httpIn.dump().c_str());

    if (method.find("GET") != std::string::npos) {
      if (file && !file->IsZombie()) {
        httpOut["result"] = "success";
      }
      else {
        NLogDebug("NGnTree is not opened");
        httpOut["result"] = "not_opened";
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (httpIn.contains("file")) {
        std::string fileName = httpIn["file"].get<std::string>();
        NLogDebug("Opening file: %s", fileName.c_str());
        if (file && !file->IsZombie()) {

          if (fileName.compare(file->GetName()) == 0) {
            NLogDebug("File %s is already opened", file->GetName());
            httpOut["result"] = "success";
            // inputs["ngnt"] = ngnt;
            return;
          }
          server->RemoveInputObject("file");
        }
        file = TFile::Open(fileName.c_str());
        if (file) {
          NLogDebug("Successfully opened file: %s", file->GetName());
          httpOut["result"]                                               = "success";
          server->GetWorkspace()["open"]["type"]                          = "object";
          server->GetWorkspace()["open"]["properties"]["file"]["type"]    = "string";
          server->GetWorkspace()["open"]["properties"]["file"]["default"] = file->GetName();
          wsOut["workspace"]["open"]                                      = server->GetWorkspace()["open"];

          file->ls();

          if (server->GetWorkspace().contains("object") == false) {

            // Build reshape properties based on NGnTree binning (default values and options for levels and binning
            // definitions)
            json objectProperties;
            objectProperties["obj"]["type"]    = "string";
            objectProperties["obj"]["default"] = "phianalysis-t-hn-sparse/unlikepm";

            // wsOut["workspace"]["reshape"]["properties"] = reshapeProperties;
            server->GetWorkspace()["object"]["properties"] = objectProperties;
            server->GetWorkspace()["object"]["type"]       = "object";
          }
          wsOut["workspace"]["object"] = server->GetWorkspace()["object"];

          server->AddInputObject("file", file);
        }
        else {
          NLogError("Failed to open NGnTree from file: %s", fileName.c_str());
          httpOut["result"] = "failure";
        }
      }
      else {
        httpOut["error"]  = "Missing 'file' parameter for open action";
        httpOut["result"] = "failure";
      }
    }
    else if (method.find("DELETE") != std::string::npos) {

      if (file) {
        NLogDebug("Closing file %s", file->GetName());
        file->Close();
        server->RemoveInputObject("file");
      }
      else {
        NLogDebug("No file to close");
      }
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for open action";
    }
  };

  handlers["object"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    NLogInfo("/object method=%s in=%s", method.c_str(), httpIn.dump().c_str());
    if (method.find("GET") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {

      TFile * file = (TFile *)server->GetInputObject("file");
      if (!file || file->IsZombie()) {
        NLogError("No valid file opened for loading object");
        httpOut["error"]  = "No valid file opened for loading object";
        httpOut["result"] = "failure";
        return;
      }

      TObject * existingObj = server->GetInputObject("object");
      if (existingObj) {
        NLogDebug("Removing existing object from workspace before loading new one");
        server->RemoveInputObject("object");
      }
      std::string objectName = httpIn["obj"].get<std::string>();
      THnSparse * obj        = (THnSparse *)file->Get(objectName.c_str());
      if (obj) {
        server->AddInputObject("object", obj);
        NLogDebug("Successfully retrieved object: %s", obj->GetName());
        // server->GetWorkspace()["object"]["properties"]["obj"]["default"] = objectName->GetName();
        // wsOut["workspace"]["object"]                                     = server->GetWorkspace()["object"];

        if (server->GetWorkspace().contains("project") == false) {
          json                     axesInfo;
          std::vector<int>         axisIndices;
          std::vector<std::string> axisNames;
          for (int i = 0; i < obj->GetNdimensions(); ++i) {
            TAxis * axis = obj->GetAxis(i);
            axisIndices.push_back(i);
            axisNames.push_back(axis->GetName());
          }

          axesInfo["axes"]["type"]                      = "array";
          axesInfo["axes"]["format"]                    = "multiselect";
          axesInfo["axes"]["items"]["type"]             = "string";
          axesInfo["axes"]["items"]["enum"]             = axisIndices;
          axesInfo["axes"]["items"]["enumDescriptions"] = axisNames;
          if (!axisNames.empty()) {
            axesInfo["axes"]["default"] = json::array({axisIndices.front()});
          }
          else {
            axesInfo["axes"]["default"] = json::array();
          }

          server->GetWorkspace()["project"]["properties"] = axesInfo;
          server->GetWorkspace()["project"]["type"]       = "object";

          wsOut["workspace"]["project"] = server->GetWorkspace()["project"];
          NLogDebug("Set content properties in workspace: %s", server->GetWorkspace()["project"].dump().c_str());
        }
      }

      httpOut["result"] = "success";
      // httpOut["result"] = "skipped";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"]  = "Unsupported HTTP method for object action";
      httpOut["result"] = "failure";
    }
  };

  handlers["project"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                           std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    NLogInfo("/project method=%s in=%s", method.c_str(), httpIn.dump().c_str());
    if (method.find("GET") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {
      TObject * existingObj = server->GetInputObject("object");
      if (!existingObj) {
        NLogError("No object found in workspace for projection");
        httpOut["error"]  = "No object found in workspace for projection";
        httpOut["result"] = "failure";
        return;
      }
      THnSparse * obj = dynamic_cast<THnSparse *>(existingObj);
      if (!obj) {
        NLogError("Object in workspace is not a THnSparse for projection");
        httpOut["error"]  = "Object in workspace is not a THnSparse for projection";
        httpOut["result"] = "failure";
        return;
      }
      std::vector<int> axesToProject;
      if (httpIn.contains("axes")) {
        NLogDebug("Received axes for projection: %s", httpIn["axes"].dump().c_str());
        for (const auto & axis : httpIn["axes"].get<std::vector<int>>()) {
          axesToProject.push_back(axis);
        }

        NLogDebug("Projecting THnSparse with axes: %s", json(axesToProject).dump().c_str());
        // Get TH1, TH2, or TH3 projection based on number of axes
        TH1 * proj = nullptr;
        if (axesToProject.size() == 1) {
          proj = obj->Projection(axesToProject[0]);
        }
        else if (axesToProject.size() == 2) {
          proj = obj->Projection(axesToProject[0], axesToProject[1]);
        }
        else if (axesToProject.size() == 3) {
          proj = obj->Projection(axesToProject[0], axesToProject[1], axesToProject[2]);
        }
        else {
          NLogError("Unsupported number of axes for projection: %zu", axesToProject.size());
          httpOut["error"]  = "Unsupported number of axes for projection";
          httpOut["result"] = "failure";
          return;
        }

        proj->Print();
        TList * outputList = new TList();
        outputList->Add(proj);
        TString projJsonStr                      = TBufferJSON::ConvertToJSON(outputList);
        wsOut["payload"]["spectra"]["obj"]       = json::parse(projJsonStr.Data());
        wsOut["payload"]["spectra"]["targetPad"] = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad1";
      }
      httpOut["result"] = "success";
      // httpOut["result"] = "skipped";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"]  = "Unsupported HTTP method for project action";
      httpOut["result"] = "failure";
    }
  };
}
