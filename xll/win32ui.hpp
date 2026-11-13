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
// the MC FLO logo (icon resource 1 of the .xll); the Windows default icon if it is missing
HICON logoIcon(bool smallIcon);
// message box with the MC FLO logo (information) or the Windows error icon
int messageBox(HWND owner, const std::wstring& text, const std::wstring& title, bool error, UINT buttons = MB_OK);
std::string pathOfUrl(const std::string& url);
bool copyToClipboard(const std::string& text);
// keystrokes of MonteCalc's modeless windows reach them, not Excel's selected cell (installed by show())
void installKeyboardHook();
void removeKeyboardHook();
// converts an HTML file with Microsoft Word: kind "word" (.docx) or "pdf"; "" if Word is not available
std::string convertHtml(const std::string& htmlPath, const std::string& kind);
} // namespace w32
} // namespace montecalc
#endif
