#if defined(__CINT__) || defined(__ROOTCLING__) || defined(__CLING__)

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class Ndmspc::NConfig + ;
#pragma link C++ class Ndmspc::NThreadData + ;
#pragma link C++ class Ndmspc::NDimensionalExecutor + ;
#pragma link C++ class Ndmspc::NBinning + ;
#pragma link C++ class Ndmspc::NTreeBranch + ;
#pragma link C++ class Ndmspc::NHnSparseTreeInfo + ;
#pragma link C++ class Ndmspc::NHnSparseTree + ;
#pragma link C++ class Ndmspc::NHnSparseTreeUtils + ;
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayD> + ;
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayF> + ;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayL64> + ;
#else
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayL> + ;
#endif
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayI> + ;
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayS> + ;
#pragma link C++ class Ndmspc::NHnSparseTreeT < TArrayC> + ;

#endif
