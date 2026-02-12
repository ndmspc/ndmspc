#ifndef Ndmspc_NumcalResult_H
#define Ndmspc_NumcalResult_H
#include <TNamed.h>
#include <vector>
#include <iostream>

namespace Ndmspc {
class NumcalResult : public TNamed {
public:
    NumcalResult();
    NumcalResult(const char* name, const char* title);
    virtual ~NumcalResult();
    void Print(Option_t* option = "") const override;
    int GetNeval() const { return fNeval; }
    int GetFail() const { return fFail; }
    const std::vector<double>& GetIntegral() const { return fIntegral; }
    const std::vector<double>& GetError() const { return fError; }
    const std::vector<double>& GetProb() const { return fProb; }

    void SetIntegral(const std::vector<double>& values) { fIntegral = values; }
    void SetError(const std::vector<double>& values) { fError = values; }
    void SetProb(const std::vector<double>& values) { fProb = values; }
    void SetNeval(int value) { fNeval = value; }
    void SetFail(int value) { fFail = value; }
    std::vector<double>& IntegralRef() { return fIntegral; }
    std::vector<double>& ErrorRef() { return fError; }
    std::vector<double>& ProbRef() { return fProb; }
    // For direct assignment and .data() access
    int& NevalRef() { return fNeval; }
    int& FailRef() { return fFail; }
private:
    int fNeval = 0;                ///< Number of function evaluations
    int fFail = 0;                 ///< Failure code from integration
    std::vector<double> fIntegral; ///< Computed integral values
    std::vector<double> fError;    ///< Estimated errors for each integral
    std::vector<double> fProb;     ///< Probability values for each integral
};
} // namespace Ndmspc
#endif
