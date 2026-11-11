// MonteCalc - interactive windows (modeless): wizard, fitting, results, correlogram,
// influence diagram, AI mentor and the variable list.
#pragma once

#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <string>

namespace montecalc {

namespace css = com::sun::star;
using Ctx = css::uno::Reference<css::uno::XComponentContext>;
using Frame = css::uno::Reference<css::frame::XFrame>;

void openWizard(const Ctx& ctx, const Frame& frame, const std::string& function = "");  // function: e.g. "Normal" (preselected)
void openFitWindow(const Ctx& ctx, const Frame& frame);
// view: Histogram, Cumulative, Tornado, Spider, SpiderCum, Scatter, Variants, Decision, Statistics;
// exportTo: file URL - the window is exported as PNG and closed (macros, tests)
// A correlation variable (by name, or the selected correlation cell when variable is empty) opens as
// the scatter of its two variables; scatterX: show the scatter of scatterX against variable.
void openResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& view = "",
                      const std::string& exportTo = "", const std::string& scatterX = "");
void openCorrelogram(const Ctx& ctx, const Frame& frame);
void openInfluenceDiagram(const Ctx& ctx, const Frame& frame, const std::string& options = "", const std::string& exportTo = "");
void openMentor(const Ctx& ctx, const Frame& frame);
void openVariableList(const Ctx& ctx, const Frame& frame);
// "Insert correlation": graphical window (scatter preview of the copula). With var1 set the values are
// preset; exportTo saves the preview as PNG; insertNow writes the formula and closes the window.
void openCorrelationWindow(const Ctx& ctx, const Frame& frame, const std::string& var1 = "", const std::string& var2 = "",
                           double r = 0.5, int type = 0, const std::string& exportTo = "", bool insertNow = false);

// FLO_Context sheet (also used by the influence diagram and the mentor)
void writeContextSheet(const Ctx& ctx, const Frame& frame);
// Text description of the model structure and statistics (sent to the AI mentor)
std::string modelContextText(const Frame& frame);

} // namespace montecalc
