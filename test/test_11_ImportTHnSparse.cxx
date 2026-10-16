#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <TFile.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TSystem.h>
#include "ndmspc/core/NGnTree.h"

namespace {

void WriteJsonFile(const std::string & path, const std::string & content)
{
  std::ofstream out(path);
  out << content;
}

bool FileExists(const std::string & path) { return gSystem->AccessPathName(path.c_str()) == kFALSE; }

} // namespace

TEST(NImportTHnSparse, ImportCernStaff)
{
  const std::string jsonPath = "test_import_thnsparse.json";
  const std::string outFile  = "/tmp/test_import_thnsparse_ngnt.root";
  WriteJsonFile(jsonPath, std::string(R"({
    "type": "thnsparse",
    "objects": [ { "name": "hsparse", "type": "THnSparse" } ],
    "axes": ["Age", "Service"],
    "binning": { "Service": [[3]] },
    "projections": [["Age"], ["Age", "Service"]],
    "outputFile": ")") +
                              outFile + R"("
  })");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonPath, "cernstaff.root");
  ASSERT_NE(ngnt, nullptr);
  ASSERT_NE(ngnt->GetBinning(), nullptr);
  EXPECT_EQ(ngnt->GetBinning()->GetAxes().size(), 2u);
  EXPECT_GT(ngnt->GetEntries(), 0);
  delete ngnt;

  ASSERT_TRUE(FileExists(outFile));
  Ndmspc::NGnTree * reopened = Ndmspc::NGnTree::Open(outFile);
  ASSERT_NE(reopened, nullptr);
  ASSERT_NE(reopened->GetBinning(), nullptr);
  EXPECT_EQ(reopened->GetBinning()->GetAxes().size(), 2u);
  EXPECT_GT(reopened->GetEntries(), 0);
  delete reopened;

  std::remove(jsonPath.c_str());
}

TEST(NImportTHnSparse, UnknownAxisReturnsNull)
{
  const std::string jsonPath = "test_import_thnsparse_bad_axis.json";
  WriteJsonFile(jsonPath, std::string(R"({
    "type": "thnsparse",
    "objects": [ { "name": "hsparse", "type": "THnSparse" } ],
    "axes": ["NotAnAxis"],
    "outputFile": "/tmp/test_import_thnsparse_bad_axis.root"
  })"));

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonPath, "cernstaff.root");
  EXPECT_EQ(ngnt, nullptr);
  delete ngnt;

  std::remove(jsonPath.c_str());
}

TEST(NImportTHnSparse, MismatchedBinningReturnsNull)
{
  const std::string inFile   = "/tmp/test_import_thnsparse_mismatch.root";
  const std::string jsonPath = "test_import_thnsparse_mismatch.json";

  {
    TFile * f = TFile::Open(inFile.c_str(), "RECREATE");
    ASSERT_NE(f, nullptr);
    Int_t    binsA[1] = {10};
    Int_t    binsB[1] = {20};
    Double_t minA[1]  = {0};
    Double_t maxA[1]  = {1};

    THnSparseF * hA = new THnSparseF("hA", "hA", 1, binsA, minA, maxA);
    hA->GetAxis(0)->SetName("x");
    hA->Write();

    THnSparseF * hB = new THnSparseF("hB", "hB", 1, binsB, minA, maxA);
    hB->GetAxis(0)->SetName("x");
    hB->Write();

    f->Close();
    delete f;
  }

  WriteJsonFile(jsonPath, std::string(R"({
    "type": "thnsparse",
    "objects": [ { "name": "hA", "type": "THnSparse" }, { "name": "hB", "type": "THnSparse" } ],
    "axes": ["x"],
    "outputFile": "/tmp/test_import_thnsparse_mismatch_ngnt.root"
  })"));

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonPath, inFile);
  EXPECT_EQ(ngnt, nullptr);
  delete ngnt;

  std::remove(jsonPath.c_str());
}

