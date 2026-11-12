// MonteCalc - simulation engine and MC FLO commands (protocol handler for "org.montecalc:")
#pragma once

#ifndef MONTECALC_XLL
#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/implbase4.hxx>
#else
#include "../xll/uno_shim.hpp"
#endif

#include "../core/simulation.h"
#include "model.hpp"

#include <string>
#include <vector>

namespace montecalc {

using rtl::OUString;
namespace css = com::sun::star;

constexpr const char* kEngineImplName = "org.montecalc.Engine";
constexpr const char* kEngineServiceName = "com.sun.star.frame.ProtocolHandler";

struct RunOptions {
    mc::Settings s;
    bool visualize = false;
    bool report = true;
    bool memory = false;   // Memory Calculation
    bool dip = false;      // Use DIP: store outputs as FLO_r_ arrays in the model
};

// multiple simulation: rows Variant | Value | Reference (=cell) of the stored range
struct Variant {
    std::string label;
    double value = 0;
    css::table::CellAddress ref;
};
std::vector<Variant> loadVariants(const ModelDoc& d);
void restoreReplay(const ModelDoc& d);   // the model's own input formulas again (after "Analyse iteration")
void storeDip(const ModelDoc& d);        // FLO_r_ arrays of the outputs (Use DIP)
double cellValue(const Cell& c);         // NaN for texts and errors

RunOptions loadOptions(const ModelDoc& d);
void saveOptions(const ModelDoc& d, const RunOptions& o);

// Runs a full simulation (all variants). Returns a summary text. Used by the
// engine and by the result windows ("Start").
struct RunResult {
    std::string summary;
    double seconds = 0;
    bool memoryUsed = false;
    std::vector<std::string> warnings;
};
RunResult runSimulation(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                        const css::uno::Reference<css::frame::XFrame>& frame, const RunOptions& o);

#ifdef MONTECALC_XLL
// Excel: the MC FLO commands, run by the ribbon and the windows
class Engine {
public:
    Engine() = default;
    // e.g. run("Wizard", {{"Function", "Normal"}}); silent: no dialogs (arguments give the values)
    void run(const std::string& command, const std::vector<css::beans::PropertyValue>& args = {}, bool silent = false);
#else
class Engine : public cppu::WeakImplHelper4<css::frame::XDispatchProvider, css::frame::XDispatch,
                                            css::lang::XInitialization, css::lang::XServiceInfo> {
public:
    explicit Engine(const css::uno::Reference<css::uno::XComponentContext>& ctx) : ctx_(ctx) {}

    void SAL_CALL initialize(const css::uno::Sequence<css::uno::Any>& args) override;
    css::uno::Reference<css::frame::XDispatch> SAL_CALL queryDispatch(const css::util::URL& url, const OUString& target,
                                                                     sal_Int32 flags) override;
    css::uno::Sequence<css::uno::Reference<css::frame::XDispatch>> SAL_CALL queryDispatches(
        const css::uno::Sequence<css::frame::DispatchDescriptor>& requests) override;
    void SAL_CALL dispatch(const css::util::URL& url, const css::uno::Sequence<css::beans::PropertyValue>& args) override;
    void SAL_CALL addStatusListener(const css::uno::Reference<css::frame::XStatusListener>& l, const css::util::URL& url) override;
    void SAL_CALL removeStatusListener(const css::uno::Reference<css::frame::XStatusListener>&, const css::util::URL&) override {}
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& s) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;
    static css::uno::Sequence<OUString> serviceNames();
#endif

private:
    void execute(const std::string& cmd);   // the command table (both programs)
    css::uno::Reference<css::uno::XComponentContext> ctx_;
    css::uno::Reference<css::frame::XFrame> frame_;
    bool silent_ = false;
    css::uno::Sequence<css::beans::PropertyValue> args_;

    css::uno::Reference<css::frame::XModel> model();
    void message(const std::string& title, const std::string& text, bool error = false);
    bool ask(const std::string& title, std::vector<struct FormField>& fields);

    // simulation
    void cmdRun();
    void cmdSettings();
    void cmdReset();
    void cmdStoreResults();
    void cmdSaveResults();
    void cmdReplay();
    void cmdExpected();
    void cmdPreview();
    void cmdSelectVariant();
    void cmdChangeHistogram();
    void cmdCalcCorrelations();
    // model definition
    void cmdRegister();
    void cmdModel();
    void cmdInsertOutput();
    void cmdInsertCorrelation();
    void cmdLock();
    void cmdMultipleSimulation();
    void cmdVariables();
    // tools
    void cmdHideFormulas();
    void cmdLoadFormulas();
    void cmdModelCheck();
    void cmdSort(bool ascending);
    void cmdForecastTool();
    void cmdStartBatch();
    void cmdWriteContext();
    void cmdAbout();
    void cmdFunctions();
    void cmdInsertFunction();
    void insertFunction(const std::string& name);
};

// fMC_ functions by MC FLO category (tools/gen_ui.py: FUNCTION_MENU)
struct FunctionCategory { const char* name; std::vector<const char*> functions; };
const std::vector<FunctionCategory>& functionMenu();

} // namespace montecalc
