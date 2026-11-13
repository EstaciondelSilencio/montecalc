// MonteCalc for Excel - the Excel objects behind ModelDoc / Cell (uno/model.hpp)
#pragma once
#ifdef _WIN32
#include "com.hpp"

#include <functional>

namespace montecalc {
struct XlCellImpl {
    com::Disp range;
};
struct XlDocImpl {
    com::Disp wb;
};
// runs fn inside a command that Excel runs itself (MCFLO.RunInCommand), where Excel always accepts
// changes of the workbook; exceptions of fn are thrown again here (xll.cpp)
void runInExcelCommand(const std::function<void()>& fn);
} // namespace montecalc
#endif
