// MonteCalc for Excel - the Excel objects behind ModelDoc / Cell (uno/model.hpp)
#pragma once
#ifdef _WIN32
#include "com.hpp"

namespace montecalc {
struct XlCellImpl {
    com::Disp range;
};
struct XlDocImpl {
    com::Disp wb;
};
} // namespace montecalc
#endif
