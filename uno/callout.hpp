// MonteCalc - the MC FLO "callout": a translucent wedge from a spreadsheet cell to the result window
// that was opened for it, so one sees which cell the window belongs to.
//
// Windows only (Excel and LibreOffice on Windows): a transparent, click-through layered window just
// behind the result window. It follows the window when it moves and the cell when the sheet scrolls,
// and hides while the cell is out of view or another program is in front.
#pragma once

#include <functional>

namespace montecalc {
namespace callout {

// screen rectangle (pixels) of the cell; false while the cell is not visible
using CellRect = std::function<bool(int& x, int& y, int& w, int& h)>;

// shows the wedge between the cell and the window (HWND); replaces an earlier one of the window
void attach(void* window, CellRect cell);
void detach(void* window);

} // namespace callout
} // namespace montecalc
