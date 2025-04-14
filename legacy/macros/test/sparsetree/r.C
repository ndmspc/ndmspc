#include <HnSparseTreeUtils.h>

void r(int limit = 1000, std::string file = "/tmp/hnst.root")
{
  Ndmspc::HnSparseTreeUtils::Read(limit, file);
  Ndmspc::HnSparseTreeUtils::ReadNew(limit, file);
}
