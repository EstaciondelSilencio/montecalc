// MonteCalc for Excel - formulas between LibreOffice syntax (shared code) and Excel syntax:
// "; " <-> "," separators, $'Sheet'.A1 <-> 'Sheet'!A1 references, {1;2|3;4} <-> {1,2;3,4} arrays
#pragma once

#include <string>

namespace montecalc {
namespace xlf {
std::string toExcel(const std::string& libreOfficeFormula);
std::string fromExcel(const std::string& excelFormula);
} // namespace xlf
} // namespace montecalc
