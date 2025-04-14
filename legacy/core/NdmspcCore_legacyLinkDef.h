#if defined(__CINT__) || defined(__ROOTCLING__) || defined(__CLING__)

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class HttpRequestCurl+;
#pragma link C++ class Ndmspc::Axis + ;
#pragma link C++ class Ndmspc::Axes + ;
#pragma link C++ class Ndmspc::Utils + ;
#pragma link C++ class Ndmspc::Config + ;
#pragma link C++ class Ndmspc::Manager + ;
#pragma link C++ class Ndmspc::Core + ;
#pragma link C++ class Ndmspc::InputMap + ;
#pragma link C++ class Ndmspc::Results + ;
#pragma link C++ class Ndmspc::PointRun + ;
#pragma link C++ class Ndmspc::PointDraw + ;

#pragma link C++ class Ndmspc::HnSparseTreeInfo + ;
#pragma link C++ class Ndmspc::HnSparseTreeBranch + ;
#pragma link C++ class Ndmspc::HnSparseTree + ;
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayD> + ;
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayF> + ;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayL64> + ;
#else
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayL> + ;
#endif
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayI> + ;
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayS> + ;
#pragma link C++ class Ndmspc::HnSparseTreeT < TArrayC> + ;
#pragma link C++ class Ndmspc::HnSparseTreeUtils + ;

#pragma link C++ class Ndmspc::HnSparseBrowser + ;

#endif
