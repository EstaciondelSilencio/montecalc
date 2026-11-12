// MonteCalc for Excel - the "MC FLO" ribbon tab (COM add-in inside the .xll)
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace montecalc {
bool registerRibbon();      // the COM add-in for the current user (HKCU)
void unregisterRibbon();
bool connectRibbon();       // Application.COMAddIns("MonteCalc.Ribbon").Connect = True
void ribbonInvalidate();    // labels, settings and variable lists again
HRESULT ribbonClassObject(REFCLSID clsid, REFIID riid, void** out);
// implemented in xll.cpp
void ribbonLoaded();
void ribbonLanguageChanged();
// tests
std::string ribbonXmlForTest();
bool ribbonImageForTest(const std::string& id);
} // namespace montecalc
#endif
