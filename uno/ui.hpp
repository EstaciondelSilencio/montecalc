// MonteCalc - small UI helpers: message boxes and a programmatic form dialog
#pragma once

#ifdef MONTECALC_XLL
#include "../xll/uno_shim.hpp"
#else
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#endif

#include <string>
#include <vector>

namespace montecalc {

namespace css = com::sun::star;

void showMessage(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                 const css::uno::Reference<css::frame::XFrame>& frame, const std::string& title,
                 const std::string& text, bool error);

struct FormField {
    std::string label;
    std::string value;                 // in/out (text fields and drop-downs)
    bool isCheck = false;
    bool checked = false;              // in/out (check boxes)
    std::vector<std::string> options;  // non-empty: drop-down list
    int lines = 1;                     // > 1: multi-line text
    bool readOnly = false;
};

// Makes a button, check box or one-line label show its whole text: when the text is wider
// than the control (a longer translation), the font is reduced in small steps (to 75 % at most).
#ifndef MONTECALC_XLL
void fitControlText(const css::uno::Reference<css::uno::XInterface>& control);
#endif

// Lets the user interface work during a long computation on the calling thread (preview window,
// pause / stop buttons). In Excel only MonteCalc's own windows get mouse and keyboard input.
void processEvents(const css::uno::Reference<css::uno::XComponentContext>& ctx);
// false when a command runs on another thread than the program's windows (LibreOffice on
// Windows, commands started by scripts): that thread paints by itself, processEvents does nothing
bool onWindowThread(const css::uno::Reference<css::uno::XComponentContext>& ctx);

// Shows a modal dialog with one row per field. Returns true on OK.
bool showForm(const css::uno::Reference<css::uno::XComponentContext>& ctx, const std::string& title,
              std::vector<FormField>& fields, int width = 220);

} // namespace montecalc
