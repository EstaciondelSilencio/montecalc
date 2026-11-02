// MonteCalc - result workbook (Data, Summary, Charts, Sensitivity sheets)
#pragma once

#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <string>

namespace montecalc {

namespace css = com::sun::star;

// Creates a new spreadsheet document with the stored results.
// Returns a note (e.g. CSV export path) or an empty string.
std::string createReport(const css::uno::Reference<css::uno::XComponentContext>& ctx, bool hidden,
                         css::uno::Reference<css::lang::XComponent>* created = nullptr);

} // namespace montecalc
