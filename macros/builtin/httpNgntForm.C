#include <map>
#include <string>
#include <NGnHttpServer.h>

void httpNgntForm()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  handlers["form"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    if (method.find("GET") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {
      NLogInfo("/form POST httpIn=%s", httpIn.dump().c_str());
      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for form action";
    }
  };
}
