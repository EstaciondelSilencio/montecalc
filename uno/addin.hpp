// MonteCalc - Calc add-in exposing the MC FLO model and tool functions
#pragma once

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/XServiceName.hpp>
#include <com/sun/star/sheet/XAddIn.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <cppuhelper/implbase4.hxx>
#include <org/montecalc/XMonteCalc.hpp>

namespace montecalc {

using rtl::OUString;
namespace css = com::sun::star;

using AnyMatrix = css::uno::Sequence<css::uno::Sequence<css::uno::Any>>;
using DoubleMatrix = css::uno::Sequence<css::uno::Sequence<double>>;

constexpr const char* kAddInImplName = "org.montecalc.MonteCalcAddIn";
constexpr const char* kAddInServiceName = "org.montecalc.MonteCalc";

class MonteCalcAddIn
    : public cppu::WeakImplHelper4<org::montecalc::XMonteCalc, css::sheet::XAddIn, css::lang::XServiceName,
                                   css::lang::XServiceInfo> {
public:
    MonteCalcAddIn() = default;

    // XMonteCalc (generated)
#include "functions_decl.inc"

    // XAddIn
    OUString SAL_CALL getProgrammaticFuntionName(const OUString& aDisplayName) override;
    OUString SAL_CALL getDisplayFunctionName(const OUString& aProgrammaticName) override;
    OUString SAL_CALL getFunctionDescription(const OUString& aProgrammaticName) override;
    OUString SAL_CALL getDisplayArgumentName(const OUString& aProgrammaticFunctionName, sal_Int32 nArgument) override;
    OUString SAL_CALL getArgumentDescription(const OUString& aProgrammaticFunctionName, sal_Int32 nArgument) override;
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
};

} // namespace montecalc
