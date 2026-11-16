// MonteCalc - MC FLO function names in Excel files (1:1 compatibility with MC FLO workbooks)
//
// Calc keeps its own names for add-in functions in .xlsx files (ORG.MONTECALC.MONTECALC.FLOSIMULA_PERT)
// and does not recognise MC FLO's spelling (FLOsimula_PERT) when it opens one: such cells show #NAME?.
// A job of the extension (registry/Jobs.xcu) therefore
//   - after opening a spreadsheet that is not in Calc's own format, turns MC FLO's function names
//     back into working MonteCalc calls, and
//   - after saving as .xlsx, writes MC FLO's exact spelling into the file (sheets and defined names).
#pragma once

#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/task/XJob.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase2.hxx>

#include <string>

namespace montecalc {

namespace css = com::sun::star;

// "FLOsimula_PERT(…)", "_xll.FLOsimula_PERT(…)", "flosimula_pert(…)" -> "FLOSIMULA_PERT(…)" (a name Calc resolves)
std::string toCalcSpelling(const std::string& formula, bool& changed);
// "ORG.MONTECALC.MONTECALC.FLOSIMULA_PERT(" -> "FLOsimula_PERT(" (MC FLO's spelling) in any text
std::string toMcFloSpelling(const std::string& text, bool& changed);
// cells whose formulas use MC FLO names Calc did not resolve; returns the number of cells changed
int resolveMcFloFormulas(const css::uno::Reference<css::frame::XModel>& model);
// the saved .xlsx file at url gets MC FLO's spelling; false if nothing was changed
bool writeMcFloSpelling(const css::uno::Reference<css::uno::XComponentContext>& ctx, const std::string& url);

constexpr const char* kNamesJobImplName = "org.montecalc.McFloNamesJob";
constexpr const char* kNamesJobServiceName = "org.montecalc.McFloNamesJob";

class McFloNamesJob : public cppu::WeakImplHelper2<css::task::XJob, css::lang::XServiceInfo> {
public:
    explicit McFloNamesJob(const css::uno::Reference<css::uno::XComponentContext>& ctx) : ctx_(ctx) {}
    css::uno::Any SAL_CALL execute(const css::uno::Sequence<css::beans::NamedValue>& args) override;
    rtl::OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const rtl::OUString& name) override;
    css::uno::Sequence<rtl::OUString> SAL_CALL getSupportedServiceNames() override;
    static css::uno::Sequence<rtl::OUString> serviceNames();

private:
    css::uno::Reference<css::uno::XComponentContext> ctx_;
};

} // namespace montecalc
