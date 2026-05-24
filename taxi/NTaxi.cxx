#include <TROOT.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/exception.h>
#include "NTaxi.h"

using std::ifstream;

/// \cond CLASSIMP
ClassImp(Ndmspc::NTaxi);
/// \endcond

namespace Ndmspc {


THnSparse * NTaxi::CreateSparseFromParquetTaxi(const std::string & filename, THnSparse * hns, Int_t nMaxRows)
{
  ///
  /// Create THnSparse from Parquet file
  ///
  // Open the Parquet file

  if (hns == nullptr) {
    NLogError("NTaxi::CreateSparseFromParquetTaxi: THnSparse 'hns' is nullptr ...");
    return nullptr;
  }

  std::shared_ptr<arrow::io::ReadableFile>                infile;
  arrow::Result<std::shared_ptr<arrow::io::ReadableFile>> infile_result = arrow::io::ReadableFile::Open(filename);
  if (!infile_result.ok()) {
    NLogError("NTaxi::CreateSparseFromParquetTaxi: Error opening file %s: %s", filename.c_str(),
              infile_result.status().ToString().c_str());
    return nullptr;
  }
  infile = infile_result.ValueUnsafe();

  // Create a Parquet reader using the modern arrow::Result API
  std::unique_ptr<parquet::arrow::FileReader> reader;

  // The new approach using arrow::Result:
  arrow::Result<std::unique_ptr<parquet::arrow::FileReader>> reader_result =
      parquet::arrow::OpenFile(infile, arrow::default_memory_pool()); // No third parameter!
  if (!reader_result.ok()) {
    NLogError("NTaxi::CreateSparseFromParquetTaxi: Error opening Parquet file reader for file %s: %s",
              filename.c_str(), reader_result.status().ToString().c_str());
    arrow::Status status = infile->Close(); // Attempt to close
    return nullptr;
  }
  reader = std::move(reader_result).ValueUnsafe(); // Transfer ownership from Result to unique_ptr

  // Get file metadata (optional)
  // Note: parquet_reader() returns a const ptr, and metadata() returns a shared_ptr
  std::shared_ptr<parquet::FileMetaData> file_metadata = reader->parquet_reader()->metadata();
  NLogTrace("Parquet file '%s' opened successfully.", filename.c_str());
  NLogTrace("Parquet file version: %d", file_metadata->version());
  NLogTrace("Parquet created by: %s", file_metadata->created_by().c_str());
  NLogTrace("Parquet number of columns: %d", file_metadata->num_columns());
  NLogTrace("Parquet number of rows: %lld", file_metadata->num_rows());
  NLogTrace("Parquet number of row groups: %d", file_metadata->num_row_groups());

  // Read the file as a record batch stream (Result API, non-deprecated)
  arrow::Result<std::unique_ptr<arrow::RecordBatchReader>> batch_reader_result = reader->GetRecordBatchReader();
  if (!batch_reader_result.ok()) {
    NLogError("NTaxi::CreateSparseFromParquetTaxi: Error reading table from Parquet file %s: %s", filename.c_str(),
              batch_reader_result.status().ToString().c_str());
    arrow::Status status = infile->Close();
    return nullptr;
  }
  auto batch_reader = std::move(batch_reader_result).ValueUnsafe();

  arrow::Status status;

  // It's good practice to close the input file stream when done
  status = infile->Close();
  if (!status.ok()) {
    NLogWarning("NTaxi::CreateSparseFromParquetTaxi: Error closing input file %s: %s", filename.c_str(),
                status.ToString().c_str());
    // This is a warning, we still want to return the table.
  }

  // Print schema of the table
  NLogTrace("Parquet Table Schema:\n%s", batch_reader->schema()->ToString().c_str());

  const Int_t              nDims = hns->GetNdimensions();
  std::vector<std::string> column_names;
  for (int i = 0; i < nDims; ++i) {
    column_names.push_back(hns->GetAxis(i)->GetName());
  }
  // std::cout << "\nData (first 5 rows):\n";

  // int max_rows                                           = table->num_rows();
  int max_rows   = 1e8;
  max_rows       = nMaxRows > 0 ? std::min(max_rows, nMaxRows) : max_rows;
  int print_rows = std::min(max_rows, 5);
  std::shared_ptr<arrow::RecordBatch> batch;
  auto                                point = std::make_unique<Double_t[]>(nDims);
  // Double_t                            point[nDims];

  if (print_rows > 0) {
    NLogTrace("Printing first %d rows of Parquet file '%s' ...", print_rows, filename.c_str());
    // NLogInfo("Columns: %s", NTaxi::Join(column_names, '\t').c_str());
  }

  while (batch_reader->ReadNext(&batch).ok() && batch) {
    NLogTrace("Processing batch with %d rows and %d columns ...", batch->num_rows(), batch->num_columns());
    for (int i = 0; i < batch->num_rows(); ++i) {
      if (i >= max_rows) break; // Limit to first 5 rows for display

      bool isValid = true;
      int  idx     = 0;
      for (int j = 0; j < batch->num_columns(); ++j) {
        if (std::find(column_names.begin(), column_names.end(), batch->column_name(j)) == column_names.end())
          continue; // Skip columns not in our list
        // NLogDebug("[%d %s]Processing row %d, column '%s' ...", idx, hns->GetAxis(idx)->GetName(), i,
        //                batch->column_name(j).c_str());
        // std::cout << batch->column_name(j) << "\t";
        const auto &                                  array         = batch->column(j);
        arrow::Result<std::shared_ptr<arrow::Scalar>> scalar_result = array->GetScalar(i);
        if (scalar_result.ok()) {
          // if (i < print_rows) std::cout << scalar_result.ValueUnsafe()->ToString() << "\t";
          if (scalar_result.ValueUnsafe()->is_valid) {
            TAxis * axis = hns->GetAxis(idx);
            if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::STRING ||
                scalar_result.ValueUnsafe()->type->id() == arrow::Type::LARGE_STRING) {
              // Arrow StringScalar's value is an arrow::util::string_view or arrow::util::string_view
              // It's best to convert it to std::string for general use.
              std::string value = scalar_result.ValueUnsafe()->ToString();
              // TODO: check if not shifted by one
              // NLogInfo("NTaxi::CreateSparseFromParquetTaxi: Mapping string value '%s' to axis '%s' ...",
              //               value.c_str(), axis->GetName());
              point[idx] = axis->GetBinCenter(axis->FindBin(value.c_str()));
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::INT32) {
              auto int_scalar = std::static_pointer_cast<arrow::Int32Scalar>(scalar_result.ValueUnsafe());

              point[idx] = static_cast<Double_t>(int_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::INT64) {
              auto int64_scalar = std::static_pointer_cast<arrow::Int64Scalar>(scalar_result.ValueUnsafe());
              point[idx]        = static_cast<Double_t>(int64_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::UINT32) {
              auto uint32_scalar = std::static_pointer_cast<arrow::UInt32Scalar>(scalar_result.ValueUnsafe());
              point[idx]         = static_cast<Double_t>(uint32_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::FLOAT) {
              auto float_scalar = std::static_pointer_cast<arrow::FloatScalar>(scalar_result.ValueUnsafe());
              point[idx]        = static_cast<Double_t>(float_scalar->value);
            }
            else if (scalar_result.ValueUnsafe()->type->id() == arrow::Type::DOUBLE) {
              auto double_scalar = std::static_pointer_cast<arrow::DoubleScalar>(scalar_result.ValueUnsafe());
              point[idx]         = double_scalar->value;
            }
            else {
              NLogError("NTaxi::CreateSparseFromParquetTaxi: Unsupported data type for column '%s' ...",
                        batch->column_name(j).c_str());
              isValid = false;
            }
          }
          else {
            // Handle null values (set to 0 or some default)
            //
            //
            point[idx] = -1000;
            isValid    = false;
            isValid    = true;
          }
        }
        else {
          NLogError("NTaxi::CreateSparseFromParquetTaxi: Error getting scalar at (%d,%d): %s", i, j,
                    scalar_result.status().ToString().c_str());
          isValid = false;
        }
        idx++;
      }
      // if (i < print_rows) std::cout << std::endl;
      if (isValid) {
        // print point
        // for (int d = 0; d < nDims; ++d) {
        //   NLogDebug("Point[%d=%s]=%f", d, hns->GetAxis(d)->GetName(), point[d]);
        // }
        hns->Fill(point.get());
      }
      else {
        NLogWarning("Skipping row %d due to invalid data.", i);
      }
    }
  }
  return hns;
}
} // namespace Ndmspc
