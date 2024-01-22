#if defined(__CINT__) || defined(__ROOTCLING__)

#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class NDH::HnSparse + ;
#pragma link C++ class NDH::HnSparseT<TArrayD> + ;
#pragma link C++ class NDH::HnSparseT<TArrayF> + ;
#pragma link C++ class NDH::HnSparseT<TArrayL> + ;
#pragma link C++ class NDH::HnSparseT<TArrayI> + ;
#pragma link C++ class NDH::HnSparseT<TArrayS> + ;
#pragma link C++ class NDH::HnSparseT<TArrayC> + ;

#pragma link C++ class NDH::HnSparseStress + ;

#endif
