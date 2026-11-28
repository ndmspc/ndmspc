# Optional feature modules, grouped here. Each keeps its own public include namespace
# (ndmspc/hep, ndmspc/taxi, ndmspc/numcal) via the include-subdir argument to RootLib; see
# cmake/modules/ROOTMacros.cmake. core/ and http/ stay at the repository root.
if(WITH_HEP)
  add_subdirectory(hep)
endif()

if(WITH_NUMCAL)
  add_subdirectory(numcal)
endif()

if(WITH_TAXI)
  add_subdirectory(taxi)
endif()