TEST(NImportTHnSparse, TitlesImportedFromTHnSparseAndOverriddenByJson)
{
  const std::string inFile   = "/tmp/test_import_thnsparse_titles.root";
  const std::string jsonPath = "test_import_thnsparse_titles.json";
  const std::string outFile  = "/tmp/test_import_thnsparse_titles_ngnt.root";

  {
    TFile * f = TFile::Open(inFile.c_str(), "RECREATE");
    ASSERT_NE(f, nullptr);
    Int_t    bins[2] = {10, 5};
    Double_t min[2]  = {0, 0};
    Double_t max[2]  = {10, 5};

    THnSparseF * h = new THnSparseF("h", "h", 2, bins, min, max);
    h->GetAxis(0)->SetName("x");
    h->GetAxis(0)->SetTitle("X title");
    h->GetAxis(1)->SetName("y");
    h->GetAxis(1)->SetTitle("Y title");
    Double_t v[2];
    for (int i = 0; i < 10; i++) {
      v[0] = i;
      v[1] = i % 5;
      h->Fill(v);
    }
    h->Write();

    f->Close();
    delete f;
  }

  WriteJsonFile(jsonPath, std::string(R"({
    "type": "thnsparse",
    "objects": [ { "name": "h", "type": "THnSparse" } ],
    "axes": ["x", "y"],
    "titles": { "y": "Overridden Y" },
    "projections": [["x"], ["x", "y"]],
    "outputFile": ")") +
                              outFile + R"("
  })");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonPath, inFile);
  ASSERT_NE(ngnt, nullptr);
  ASSERT_NE(ngnt->GetBinning(), nullptr);
  ASSERT_EQ(ngnt->GetBinning()->GetAxes().size(), 2u);
  // 'x' title comes from the THnSparse, 'y' is overridden from the JSON
  EXPECT_STREQ(ngnt->GetBinning()->GetAxes()[0]->GetTitle(), "X title");
  EXPECT_STREQ(ngnt->GetBinning()->GetAxes()[1]->GetTitle(), "Overridden Y");

  // Projected histograms carry the same titles on their axes
  bool checked1D = false;
  bool checked2D = false;
  for (Long64_t e = 0; e < ngnt->GetEntries() && !(checked1D && checked2D); e++) {
    ngnt->GetEntry(e);
    TList * out = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
    if (!out) continue;
    for (int i = 0; i < out->GetEntries(); i++) {
      TH1 * proj = dynamic_cast<TH1 *>(out->At(i));
      if (!proj) continue;
      const std::string name = proj->GetName();
      if (name == "h_proj_x") {
        EXPECT_STREQ(proj->GetXaxis()->GetTitle(), "X title");
        checked1D = true;
      }
      else if (name == "h_proj_x_y") {
        EXPECT_STREQ(proj->GetXaxis()->GetTitle(), "X title");
        EXPECT_STREQ(proj->GetYaxis()->GetTitle(), "Overridden Y");
        checked2D = true;
      }
    }
  }
  EXPECT_TRUE(checked1D);
  EXPECT_TRUE(checked2D);
  delete ngnt;

  std::remove(jsonPath.c_str());
}

/** A projection's axis carries no title of its own (ROOT does not put one there), so it reads by name. */
TEST(NImportTHnSparse, ProjectionAxisWithoutATitleReadsByItsName)
{
  const std::string inFile   = "/tmp/test_import_thnsparse_notitle.root";
  const std::string jsonPath = "test_import_thnsparse_notitle.json";

  {
    TFile * f = TFile::Open(inFile.c_str(), "RECREATE");
    ASSERT_NE(f, nullptr);
    Int_t    bins[1] = {5};
    Double_t min[1]  = {0};
    Double_t max[1]  = {5};

    THnSparseF * h = new THnSparseF("h", "h", 1, bins, min, max);
    h->GetAxis(0)->SetName("x"); // deliberately: no title
    Double_t v[1] = {1};
    h->Fill(v);
    h->Write();

    f->Close();
    delete f;
  }

  WriteJsonFile(jsonPath, std::string(R"({
    "type": "thnsparse",
    "objects": [ { "name": "h", "type": "THnSparse" } ],
    "axes": ["x"],
    "projections": [["x"]],
    "outputFile": "/tmp/test_import_thnsparse_notitle_ngnt.root"
  })"));

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(jsonPath, inFile);
  ASSERT_NE(ngnt, nullptr);

  bool checked = false;
  for (Long64_t e = 0; e < ngnt->GetEntries() && !checked; e++) {
    ngnt->GetEntry(e);
    TList * out = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
    if (!out) continue;
    for (int i = 0; i < out->GetEntries(); i++) {
      TH1 * proj = dynamic_cast<TH1 *>(out->At(i));
      if (proj != nullptr && std::string(proj->GetName()) == "h_proj_x") {
        EXPECT_STREQ(proj->GetXaxis()->GetTitle(), "x");
        checked = true;
      }
    }
  }
  EXPECT_TRUE(checked);

  delete ngnt;
  std::remove(jsonPath.c_str());
}

/**
 * The configuration may arrive as a **json object** too, not only as a JSON file: that is what a form
 * fills in (`NGnTree::Import(cfg, filename)`), and the file overload is the same work with the loading
 * in front of it.
 */
TEST(NImportTHnSparse, ImportsFromAJsonObject)
{
  const std::string inFile  = "/tmp/test_import_thnsparse_object.root";
  const std::string outFile = "/tmp/test_import_thnsparse_object_ngnt.root";

  {
    TFile * f = TFile::Open(inFile.c_str(), "RECREATE");
    ASSERT_NE(f, nullptr);
    Int_t    bins[2] = {6, 4};
    Double_t min[2]  = {0, 0};
    Double_t max[2]  = {6, 4};

    THnSparseF * h = new THnSparseF("h", "h", 2, bins, min, max);
    h->GetAxis(0)->SetName("x");
    h->GetAxis(1)->SetName("y");
    Double_t v[2] = {1, 1};
    h->Fill(v);
    h->Write();

    f->Close();
    delete f;
  }

  json cfg;
  cfg["type"]        = "thnsparse";
  cfg["objects"]     = json::array({"h"});
  cfg["axes"]        = json::array({"x"});
  cfg["projections"] = json::array({json::array({"x"}), json::array({"x", "y"})});
  cfg["outputFile"]  = outFile;

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Import(cfg, inFile);
  ASSERT_NE(ngnt, nullptr);
  ASSERT_NE(ngnt->GetBinning(), nullptr);
  EXPECT_EQ(ngnt->GetBinning()->GetAxes().size(), 1u);
  EXPECT_GT(ngnt->GetEntries(), 0);
  delete ngnt;

  EXPECT_TRUE(FileExists(outFile));
  std::remove(outFile.c_str());
}
