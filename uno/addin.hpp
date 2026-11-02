// MonteCalc - Calc add-in exposing the simulation functions
#pragma once

#include <com/sun/star/lang/XLocalizable.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/XServiceName.hpp>
#include <com/sun/star/sheet/XAddIn.hpp>
#include <cppuhelper/implbase4.hxx>
#include <org/montecalc/XMonteCalc.hpp>

#include "../core/distributions.h"
#include "../core/simulation.h"
#include "../core/timeseries.h"

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace montecalc {

using rtl::OUString;
namespace css = com::sun::star;

using AnyMatrix = css::uno::Sequence<css::uno::Sequence<css::uno::Any>>;
using DoubleMatrix = css::uno::Sequence<css::uno::Sequence<double>>;

constexpr const char* kAddInImplName = "org.montecalc.MonteCalcAddIn";
constexpr const char* kAddInServiceName = "org.montecalc.MonteCalc";

class MonteCalcAddIn
    : public cppu::WeakImplHelper4<org::montecalc::XMonteCalc, css::sheet::XAddIn,
                                  css::lang::XServiceName, css::lang::XServiceInfo> {
public:
    MonteCalcAddIn() = default;

    // XMonteCalc (generated)
#include "functions_decl.inc"

    // XAddIn
    OUString SAL_CALL getProgrammaticFuntionName(const OUString& aDisplayName) override;
    OUString SAL_CALL getDisplayFunctionName(const OUString& aProgrammaticName) override;
    OUString SAL_CALL getFunctionDescription(const OUString& aProgrammaticName) override;
    OUString SAL_CALL getDisplayArgumentName(const OUString& aProgrammaticFunctionName,
                                             sal_Int32 nArgument) override;
    OUString SAL_CALL getArgumentDescription(const OUString& aProgrammaticFunctionName,
                                             sal_Int32 nArgument) override;
    OUString SAL_CALL getProgrammaticCategoryName(const OUString& aProgrammaticFunctionName) override;
    OUString SAL_CALL getDisplayCategoryName(const OUString& aProgrammaticFunctionName) override;

    // XServiceName
    OUString SAL_CALL getServiceName() override;
    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& ServiceName) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;
    // XLocalizable (base of XAddIn)
    void SAL_CALL setLocale(const css::lang::Locale& eLocale) override;
    css::lang::Locale SAL_CALL getLocale() override;

    static css::uno::Sequence<OUString> serviceNames();

private:
    css::lang::Locale locale_;

    double distInput(const OUString& name, const char* label, std::initializer_list<double> params,
                     const std::function<mc::DistPtr()>& make, const css::uno::Any& lower,
                     const css::uno::Any& upper, const css::uno::Any& count);
    double customInput(const OUString& name, const AnyMatrix& data, const css::uno::Any& bins,
                       const css::uno::Any& lower, const css::uno::Any& upper,
                       const css::uno::Any& count);
    double discreteCustomInput(const OUString& name, const AnyMatrix& values,
                               const AnyMatrix& probs, const css::uno::Any& lower,
                               const css::uno::Any& upper, const css::uno::Any& count);
    double tsInput(const OUString& name, const char* label, mc::TsType type,
                   std::initializer_list<double> params, const css::uno::Any& periods);
    double withSamples(const OUString& name,
                       const std::function<double(const std::vector<double>&)>& fn);
    double withPair(const OUString& n1, const OUString& n2,
                    const std::function<double(const std::vector<double>&,
                                               const std::vector<double>&)>& fn);
    OUString bootstrapText(const OUString& name, const AnyMatrix* data, double samples,
                           double confidence, const css::uno::Any& p);
    OUString topDriverName(const OUString& output, double rank);
    double topDriverCorr(const OUString& output, double rank);
    OUString variableName(mc::VarKind kind, double pos);
    DoubleMatrix samplesMatrix(const OUString& name);
    DoubleMatrix histogramMatrix(const OUString& name, const css::uno::Any& bins);
    double sortValue(const AnyMatrix& data, double pos, const css::uno::Any& desc);
    DoubleMatrix benfordMatrix(const AnyMatrix& data);
};

} // namespace montecalc
