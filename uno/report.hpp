// MonteCalc - result workbook (Summary, Statistics, Sensitivity, Variants, Data)
#pragma once

#ifndef MONTECALC_XLL
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#else
#include "../xll/uno_shim.hpp"
#endif

#include <string>

namespace montecalc {

namespace css = com::sun::star;

// Creates a new spreadsheet document with the stored results of the active
// simulation. `model` is the model document (its URL is remembered for
// "Close and save results"). Returns a note (e.g. CSV export path) or "".
std::string createReport(const css::uno::Reference<css::uno::XComponentContext>& ctx, bool hidden,
                         class ModelDoc* created,
                         const css::uno::Reference<css::frame::XModel>& model);

// Rebuilds the Statistics sheet of a result workbook with a new number of bins.
void rebuildStatistics(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                       const css::uno::Reference<css::frame::XModel>& report, int bins);

// "Correlation and sensitivities": regression of Y on X with live forecast cells
// (I4 = new X, I5 = estimate, K5/L5 = approximate 95% forecast interval).
void addSensitivitySheet(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                         const css::uno::Reference<css::frame::XModel>& current, const std::string& x,
                         const std::string& y, bool hidden);

} // namespace montecalc
