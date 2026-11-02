// MonteCalc - small UI helpers: message boxes and a programmatic form dialog
#pragma once

#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

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

// Shows a modal dialog with one row per field. Returns true on OK.
bool showForm(const css::uno::Reference<css::uno::XComponentContext>& ctx, const std::string& title,
              std::vector<FormField>& fields, int width = 220);

} // namespace montecalc
