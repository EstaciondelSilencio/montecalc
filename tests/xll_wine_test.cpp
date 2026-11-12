// MonteCalc for Excel - Windows test of the ribbon object and the windows (run with Wine or on Windows).
// No Excel is needed: the simulation results are synthetic, the windows run without a workbook.
//
//   xll_wine_test ribbon <out.xml>      ribbon XML (as Office receives it) and its images
//   xll_wine_test window <kind> [sec]   opens a window (results, tornado, decision, scatter, wizard,
//                                       correlation, statistics, settings) for <sec> seconds
//   xll_wine_test message                an information message (MC FLO logo)
//   [lang]                              optional 4th argument: English, Deutsch or Espanol
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../core/simulation.h"
#include "../uno/engine.hpp"
#include "../uno/i18n.hpp"
#include "../uno/ui.hpp"
#include "../uno/windows.hpp"
#include "../xll/ribbon.hpp"
#include "../xll/win32ui.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>

using namespace montecalc;

static void synthetic() {
    auto& sim = mc::Simulation::instance();
    sim.beginRegistration();
    const char* inputs[] = {"MarketSize", "Share", "Price", "UnitCost", "Investment"};
    for (auto n : inputs) sim.setVariableInfo(n, mc::VarKind::Input, "synthetic", std::string("Model!") + n);
    sim.setVariableInfo("NPV", mc::VarKind::Output, "Output", "Model!B30");
    mc::Settings st;
    st.iterations = 3000;
    st.seed = 7;
    sim.prepare(st);
    std::mt19937_64 rng(7);
    std::normal_distribution<double> z(0, 1);
    int idx[6];
    for (int k = 0; k < 5; ++k) idx[k] = sim.variableIndex(inputs[k]);
    idx[5] = sim.variableIndex("NPV");
    for (int i = 0; i < st.iterations; ++i) {
        sim.setIteration(i);
        double m = 250000 + 40000 * z(rng), s = 0.08 + 0.015 * z(rng), p = 25 + 2 * z(rng) - 20 * (s - 0.08), c = 14.7 + 1.2 * z(rng) - m / 1e6,
               inv = 400000 + 60000 * z(rng);
        double npv = (m * s * (p - c) - 120000) * 3.8 - inv;
        double v[6] = {m, s, p, c, inv, npv};
        for (int k = 0; k < 6; ++k) sim.recordAt(idx[k], v[k]);
    }
    sim.finish("");
}

static void pump(int seconds) {
    DWORD end = GetTickCount() + DWORD(seconds) * 1000;
    MSG m;
    while (GetTickCount() < end) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(20);
    }
}

static VOID CALLBACK closeForm(HWND, UINT, UINT_PTR, DWORD) {
    HWND h = GetActiveWindow();
    if (h) PostMessageW(h, WM_CLOSE, 0, 0);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string mode = argv[1];
    CoInitialize(nullptr);
    if (mode == "ribbon") {
        std::string xml = ribbonXmlForTest();
        std::ofstream(argc > 2 ? argv[2] : "ribbon.xml") << xml;
        int ok = 0, n = 0;
        for (const char* id : {"start_32", "statistics_16", "dist_Normal_16", "logo_32", "functions_16", "bayes_16"}) {
            ++n;
            if (ribbonImageForTest(id)) ++ok;
        }
        // the class factory hands out the add-in with its interfaces
        IClassFactory* f = nullptr;
        const CLSID clsid = {0x8B2E7F31, 0x6C1A, 0x4E8B, {0x9A, 0x57, 0x2F, 0x4D, 0x9C, 0x0B, 0x6E, 0x11}};
        HRESULT hr = ribbonClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&f));
        IDispatch* d = nullptr;
        if (SUCCEEDED(hr) && f) hr = f->CreateInstance(nullptr, IID_IDispatch, reinterpret_cast<void**>(&d));
        DISPID id = 0;
        LPOLESTR names[] = {const_cast<LPOLESTR>(L"GetLabel")};
        HRESULT hn = d ? d->GetIDsOfNames(IID_NULL, names, 1, 0, &id) : E_FAIL;
        // the MC FLO logo: icon resource 1 (file, windows, messages) and the large ribbon button
        bool icon = FindResourceW(w32::module(), MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14)) && w32::logoIcon(false) != LoadIconW(nullptr, MAKEINTRESOURCEW(32512) /* IDI_APPLICATION */);
        bool logoButton = xml.find("id=\"AboutLogo\"") != std::string::npos;
        std::printf("ribbon xml %zu chars, images %d/%d, factory %s, callbacks %s, logo icon %s, logo button %s\n", xml.size(), ok, n,
                    SUCCEEDED(hr) && d ? "ok" : "FAILED", SUCCEEDED(hn) && id == 3 ? "ok" : "FAILED", icon ? "ok" : "FAILED", logoButton ? "ok" : "FAILED");
        if (d) d->Release();
        return ok == n && d && icon && logoButton ? 0 : 1;
    }
    if (mode == "message") {   // information message with the MC FLO logo
        SetTimer(nullptr, 1, 6000, closeForm);
        w32::messageBox(nullptr, L"Simulation finished: 3000 iterations in 1.2 s", L"MonteCalc", false);
        return 0;
    }
    if (mode == "window") {
        std::string kind = argc > 2 ? argv[2] : "results";
        int secs = argc > 3 ? std::atoi(argv[3]) : 6;
        std::string prevLang = languageSetting();
        if (argc > 4) setLanguageSetting(argv[4]);
        struct Restore {
            std::string l;
            ~Restore() { setLanguageSetting(l); }
        } restore{prevLang};
        synthetic();
        try {
        Ctx ctx;
        Frame frame;
        if (kind == "results") openResultWindow(ctx, frame, "NPV", "Histogram");
        else if (kind == "tornado") openResultWindow(ctx, frame, "NPV", "Tornado");
        else if (kind == "decision") openResultWindow(ctx, frame, "NPV", "Decision");
        else if (kind == "scatter") openResultWindow(ctx, frame, "NPV", "Scatter");
        else if (kind == "statistics") openResultWindow(ctx, frame, "NPV", "Statistics");
        else if (kind == "wizard") openWizard(ctx, frame, "PERT");
        else if (kind == "correlation") openCorrelationWindow(ctx, frame, "Price", "Share", -0.6, 0);
        else if (kind == "settings") {
            SetTimer(nullptr, 1, DWORD(secs) * 1000, closeForm);
            std::vector<FormField> f = {{"# iterations", "5000"}, {"Seed (0 = random, -1 = preview mode)", "2026"}, {"Worst case level", "0.05"},
                                        {"Use correlations", "", true, true},
                                        {"Language (MonteCalc windows and messages)", "Auto", false, false, {"Auto", "English", "Deutsch", "Espa\xC3\xB1ol"}}};
            showForm(ctx, "MonteCalc - Settings", f);
            return 0;
        }
        } catch (const std::exception& e) {
            std::printf("window %s: %s\n", kind.c_str(), e.what());   // e.g. no workbook for the correlation window
            return 1;
        }
        pump(secs);
        return 0;
    }
    return 2;
}
