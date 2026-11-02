// MonteCalc - simulation engine (protocol handler for "org.montecalc:" commands)
#pragma once

#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase4.hxx>

#include "../core/simulation.h"

#include <map>
#include <string>

namespace montecalc {

using rtl::OUString;
namespace css = com::sun::star;

constexpr const char* kEngineImplName = "org.montecalc.Engine";
constexpr const char* kEngineServiceName = "com.sun.star.frame.ProtocolHandler";

class Engine : public cppu::WeakImplHelper4<css::frame::XDispatchProvider, css::frame::XDispatch,
                                           css::lang::XInitialization, css::lang::XServiceInfo> {
public:
    explicit Engine(const css::uno::Reference<css::uno::XComponentContext>& ctx) : ctx_(ctx) {}

    // XInitialization
    void SAL_CALL initialize(const css::uno::Sequence<css::uno::Any>& args) override;
    // XDispatchProvider
    css::uno::Reference<css::frame::XDispatch> SAL_CALL queryDispatch(
        const css::util::URL& url, const OUString& target, sal_Int32 flags) override;
    css::uno::Sequence<css::uno::Reference<css::frame::XDispatch>> SAL_CALL queryDispatches(
        const css::uno::Sequence<css::frame::DispatchDescriptor>& requests) override;
    // XDispatch
    void SAL_CALL dispatch(const css::util::URL& url,
                           const css::uno::Sequence<css::beans::PropertyValue>& args) override;
    void SAL_CALL addStatusListener(const css::uno::Reference<css::frame::XStatusListener>& l,
                                    const css::util::URL& url) override;
    void SAL_CALL removeStatusListener(const css::uno::Reference<css::frame::XStatusListener>&,
                                       const css::util::URL&) override {}
    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& s) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static css::uno::Sequence<OUString> serviceNames();

private:
    css::uno::Reference<css::uno::XComponentContext> ctx_;
    css::uno::Reference<css::frame::XFrame> frame_;
    bool silent_ = false;

    css::uno::Reference<css::frame::XModel> model();
    void recalc();

    void cmdRun(const css::uno::Sequence<css::beans::PropertyValue>& args);
    void cmdSettings();
    void cmdVariables();
    void cmdReport();
    void cmdReplay(const css::uno::Sequence<css::beans::PropertyValue>& args);
    void cmdExpected();
    void cmdPreview();
    void cmdLock();
    void cmdReset();
    void cmdAbout();

    void message(const std::string& title, const std::string& text, bool error = false);
};

} // namespace montecalc
