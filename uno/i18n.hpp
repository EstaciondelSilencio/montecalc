// MonteCalc - English, German and Spanish user interface texts (as in MC FLO).
//
// The code keeps writing English texts; they are translated where they reach the screen
// (window titles, labels, buttons, list entries, messages, form dialogs, diagram text).
// The table is tools/i18n_catalog.py (generated into uno/i18n_catalog.inc by tools/gen_ui.py).
//
// Language: Documents/MC_FLO_language.txt ("English", "Deutsch", "Español" or "Auto"),
// set with MC FLO > Settings; "Auto" (default) follows the LibreOffice user interface language.
#pragma once

#ifndef MONTECALC_XLL
#include <com/sun/star/uno/XComponentContext.hpp>
#endif

#include <string>

namespace montecalc {

enum class Lang { EN = 0, DE = 1, ES = 2 };

// gives the translation code access to the LibreOffice configuration (call before tr())
#ifndef MONTECALC_XLL
void i18nSetContext(const com::sun::star::uno::Reference<com::sun::star::uno::XComponentContext>& ctx);
#endif

Lang uiLanguage();
// stored choice: "Auto", "English", "Deutsch" or "Español"
std::string languageSetting();
void setLanguageSetting(const std::string& choice);

// translation of an English text: whole text, then {} patterns, then phrases inside the text
std::string tr(const std::string& english);
// whole-text translation only (list entries, which may also be variable names)
std::string trExact(const std::string& english);

} // namespace montecalc
