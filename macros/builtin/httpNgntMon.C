#include <NGnHttpServer.h>
#include <NMonJobManager.h>
#include <NMonJob.h>

void httpNgntMon()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // Store lambdas (must be non-capturing to convert to function pointer)
  handlers["jobs"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                        std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    Ndmspc::NMonJobManager * jobManager = (Ndmspc::NMonJobManager *)server->GetInputObject("_jobManager");
    if (!jobManager) {
      jobManager = new Ndmspc::NMonJobManager();
      server->AddInputObject("_jobManager", jobManager);
    }

    if (method.find("GET") != std::string::npos) {
      httpOut["result"]  = "success";
      httpOut["jobList"] = jobManager->GetString();
      jobManager->Print();
      wsOut["nJobs"]     = jobManager->getfJobs().size();
      wsOut["jobList"]   = jobManager->GetString();
      httpOut["message"] = "Get action is not implemented for jobs, but acknowledged.";
    }
    else if (method.find("PATCH") != std::string::npos) {
      // wsOut["health"] = "ok";
      json j = httpIn;
      if (jobManager->UpdateTask(j["name"], j["task"], j["action"])) {

        httpOut["result"]  = "succes";
        httpOut["jobList"] = jobManager->GetString();
      }
      else {
        httpOut["result"] = "failure";
      }
      httpOut["message"] = "Update action is not implemented for jobs, but acknowledged.";
    }
    else if (method.find("POST") != std::string::npos) {
      // wsOut["health"] = "ok";
      // httpOut["result"]  = "skipped";
      // httpOut["result"] = "success";

      Ndmspc::NMonJob * job = new Ndmspc::NMonJob(); // fixme need to destroy job latter to prevent memory leak
      if (job->ParseMessage(httpIn.dump())) {
        jobManager->AddJob(job);
        httpOut["result"]  = "success";
        httpOut["jobList"] = jobManager->GetString();
      }
      else {
        httpOut["result"] = "failure";
      }
      httpOut["message"] = "Post action is not implemented for jobs, but acknowledged.";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"]  = "success";
      httpOut["message"] = "Delete action is not implemented for jobs, but acknowledged.";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for test action";
    }
  };
}
