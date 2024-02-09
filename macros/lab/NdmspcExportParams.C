#include <TString.h>
#include <TAxis.h>
#include <TFile.h>
#include <TKey.h>
#include <TH1D.h>
#include <TGraphErrors.h>
#include <TF1.h>
#include <THStack.h>
void FillEntries(std::vector<Int_t> &entries, TAxis *a, std::vector<Int_t> filter)
{

  if (a == nullptr && filter.empty())
  {
    entries.push_back(0);
    return;
  }

  if (!filter.empty())
  {
    entries = filter;
  }
  else
  {
    for (Int_t iBin = 1; iBin <= a->GetNbins(); iBin++)
    {
      entries.push_back(iBin);
    }
  }
}

std::vector<Int_t> ParseListOfBins(const char *str, char token = ',')
{
  std::vector<Int_t> result;
  do
  {
    const char *begin = str;

    while (*str != token && *str)
      str++;
    std::string s = std::string(begin, str);
    if (!s.empty())
      result.push_back(atoi(s.c_str()));
  } while (0 != *str++);

  return result;
}

Int_t NdmspcExportParams(Int_t idGraphAxis = 0,
                         Int_t idStackAxis = 1,
                         Int_t idCanvasAxis = -1,
                         const char *graphFilterStr = "",
                         const char *stackFilterStr = "",
                         const char *canvasFilterStr = "",
                         TString input = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/demo/phi/pt_mult/hProjMap.root",
                         TString binsDir = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/demo/phi/pt_mult/bins",
                         TString paramHistogram = "peak",
                         TString outputFileNameTmpl = "/tmp/out_%d.root",
                         TString mapStr = "hProjMap",
                         TString contentFileName = "content.root",
                         const char *filterParseToken = ",",
                         TString fileOpt = "?remote=1")
{

  if (outputFileNameTmpl.BeginsWith("root://"))
    outputFileNameTmpl += fileOpt;
  TH1::AddDirectory(kFALSE);

  // if ((idGraphAxis == idStackAxis) || (idGraphAxis == idCanvasAxis) || (idStackAxis == idCanvasAxis))
  // {
  //   Printf("Some id axis are equal!!! Stoping !!!");
  //   return 1;
  // }

  std::vector<Int_t> graphFilter = ParseListOfBins(graphFilterStr, filterParseToken[0]);
  std::vector<Int_t> stackFilter = ParseListOfBins(stackFilterStr, filterParseToken[0]);
  std::vector<Int_t> canvasFilter = ParseListOfBins(canvasFilterStr, filterParseToken[0]);

  std::vector<Int_t> graphEntries;
  std::vector<Int_t> stackEntries;
  std::vector<Int_t> canvasEntries;

  TAxis *aGraph = nullptr;
  TAxis *aStack = nullptr;
  TAxis *aCanvas = nullptr;

  TFile *fNdmspcInput = TFile::Open(TString::Format("%s%s", input.Data(), fileOpt.Data()).Data(), "READ");
  if (!fNdmspcInput)
    return 1;

  TKey *k = fNdmspcInput->GetKey(mapStr.Data());
  if (!k)
    return 2;
  TString className = k->GetClassName();
  // // k->Print();

  TH1 *hMap = (TH1 *)fNdmspcInput->Get(mapStr.Data());
  if (!hMap)
    return 3;

  TAxis *a = nullptr;
  if (className.BeginsWith("TH1"))
  {
    if (idGraphAxis == 0)
      aGraph = hMap->GetXaxis();
    else
    {
      Printf("idGraphAxis out of range dimensions");
      return 4;
    }

    idStackAxis = -1;
    idCanvasAxis = -1;
  }
  else if (className.BeginsWith("TH2"))
  {
    if (idGraphAxis == 0)
      aGraph = hMap->GetXaxis();
    else if (idGraphAxis == 1)
      aGraph = hMap->GetYaxis();
    else
    {
      Printf("idGraphAxis out of range dimensions");
      return 4;
    }
    if (idStackAxis >= 0)
    {

      if (idStackAxis == 0)
        aStack = hMap->GetXaxis();
      else if (idStackAxis == 1)
        aStack = hMap->GetYaxis();
      else
      {
        Printf("idStackAxis out of range dimensions");
        return 4;
      }
    }

    idCanvasAxis = -1;
  }

  else if (className.BeginsWith("TH3"))
  {
    if (idGraphAxis == 0)
      aGraph = hMap->GetXaxis();
    else if (idGraphAxis == 1)
      aGraph = hMap->GetYaxis();
    else if (idGraphAxis == 2)
      aGraph = hMap->GetYaxis();
    else
    {
      Printf("idGraphAxis out of range dimensions");
      return 4;
    }
    if (idStackAxis >= 0)
    {
      if (idStackAxis == 0)
        aStack = hMap->GetXaxis();
      else if (idStackAxis == 1)
        aStack = hMap->GetYaxis();
      else if (idStackAxis == 2)
        aStack = hMap->GetYaxis();
      else
      {
        Printf("idStackAxis out of range dimensions");
        return 4;
      }
    }

    if (idCanvasAxis >= 0)
    {

      if (idCanvasAxis == 0)
        aCanvas = hMap->GetXaxis();
      else if (idCanvasAxis == 1)
        aCanvas = hMap->GetYaxis();
      else if (idCanvasAxis == 2)
        aCanvas = hMap->GetYaxis();
      else
      {
        Printf("idCanvasAxis out of range dimensions");
        return 4;
      }
    }
  }

  else
  {
    Printf("Unsupported type '%s' !!!", className.Data());
  }

  FillEntries(graphEntries, aGraph, graphFilter);
  FillEntries(stackEntries, aStack, stackFilter);
  FillEntries(canvasEntries, aCanvas, canvasFilter);

  TList *output = nullptr;
  TFile *fCurrent = nullptr;
  TH1D *h = nullptr;
  THStack *hs = nullptr;
  Int_t x[3];

  for (auto idCanvas : canvasEntries)
  {

    TFile *fOut = TFile::Open(TString::Format(outputFileNameTmpl.Data(), idCanvas).Data(), "RECREATE");
    if (!fOut)
      return 14;
    Int_t color = kBlack;
    for (auto idStack : stackEntries)
    {
      TString cutsTitle, cutsName;
      if (aStack) {
        cutsName = TString::Format("%s[%.2f,%.2f]", aStack->GetName(), aStack->GetBinLowEdge(idStack), aStack->GetBinUpEdge(idStack));
        cutsTitle = TString::Format("%s [%.2f,%.2f]", aStack->GetName(), aStack->GetBinLowEdge(idStack), aStack->GetBinUpEdge(idStack));
      }
      // Printf("%s ", cutsTitle.Data());

      Int_t iBin = 1;
      for (auto idGraph : graphEntries)
      {
        std::vector<Int_t> point = {0, 0, 0};
        point[idGraphAxis] = idGraph;
        point[idStackAxis] = idStack;
        point[idCanvasAxis] = idCanvas;
        // Printf("%d %d %d -> %d %d %d", idGraphAxis, idStackAxis, idCanvasAxis, point[idGraphAxis], point[idStackAxis], point[idCanvasAxis]);

        // Printf("%d/%d %f %f", iBin, cutValue, a->GetBinCenter(iBin), a->GetBinWidth(iBin));
        TString fCurrentFilename = TString::Format("%s/%d/%s", binsDir.Data(), point[0], contentFileName.Data());
        if (idStackAxis >= 0)
          fCurrentFilename = TString::Format("%s/%d/%d/%s", binsDir.Data(), point[0], point[1], contentFileName.Data());
        if (idCanvasAxis >= 0)
          fCurrentFilename = TString::Format("%s/%d/%d/%d/%s", binsDir.Data(), point[0], point[1], point[2], contentFileName.Data());

        Printf("%s", fCurrentFilename.Data());

        fCurrent = TFile::Open(fCurrentFilename.Data(), "READ");
        if (!fCurrent)
          continue;

        TH1 *hParam = (TH1 *)fCurrent->Get(paramHistogram.Data());

        if (hParam)
        {
          TList *functions = hParam->GetListOfFunctions();
          if (output == nullptr)
          {
            output = new TList();

            for (Int_t iFun = 0; iFun < functions->GetEntries(); iFun++)
            {
              TF1 *fun = (TF1 *)functions->At(iFun);

              Double_t *params = fun->GetParameters();
              const Double_t *paramsErr = fun->GetParErrors();
              for (Int_t iPar = 0; iPar < fun->GetNpar(); iPar++)
              {

                hs = new THStack(TString::Format("%s_%s", fun->GetName(), fun->GetParName(iPar)).Data(), TString::Format("%s (%s) %s", fun->GetName(), fun->GetParName(iPar), cutsTitle.Data()).Data());
                hs->SetDrawOption("E1");
                h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), fun->GetParName(iPar), cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
                // h->SetFillColor(idStack+1);
                h->SetMarkerStyle(21);
                h->SetMarkerColor(color);
                h->SetOption("E1");
                h->SetBinContent(point[idGraphAxis], params[iPar]);
                h->SetBinError(point[idGraphAxis], paramsErr[iPar]);
                Printf("1 %s %d %d %d -> %d %d %d v=%f e=%f e2=%f", hs->GetTitle(), idGraphAxis, idStackAxis, idCanvasAxis, point[idGraphAxis], point[idStackAxis], point[idCanvasAxis], params[iPar], paramsErr[iPar], h->GetBinError(point[idGraphAxis]));
                hs->Add(h);
                output->Add(hs);
              }

              hs = new THStack(TString::Format("%s_%s", fun->GetName(), "chi2").Data(), TString::Format("%s (%s) %s", fun->GetName(), "chi2", cutsTitle.Data()).Data());
              hs->SetDrawOption("E1");
              h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), "chi2", cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
              // h->SetFillColor(idStack+1);
              h->SetMarkerStyle(21);
              h->SetMarkerColor(color);
              h->SetOption("E1");
              h->SetBinContent(point[idGraphAxis], fun->GetChisquare());
              h->SetBinError(point[idGraphAxis], 0.001);
              // Printf("1 %s %d %d %d -> %d %d %d", h->GetTitle(), idGraphAxis, idStackAxis, idCanvasAxis, point[idGraphAxis], point[idStackAxis], point[idCanvasAxis]);
              hs->Add(h);
              output->Add(hs);

              hs = new THStack(TString::Format("%s_%s", fun->GetName(), "prob").Data(), TString::Format("%s (%s) %s", fun->GetName(), "prob", cutsTitle.Data()).Data());
              hs->SetDrawOption("E1");
              h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), "prob", cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
              // h->SetFillColor(idStack+1);
              h->SetMarkerStyle(21);
              h->SetMarkerColor(color);
              h->SetOption("E1");
              h->SetBinContent(point[idGraphAxis], fun->GetProb());
              h->SetBinError(point[idGraphAxis], 0.001);
              hs->Add(h);
              output->Add(hs);
            }
          }
          else
          {
            for (Int_t iFun = 0; iFun < functions->GetEntries(); iFun++)
            {
              TF1 *fun = (TF1 *)functions->At(iFun);
              Double_t *params = fun->GetParameters();
              const Double_t *paramsErr = fun->GetParErrors();
              for (Int_t iPar = 0; iPar < fun->GetNpar(); iPar++)
              {
                hs = (THStack *)output->FindObject(TString::Format("%s_%s", fun->GetName(), fun->GetParName(iPar)).Data());
                hs->SetDrawOption("E1");

                h = (TH1D *)hs->GetHists()->FindObject(TString::Format("%s_%s_%s", fun->GetName(), fun->GetParName(iPar), cutsName.Data()).Data());
                if (!h)
                {
                  h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), fun->GetParName(iPar), cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
                  // h->SetFillColor(idStack+1);
                  h->SetMarkerStyle(21);
                  h->SetMarkerColor(color);
                  h->SetOption("E1");
                  hs->Add(h);
                }
                h->SetBinContent(point[idGraphAxis], params[iPar]);
                h->SetBinError(point[idGraphAxis], paramsErr[iPar]);
                Printf("2 %s %d %d %d -> %d %d %d v=%f e=%f", hs->GetTitle(), idGraphAxis, idStackAxis, idCanvasAxis, point[idGraphAxis], point[idStackAxis], point[idCanvasAxis], params[iPar], paramsErr[iPar]);
                // Printf("%s_%s_%d %f %f", fun->GetName(), fun->GetParName(iPar), idStack, params[iPar], paramsErr[iPar]);
              }
              hs = (THStack *)output->FindObject(TString::Format("%s_%s", fun->GetName(), "chi2").Data());
              hs->SetDrawOption("E1");
              h = (TH1D *)hs->GetHists()->FindObject(TString::Format("%s_%s_%d", fun->GetName(), "chi2", idStack).Data());
              if (!h)
              {
                h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), "chi2", cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
                // h->SetFillColor(idStack+1);
                h->SetMarkerStyle(21);
                h->SetMarkerColor(color);
                h->SetOption("E1");
                hs->Add(h);
              }
              h->SetBinContent(point[idGraphAxis], fun->GetChisquare());
              h->SetBinError(point[idGraphAxis], 0.001);

              // Printf("2 %s %d %d %d -> %d %d %d", h->GetTitle(), idGraphAxis, idStackAxis, idCanvasAxis, point[idGraphAxis], point[idStackAxis], point[idCanvasAxis]);

              hs = (THStack *)output->FindObject(TString::Format("%s_%s", fun->GetName(), "prob").Data());
              hs->SetDrawOption("E1");
              h = (TH1D *)hs->GetHists()->FindObject(TString::Format("%s_%s_%s", fun->GetName(), "prob", cutsName.Data()).Data());
              if (!h)
              {
                h = new TH1D(TString::Format("%s_%s_%s", fun->GetName(), "prob", cutsName.Data()).Data(), cutsTitle.Data(), aGraph->GetNbins(), aGraph->GetXmin(), aGraph->GetXmax());
                // h->SetFillColor(idStack+1);
                h->SetMarkerStyle(21);
                h->SetOption("E1");
                h->SetMarkerColor(color);
                hs->Add(h);
              }

              h->SetBinContent(point[idGraphAxis], fun->GetProb());
              h->SetBinError(point[idGraphAxis], 0.001);
            }
          }
        }
        // if (fCurrent)
        // {
        fCurrent->Close();
        fCurrent = nullptr;
        // }
        iBin++;
      }
      color++;
    }

    if (output)
    {
      fOut->cd();
      output->Write();
      output = nullptr;
    }

    Printf("Successfully stored in %s", fOut->GetName());
    fOut->Close();
  }
  fNdmspcInput->Close();

  return 0;
}
