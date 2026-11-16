// MonteCalc - Calc add-in exposing the MC FLO model and tool functions
#pragma once


#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/lang/XServiceName.hpp>
#include <com/sun/star/sheet/XAddIn.hpp>
#include <com/sun/star/sheet/XCompatibilityNames.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <cppuhelper/implbase5.hxx>
#include <org/montecalc/XMonteCalc.hpp>

#include <string>
#include <utility>
#include <vector>

namespace montecalc {

using rtl::OUString;
namespace css = com::sun::star;

using AnyMatrix = css::uno::Sequence<css::uno::Sequence<css::uno::Any>>;
using DoubleMatrix = css::uno::Sequence<css::uno::Sequence<double>>;

constexpr const char* kAddInImplName = "org.montecalc.MonteCalcAddIn";
constexpr const char* kAddInServiceName = "org.montecalc.MonteCalc";

class MonteCalcAddIn
    : public cppu::WeakImplHelper5<org::montecalc::XMonteCalc, css::sheet::XAddIn, css::sheet::XCompatibilityNames,
                                   css::lang::XServiceName, css::lang::XServiceInfo> {
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
    // XCompatibilityNames: the Excel (MC FLO) spelling, e.g. FLOsimula_Uniform, used when Calc reads
    // and writes Excel files; Calc compares these names exactly, including the case
    css::uno::Sequence<css::sheet::LocalizedName> SAL_CALL getCompatibilityNames(const OUString& aProgrammaticName) override;
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

// every spreadsheet function: (programmatic name, MC FLO name), e.g. ("FMC_Average", "fMC_Average")
const std::vector<std::pair<std::string, std::string>>& mcfloFunctionNames();

} // namespace montecalc
