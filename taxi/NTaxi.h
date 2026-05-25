#ifndef NdmspcCoreNTaxi_H
#define NdmspcCoreNTaxi_H

#include <set>
#include <vector>
#include <TFile.h>
#include <TCanvas.h>
#include <TAxis.h>
#include <TMacro.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <TBufferJSON.h>
#include <TString.h>
#include "NLogger.h"

namespace Ndmspc {
///
/// \class NTaxi
///
/// \brief Utility class providing static helper functions for file operations, histogram manipulations,
///        axis handling, string and vector utilities, JSON parsing, and progress display.
/// \author Martin Vala <mvala@cern.ch>
///
class NTaxi : TObject {

  /// Constructor
  NTaxi() {};
  /// Destructor
  virtual ~NTaxi() {};

  public:
  /**
   * @brief Create THnSparse from Parquet Taxi file.
   * @param filename Parquet file name.
   * @param hns Optional input THnSparse.
   * @param nMaxRows Maximum number of rows to read.
   * @return Pointer to created THnSparse.
   */
  static THnSparse * CreateSparseFromParquetTaxi(const std::string & filename, THnSparse * hns = nullptr,
                                                 Int_t nMaxRows = -1);

  /// \cond CLASSIMP
  ClassDef(NTaxi, 0);
  /// \endcond;

}; // namespace NTaxi
} // namespace Ndmspc
#endif
