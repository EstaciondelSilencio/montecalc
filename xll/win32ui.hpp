// MonteCalc for Excel - Win32 helpers shared by the windows (win32ui.cpp) and the ribbon
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace montecalc {
namespace w32 {
void ensureGdiplus();
HINSTANCE module();          // the .xll
HWND excelWindow();          // Excel's main window (owner of MonteCalc's windows)
HFONT uiFont(int shrink);    // the Windows message font, optionally smaller
void baseUnits(int& bx, int& by);
std::string pathOfUrl(const std::string& url);
bool copyToClipboard(const std::string& text);
// converts an HTML file with Microsoft Word: kind "word" (.docx) or "pdf"; "" if Word is not available
std::string convertHtml(const std::string& htmlPath, const std::string& kind);
} // namespace w32
} // namespace montecalc
#endif
