#include <TAxis.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

inline int SetResultValueError(json cfg, THnSparse * finalResults, std::string name, Int_t * point, double val,
                               double err, bool normalizeToWidth = false, bool onlyPositive = false, double times = 1)
{

  bool isValNan = TMath::IsNaN(val);
  bool isErrNaN = TMath::IsNaN(err);

  if (isValNan || isErrNaN) {
    Printf("Error: SetResultValueError %s val=%f[isNaN=%d] err=%f[isNan=%d]", name.c_str(), val, isValNan, err,
           isErrNaN);
    return -2;
  }

  if (onlyPositive && val < 0) {
    return -3;
  }

  if (times > 0 && times * std::abs(val) < err) {
    Printf("Warning: Skipping '%s' because 'times * val < err' (  %f * %f < %f ) ...", name.c_str(), times,
           std::abs(val), err);
    return -4;
  }

  if (normalizeToWidth) {
    int nDimsCuts = 0;
    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      nDimsCuts++;
    }
    // Printf("1 %f %f", val, err);
    for (int iCut = 0; iCut < nDimsCuts; iCut++) {
      double w = finalResults->GetAxis(iCut + 1)->GetBinWidth(point[iCut + 1]);
      val /= w;
      err /= w;
      // Printf("%d %f %f w=%f", iCut + 1, val, err, w);
    }
  }

  int idx = finalResults->GetAxis(0)->FindBin(name.c_str());
  if (idx <= 0) {
    return idx;
  }
  point[0]       = idx;
  Long64_t binId = finalResults->GetBin(point);
  finalResults->SetBinContent(binId, val);
  finalResults->SetBinError(binId, err);

  return idx;
}
