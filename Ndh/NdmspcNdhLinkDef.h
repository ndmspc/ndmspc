#if defined(__CINT__) || defined(__ROOTCLING__)

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class Ndmspc::Ndh::HnSparse + ;
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayD> + ;
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayF> + ;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayL64> + ;
#else
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayL> + ;
#endif
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayI> + ;
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayS> + ;
#pragma link C++ class Ndmspc::Ndh::HnSparseT < TArrayC> + ;

#pragma link C++ class Ndmspc::Ndh::HnSparseStress + ;

#endif
