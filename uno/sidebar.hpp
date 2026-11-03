// MonteCalc - factory for the "MC FLO" sidebar panels (ribbon groups)
#pragma once

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/ui/XUIElementFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase2.hxx>

namespace montecalc {

namespace css = com::sun::star;
using rtl::OUString;

constexpr const char* kPanelFactoryImplName = "org.montecalc.PanelFactory";

// ResourceURL: private:resource/toolpanel/MonteCalcPanelFactory/<Group>
class PanelFactory : public cppu::WeakImplHelper2<css::ui::XUIElementFactory, css::lang::XServiceInfo> {
public:
    explicit PanelFactory(const css::uno::Reference<css::uno::XComponentContext>& ctx) : ctx_(ctx) {}
    css::uno::Reference<css::ui::XUIElement> SAL_CALL createUIElement(
        const OUString& url, const css::uno::Sequence<css::beans::PropertyValue>& args) override;
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& s) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;
    static css::uno::Sequence<OUString> serviceNames();

private:
    css::uno::Reference<css::uno::XComponentContext> ctx_;
};

} // namespace montecalc
