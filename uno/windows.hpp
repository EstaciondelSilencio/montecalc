// MonteCalc - interactive windows (modeless): wizard, fitting, results, correlogram,
// influence diagram, AI mentor and the variable list.
#pragma once

#ifndef MONTECALC_XLL
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#else
#include "../xll/uno_shim.hpp"
#endif

#include "../core/runner.h"

#include <memory>
#include <string>

namespace montecalc {

namespace css = com::sun::star;
using Ctx = css::uno::Reference<css::uno::XComponentContext>;
using Frame = css::uno::Reference<css::frame::XFrame>;

// function: e.g. "Normal" (preselected). An input variable in the selected cell opens for editing.
// saveNow (macros, tests): saves into the selected cell at once and closes the window.
void openWizard(const Ctx& ctx, const Frame& frame, const std::string& function = "", bool saveNow = false);  // function: e.g. "Normal" (preselected)
void openFitWindow(const Ctx& ctx, const Frame& frame);
// view: Histogram, Cumulative, Tornado, Spider, SpiderCum, Scatter, Variants, Decision, Statistics;
// exportTo: file URL - the window is exported as PNG and closed (macros, tests)
// A correlation variable (by name, or the selected correlation cell when variable is empty) opens as
// the scatter of its two variables; scatterX: show the scatter of scatterX against variable.
void openResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& view = "",
                      const std::string& exportTo = "", const std::string& scatterX = "");
// After a simulation: the selection is on a cell with an input, output or correlation variable -
// the result window (one, following the selection) shows it, with a wedge pointing to the cell.
// False when the cell holds no variable with results (or the option is off).
bool resultWindowForSelection(const Ctx& ctx, const Frame& frame);
// after a run: the result window for the selected cell's variable (else `variable`), which then
// follows the selection; it replaces the window of the previous run
void openResultWindowAfterRun(const Ctx& ctx, const Frame& frame, const std::string& variable);
void openCorrelogram(const Ctx& ctx, const Frame& frame);
void openInfluenceDiagram(const Ctx& ctx, const Frame& frame, const std::string& options = "", const std::string& exportTo = "");
void openMentor(const Ctx& ctx, const Frame& frame);
void openVariableList(const Ctx& ctx, const Frame& frame);
// "Insert correlation": graphical window (scatter preview of the copula). With var1 set the values are
// preset; exportTo saves the preview as PNG; insertNow writes the formula and closes the window.
void openCorrelationWindow(const Ctx& ctx, const Frame& frame, const std::string& var1 = "", const std::string& var2 = "",
                           double r = 0.5, int type = 0, const std::string& exportTo = "", bool insertNow = false);

// Live preview during a simulation: the histogram of one variable as the iterations come in,
// with Pause / Resume and Stop. The engine opens it after the registration pass and calls
// update() regularly on the user interface thread; done() closes it after the run.
class SimulationPreview {
public:
    virtual ~SimulationPreview() = default;
    virtual void startRun(const std::string& label, int iterations) = 0;   // a run (variant) begins
    virtual void update() = 0;   // new iterations (throttled: repaints a few times per second)
    virtual void done() = 0;     // the run is over: the window closes, the controls are detached
};
std::shared_ptr<SimulationPreview> openSimulationPreview(const Ctx& ctx, const Frame& frame, const std::string& variable, mc::RunControl& ctl);

// FLO_Context sheet (also used by the influence diagram and the mentor)
void writeContextSheet(const Ctx& ctx, const Frame& frame);
// Text description of the model structure and statistics (sent to the AI mentor)
std::string modelContextText(const Frame& frame);

} // namespace montecalc
