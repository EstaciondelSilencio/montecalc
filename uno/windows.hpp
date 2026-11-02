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

void openWizard(const Ctx& ctx, const Frame& frame);
void openFitWindow(const Ctx& ctx, const Frame& frame);
void openResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable);
void openCorrelogram(const Ctx& ctx, const Frame& frame);
void openInfluenceDiagram(const Ctx& ctx, const Frame& frame);
void openMentor(const Ctx& ctx, const Frame& frame);
void openVariableList(const Ctx& ctx, const Frame& frame);

// FLO_Context sheet (also used by the influence diagram and the mentor)
void writeContextSheet(const Ctx& ctx, const Frame& frame);
// Text description of the model structure and statistics (sent to the AI mentor)
std::string modelContextText(const Frame& frame);

} // namespace montecalc
