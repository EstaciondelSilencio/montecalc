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
    std::string value;      // in/out (text fields)
    bool isCheck = false;
    bool checked = false;   // in/out (check boxes)
};

// Shows a modal dialog with one row per field. Returns true on OK.
bool showForm(const css::uno::Reference<css::uno::XComponentContext>& ctx,
              const std::string& title, std::vector<FormField>& fields);

} // namespace montecalc
