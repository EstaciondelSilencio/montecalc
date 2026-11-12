// MonteCalc for Excel - the "MC FLO" ribbon tab.
//
// An XLL cannot add a ribbon tab itself; the .xll is therefore also an in-process COM add-in
// (IDTExtensibility2 + IRibbonExtensibility) that registers itself for the current user and is
// connected through Application.COMAddIns. The ribbon calls back through IDispatch by name.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <olectl.h>
#include <algorithm>
namespace Gdiplus {   // the GDI+ headers of Visual Studio use min/max, which NOMINMAX removes
using std::max;
using std::min;
}
#include <gdiplus.h>
#include <shlwapi.h>

#include "../core/flofuncs.h"
#include "../core/simulation.h"
#include "../uno/engine.hpp"
#include "../uno/i18n.hpp"
#include "../uno/model.hpp"
#include "../uno/util.hpp"
#include "com.hpp"
#include "ribbon.hpp"
#include "win32ui.hpp"

#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace montecalc {

using com::Disp;
using com::Var;
using com::wide;

// ------------------------------------------------------------------ the interfaces Office uses
MIDL_INTERFACE("B65AD801-ABAF-11D0-BB8B-00A0C90F2744")
IDTExtensibility2 : public IDispatch {
public:
    virtual HRESULT STDMETHODCALLTYPE OnConnection(IDispatch* Application, int ConnectMode, IDispatch* AddInInst, SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDisconnection(int RemoveMode, SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnAddInsUpdate(SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnStartupComplete(SAFEARRAY** custom) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnBeginShutdown(SAFEARRAY** custom) = 0;
};
MIDL_INTERFACE("000C0396-0000-0000-C000-000000000046")
IRibbonExtensibility : public IDispatch {
public:
    virtual HRESULT STDMETHODCALLTYPE GetCustomUI(BSTR RibbonID, BSTR* RibbonXml) = 0;
};
static const IID IID_IDTExtensibility2_ = {0xB65AD801, 0xABAF, 0x11D0, {0xBB, 0x8B, 0x00, 0xA0, 0xC9, 0x0F, 0x27, 0x44}};
static const IID IID_IRibbonExtensibility_ = {0x000C0396, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
// {8B2E7F31-6C1A-4E8B-9A57-2F4D9C0B6E11}
const CLSID kRibbonClsid = {0x8B2E7F31, 0x6C1A, 0x4E8B, {0x9A, 0x57, 0x2F, 0x4D, 0x9C, 0x0B, 0x6E, 0x11}};
const wchar_t* kRibbonClsidText = L"{8B2E7F31-6C1A-4E8B-9A57-2F4D9C0B6E11}";
const wchar_t* kProgId = L"MonteCalc.Ribbon";

namespace {

struct Icon { const char* name; std::vector<unsigned char> png; };
const std::vector<Icon>& icons() {
    static const std::vector<Icon> v = {
#include "icons.inc"
    };
    return v;
}

Disp g_ribbon;                       // IRibbonUI (Invalidate)
std::string g_iteration = "1";       // "Choose iteration"
std::map<std::string, std::string> g_selected;   // drop-down -> chosen variable

// ------------------------------------------------------------------ ribbon layout
struct Labels {
    std::map<std::string, std::string> text;   // control id -> English label
};
Labels& labels() {
    static Labels l;
    return l;
}

// ribbon ids must be XML names: commands with arguments ("Wizard?Function=Normal") go into tag
std::string safeId(const std::string& cmd) {
    std::string r;
    for (char c : cmd) r += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return r;
}

std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        default: r += c;
        }
    }
    return r;
}

// a button: id = command (Engine::run), label (translated through GetLabel), icon
std::string button(const std::string& id, const std::string& label, const std::string& icon, bool large = false) {
    labels().text[id] = label;
    return "<button id=\"" + safeId(id) + "\" tag=\"" + esc(id) + "\" getLabel=\"GetLabel\" image=\"" + icon + (large ? "_32" : "_16") +
           "\" onAction=\"OnAction\"" + (large ? " size=\"large\"" : "") + "/>";
}

// a button with a fixed (untranslated) label, e.g. a distribution or function name
std::string plainButton(const std::string& cmd, const std::string& label, const std::string& image) {
    return "<button id=\"" + safeId(cmd) + "\" tag=\"" + esc(cmd) + "\" label=\"" + esc(label) + "\"" + (image.empty() ? "" : " image=\"" + image + "\"") +
           " onAction=\"OnAction\"/>";
}

std::string menuOf(const std::string& id, const std::string& label, const std::string& icon, const std::string& items, bool large = false) {
    labels().text[id] = label;
    return "<menu id=\"" + safeId(id) + "\" tag=\"" + esc(id) + "\" getLabel=\"GetLabel\" image=\"" + icon + (large ? "_32" : "_16") + "\"" + (large ? " size=\"large\"" : "") +
           " itemSize=\"normal\">" + items + "</menu>";
}

std::string editBox(const std::string& id, const std::string& label) {
    labels().text[id] = label;
    return "<editBox id=\"" + id + "\" getLabel=\"GetLabel\" sizeString=\"000000000\" getText=\"GetText\" onChange=\"OnChange\"/>";
}

std::string checkBox(const std::string& id, const std::string& label) {
    labels().text[id] = label;
    return "<checkBox id=\"" + id + "\" getLabel=\"GetLabel\" getPressed=\"GetPressed\" onAction=\"OnCheck\"/>";
}

std::string dropDown(const std::string& id, const std::string& label) {
    labels().text[id] = label;
    return "<dropDown id=\"" + id + "\" getLabel=\"GetLabel\" sizeString=\"WWWWWWWWWWWW\" getItemCount=\"GetItemCount\" getItemLabel=\"GetItemLabel\" "
           "getSelectedItemIndex=\"GetSelectedIndex\" onAction=\"OnSelect\"/>";
}

// image: shown when the ribbon is too narrow and the group collapses into one button
std::string group(const std::string& id, const std::string& label, const std::string& body, const std::string& image = "") {
    labels().text[id] = label;
    return "<group id=\"" + id + "\" getLabel=\"GetLabel\"" + (image.empty() ? "" : " image=\"" + image + "\"") + ">" + body + "</group>";
}

std::string pickerMenu(const std::string& id, const std::string& label, const std::string& icon, int which) {
    std::vector<std::string> names;
    for (const auto& f : mc::floFunctions()) {
        bool ts = f.kind == mc::FloFunction::TimeSeries, disc = f.kind == mc::FloFunction::Dist && f.discrete;
        bool cont = f.kind == mc::FloFunction::Dist && !f.discrete;
        if (f.kind == mc::FloFunction::UserDefined) cont = true;
        if (f.kind == mc::FloFunction::DiscreteUserDefined) disc = true;
        if ((which == 0 && cont) || (which == 1 && disc) || (which == 2 && ts)) names.push_back(f.label);
    }
    std::string items;
    for (auto& n : names) {
        std::string img = "dist_" + n;
        bool known = false;
        for (auto& ic : icons()) if (std::string(ic.name) == img + "_16") known = true;
        items += plainButton("Wizard?Function=" + n, n, (known ? img : std::string("wizard")) + "_16");
    }
    return menuOf(id, label, icon, items);
}

std::string functionsMenu() {
    std::string cats;
    int k = 0;
    static const std::map<std::string, std::string> catIcons = {
        {"Descriptive Statistics", "statistics"}, {"Relationship functions", "correlation"}, {"Portfolio/Financial Functions", "portfolio"},
        {"Parameter-free statistics", "bootstrap"}, {"Classical Linear Regression", "regression"}, {"Generative/Bayesian statistics", "bayes"},
        {"Other functions", "functions"}};
    for (const auto& c : functionMenu()) {
        std::string items;
        for (const char* f : c.functions)
            items += plainButton("InsertFunction?Name=" + std::string(f), f, "");
        auto it = catIcons.find(c.name);
        cats += menuOf("fcat" + std::to_string(k++), c.name, it != catIcons.end() ? it->second : "functions", items);
    }
    cats += "<menuSeparator id=\"fsep\"/>";
    cats += button("FunctionWizard", "Function Wizard...", "functions");
    return menuOf("mFunctions", "Functions", "functions", cats, true);
}

std::string ribbonXml() {
    std::string g1 = group("gModel", "Model specification",
                           "<box id=\"bPick\" boxStyle=\"vertical\">" + pickerMenu("mCont", "Choose continuous variable", "dist_Normal", 0) +
                               pickerMenu("mDisc", "Choose discrete variable", "dist_Binomial", 1) +
                               pickerMenu("mTs", "Choose time series", "dist_TemporalS_ARmodel", 2) + "</box>" +
                               "<box id=\"bIns\" boxStyle=\"vertical\">" + button("Wizard", "Choose Wizard", "wizard") +
                               button("InsertCorrelation", "Insert correlation", "correlation") +
                               button("InsertOutput", "Insert output variable", "output") + "</box>" + "<box id=\"bReg\" boxStyle=\"vertical\">" +
                               button("Register", "Register variables", "register") + button("Lock", "Lock/Unlock", "lock") +
                               button("Fit", "Fit distribution", "fit") + "</box>");
    std::string g2 = group("gInfo", "Information",
                           button("InfluenceDiagram", "Variables", "influence", true) + functionsMenu() + "<box id=\"bInfo\" boxStyle=\"vertical\">" +
                               button("Model", "Model", "model") + button("Show", "Variable list", "variables") +
                               button("Mentor", "AI mentor", "mentor") + "</box>");
    std::string g3 = group("gSim", "Simulation",
                           "<box id=\"bCases\" boxStyle=\"vertical\">" + editBox("eBest", "Best-case:") + editBox("eReal", "Real-case:") +
                               editBox("eWorst", "Worst-case:") + "</box>" + "<box id=\"bIter\" boxStyle=\"vertical\">" +
                               editBox("eIterations", "Iterations:") + checkBox("cUseCorr", "Use correlations") +
                               checkBox("cVisualize", "Visualize simulation") + "</box>" + "<box id=\"bOpt\" boxStyle=\"vertical\">" +
                               checkBox("cMemory", "Memory Calculation") + checkBox("cPreview", "Preview window") + "</box>" +
                               "<box id=\"bRun\" boxStyle=\"vertical\">" +
                               button("MultipleSimulation", "Multiple simulation", "multiple") + button("Start", "Start", "start") +
                               button("Reset", "Reset", "reset") + "</box>" + button("Settings", "Settings...", "settings", true));
    std::string g4 = group("gStat", "Statistics",
                           button("Statistics", "Show statistical parameters", "statistics", true) + "<box id=\"bStat\" boxStyle=\"vertical\">" +
                               dropDown("dVar", "Variable") + button("Results", "Result window", "histogram") +
                               button("SelectVariant", "Select variant", "variant") + "</box>");
    std::string g5 = group("gCorr", "Correlations and sensitivities",
                           "<box id=\"bCorr\" boxStyle=\"vertical\">" + dropDown("dX", "X-variable") + dropDown("dY", "Y-variable") +
                               button("CalcCorrelations", "Calc correlations", "calccorr") + "</box>" +
                               button("Correlogram", "Correlogram", "correlogram", true));
    std::string g6 = group("gIter", "Individual analysis of iteration",
                           "<box id=\"bItr\" boxStyle=\"vertical\">" + editBox("eIteration", "Choose iteration:") +
                               button("Replay", "Analyze selected iteration", "iteration") + button("Expected", "Back to expected values", "expected") +
                               "</box>");
    std::string g7 = group("gResults", "Results",
                           button("SaveResults", "Close and save results", "save", true) + "<box id=\"bRes\" boxStyle=\"vertical\">" +
                               button("StoreResults", "Store results", "store") + button("ChangeHistogram", "Change histogram", "histogram") + "</box>");
    std::string other = button("SortAsc", "Sort asc", "sortasc") + button("SortDesc", "Sort des", "sortdesc") +
                        button("ModelCheck", "Excel-Check (model check)", "check") + button("ForecastTool", "Forecast-Tool...", "forecast") +
                        button("StartBatch", "Start Batch", "batch") + button("HideFormulas", "Hide formulas", "hide") +
                        button("LoadFormulas", "Load formulas", "load") + button("Variables", "Variables (text summary)", "variables") +
                        button("WriteContext", "FLO_Context sheet", "context") + button("Preview", "Preview (random values)", "preview") +
                        "<menuSeparator id=\"osep\"/>" +
                        menuOf("mLang", "Language", "about",
                               button("Lang?Choice=Auto", "Auto", "about") + plainButton("Lang?Choice=English", "English", "") +
                                   plainButton("Lang?Choice=Deutsch", "Deutsch", "") + plainButton("Lang?Choice=Espanol", "Espa\xC3\xB1ol", "")) +
                        button("About", "About MonteCalc", "logo");
    labels().text["AboutLogo"] = "About MonteCalc";
    std::string logo = "<button id=\"AboutLogo\" tag=\"About\" getLabel=\"GetLabel\" image=\"logo_32\" size=\"large\" onAction=\"OnAction\"/>";
    std::string g8 = group("gOther", "Other", menuOf("mOther", "Other", "settings", other, true) + logo, "logo_32");
    return "<customUI xmlns=\"http://schemas.microsoft.com/office/2009/07/customui\" onLoad=\"OnLoad\" loadImage=\"LoadImage\">"
           "<ribbon><tabs><tab id=\"tabMcFlo\" label=\"MC FLO\">" +
           g1 + g2 + g3 + g4 + g5 + g6 + g7 + g8 + "</tab></tabs></ribbon></customUI>";
}

// ------------------------------------------------------------------ images
IPictureDisp* picture(const std::string& name) {
    w32::ensureGdiplus();
    const Icon* icon = nullptr;
    for (auto& ic : icons())
        if (name == ic.name) icon = &ic;
    if (!icon) return nullptr;
    IStream* st = SHCreateMemStream(icon->png.data(), UINT(icon->png.size()));
    if (!st) return nullptr;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(st);
    st->Release();
    if (!bmp) return nullptr;
    HBITMAP hb = nullptr;
    bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hb);
    delete bmp;
    if (!hb) return nullptr;
    PICTDESC pd{};
    pd.cbSizeofstruct = sizeof pd;
    pd.picType = PICTYPE_BITMAP;
    pd.bmp.hbitmap = hb;
    IPictureDisp* pic = nullptr;
    if (FAILED(OleCreatePictureIndirect(&pd, IID_IPictureDisp, TRUE, reinterpret_cast<void**>(&pic)))) {
        DeleteObject(hb);
        return nullptr;
    }
    return pic;
}

// ------------------------------------------------------------------ state shown in the ribbon
const char* settingKey(const std::string& id) {
    if (id == "eBest") return "MonteCalc.Best";
    if (id == "eReal") return "MonteCalc.Real";
    if (id == "eWorst") return "MonteCalc.Worst";
    if (id == "eIterations") return "MonteCalc.Iterations";
    if (id == "cUseCorr") return "MonteCalc.UseCorrelations";
    if (id == "cVisualize") return "MonteCalc.Visualize";
    if (id == "cMemory") return "MonteCalc.Memory";
    if (id == "cPreview") return "MonteCalc.Preview";
    return nullptr;
}

std::string settingText(const std::string& id) {
    ModelDoc d;
    if (!d.valid()) return "";
    RunOptions o = loadOptions(d);
    char b[64];
    if (id == "eBest") std::snprintf(b, sizeof b, "%g", o.s.best);
    else if (id == "eReal") std::snprintf(b, sizeof b, "%g", o.s.real);
    else if (id == "eWorst") std::snprintf(b, sizeof b, "%g", o.s.worst);
    else if (id == "eIterations") std::snprintf(b, sizeof b, "%d", o.s.iterations);
    else if (id == "eIteration") return g_iteration;
    else return "";
    return b;
}

std::vector<std::string> variableNames(const std::string& id) {
    std::vector<std::string> r;
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) return r;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& v : sim.resultVariables()) {
            bool out = v.kind == mc::VarKind::Output;
            // statistics and Y: outputs first; X: inputs first
            bool first = id == "dX" ? !out : out;
            if (first == (pass == 0)) r.push_back(v.name);
        }
    return r;
}

std::string chosenVariable(const std::string& id) {
    auto names = variableNames(id);
    auto it = g_selected.find(id);
    if (it != g_selected.end())
        for (auto& n : names) if (n == it->second) return n;
    return names.empty() ? std::string() : names[0];
}

void invalidate() {
    if (!g_ribbon.valid()) return;
    try { g_ribbon.call("Invalidate"); } catch (...) {}
}

void act(const std::string& id) {
    Engine e;
    std::vector<::com::sun::star::beans::PropertyValue> args;
    auto arg = [&](const char* n, const ::com::sun::star::uno::Any& v) {
        ::com::sun::star::beans::PropertyValue p;
        p.Name = OUString(n);
        p.Value = v;
        args.push_back(p);
    };
    if (id.rfind("Lang?Choice=", 0) == 0) {
        std::string c = id.substr(12);
        setLanguageSetting(c == "Espanol" ? "Espa\xC3\xB1ol" : c);
        ribbonLanguageChanged();
        return;
    }
    if (id == "FunctionWizard") {
        com::application().obj("Dialogs", {Var(450)}).call("Show");
        return;
    }
    if (id == "Statistics" || id == "Results") {
        std::string v = chosenVariable("dVar");
        arg("Variable", ::com::sun::star::uno::Any(ou(v)));
        if (id == "Statistics") arg("View", ::com::sun::star::uno::Any(OUString("Statistics")));
        e.run("Results", args);
        return;
    }
    if (id == "CalcCorrelations") {
        std::string x = chosenVariable("dX"), y = chosenVariable("dY");
        if (!x.empty() && !y.empty()) {
            arg("X", ::com::sun::star::uno::Any(ou(x)));
            arg("Y", ::com::sun::star::uno::Any(ou(y)));
        }
        e.run("CalcCorrelations", args);
        invalidate();
        return;
    }
    if (id == "Replay") {
        bool ok;
        double it = parseNumber(g_iteration, ok);
        if (ok && it >= 1) arg("Iteration", ::com::sun::star::uno::Any(it));
        e.run("Replay", args);
        return;
    }
    e.run(id, args);
    if (id == "Start" || id == "Reset" || id == "Settings" || id == "MultipleSimulation" || id == "SelectVariant") invalidate();
}

// ------------------------------------------------------------------ the COM object
class RibbonAddIn : public IDTExtensibility2, public IRibbonExtensibility {
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IDTExtensibility2_) *out = static_cast<IDTExtensibility2*>(this);
        else if (riid == IID_IRibbonExtensibility_) *out = static_cast<IRibbonExtensibility*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return ULONG(r);
    }
    // IDispatch: the ribbon calls the callbacks by name
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override { *n = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* names, UINT count, LCID, DISPID* ids) override {
        static const std::map<std::wstring, DISPID> m = {
            {L"OnLoad", 1}, {L"LoadImage", 2}, {L"GetLabel", 3}, {L"OnAction", 4}, {L"OnCheck", 5}, {L"GetPressed", 6}, {L"GetText", 7},
            {L"OnChange", 8}, {L"GetItemCount", 9}, {L"GetItemLabel", 10}, {L"GetSelectedIndex", 11}, {L"OnSelect", 12}};
        HRESULT hr = S_OK;
        for (UINT i = 0; i < count; ++i) {
            auto it = m.find(names[i]);
            ids[i] = it != m.end() ? it->second : DISPID_UNKNOWN;
            if (it == m.end()) hr = DISP_E_UNKNOWNNAME;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* p, VARIANT* result, EXCEPINFO*, UINT*) override {
        // arguments arrive in reverse order
        auto arg = [&](UINT fromLeft) -> Var {
            UINT n = p ? p->cArgs : 0;
            if (fromLeft >= n) return Var();
            VARIANT v;
            VariantInit(&v);
            VariantCopyInd(&v, &p->rgvarg[n - 1 - fromLeft]);
            Var r(v);
            VariantClear(&v);
            return r;
        };
        auto controlId = [&]() -> std::string {   // the command is in tag (ids are XML names)
            Disp c = arg(0).disp();
            try {
                std::string t = c.get("Tag").str();
                if (!t.empty()) return t;
                return c.get("Id").str();
            } catch (...) {
                return {};
            }
        };
        auto setText = [&](const std::string& s) {
            if (!result) return;
            VariantInit(result);
            result->vt = VT_BSTR;
            std::wstring w = wide(s);
            result->bstrVal = SysAllocStringLen(w.data(), UINT(w.size()));
        };
        try {
            switch (id) {
            case 1:   // OnLoad(ribbon)
                g_ribbon = arg(0).disp();
                ribbonLoaded();
                return S_OK;
            case 2: {   // LoadImage(imageId) -> IPictureDisp
                IPictureDisp* pic = picture(arg(0).str());
                if (result && pic) {
                    VariantInit(result);
                    result->vt = VT_DISPATCH;
                    result->pdispVal = pic;
                }
                return S_OK;
            }
            case 3: {   // GetLabel(control)
                std::string cid = controlId();
                auto it = labels().text.find(cid);
                setText(it != labels().text.end() ? tr(it->second) : cid);
                return S_OK;
            }
            case 4:   // OnAction(control)
                act(controlId());
                return S_OK;
            case 5: {   // OnCheck(control, pressed)
                std::string cid = controlId();
                bool on = arg(1).boolean();
                ModelDoc d;
                if (d.valid() && settingKey(cid)) d.setProperty(settingKey(cid), ::com::sun::star::uno::Any(on ? 1.0 : 0.0));
                return S_OK;
            }
            case 6: {   // GetPressed(control)
                std::string cid = controlId();
                ModelDoc d;
                bool on = false;
                if (d.valid()) {
                    RunOptions o = loadOptions(d);
                    on = cid == "cUseCorr"     ? o.s.useCorrelations
                         : cid == "cVisualize" ? o.visualize
                         : cid == "cMemory"    ? o.memory
                         : cid == "cPreview"   ? o.preview
                                               : false;
                }
                if (result) { VariantInit(result); result->vt = VT_BOOL; result->boolVal = on ? VARIANT_TRUE : VARIANT_FALSE; }
                return S_OK;
            }
            case 7:   // GetText(control)
                setText(settingText(controlId()));
                return S_OK;
            case 8: {   // OnChange(control, text)
                std::string cid = controlId(), text = arg(1).str();
                if (cid == "eIteration") { g_iteration = text; return S_OK; }
                bool ok;
                for (auto& c : text) if (c == ',') c = '.';
                double v = parseNumber(text, ok);
                ModelDoc d;
                if (!d.valid() || !settingKey(cid)) return S_OK;
                bool valid = ok && (cid == "eIterations" ? v >= 1 && v <= 1e8 : v > 0 && v < 1);
                if (valid) d.setProperty(settingKey(cid), ::com::sun::star::uno::Any(cid == "eIterations" ? std::round(v) : v));
                else MessageBoxW(w32::excelWindow(), wide(tr("Please enter numbers only.")).c_str(), L"MonteCalc", MB_OK | MB_ICONERROR);
                invalidate();
                return S_OK;
            }
            case 9: {   // GetItemCount(control)
                int n = int(variableNames(controlId()).size());
                if (result) { VariantInit(result); result->vt = VT_I4; result->lVal = n; }
                return S_OK;
            }
            case 10: {   // GetItemLabel(control, index)
                auto names = variableNames(controlId());
                int i = int(arg(1).num(0));
                setText(i >= 0 && i < int(names.size()) ? names[size_t(i)] : "");
                return S_OK;
            }
            case 11: {   // GetSelectedIndex(control)
                std::string cid = controlId();
                auto names = variableNames(cid);
                std::string chosen = chosenVariable(cid);
                int idx = 0;
                for (size_t i = 0; i < names.size(); ++i) if (names[i] == chosen) idx = int(i);
                if (result) { VariantInit(result); result->vt = VT_I4; result->lVal = idx; }
                return S_OK;
            }
            case 12: {   // OnSelect(control, selectedId, selectedIndex)
                std::string cid = controlId();
                auto names = variableNames(cid);
                int i = int(arg(2).num(0));
                if (i >= 0 && i < int(names.size())) g_selected[cid] = names[size_t(i)];
                return S_OK;
            }
            default: return DISP_E_MEMBERNOTFOUND;
            }
        } catch (const std::exception& e) {
            MessageBoxW(w32::excelWindow(), wide(tr(e.what())).c_str(), L"MonteCalc", MB_OK | MB_ICONERROR);
            return S_OK;
        }
    }
    // IDTExtensibility2
    HRESULT STDMETHODCALLTYPE OnConnection(IDispatch* app, int, IDispatch*, SAFEARRAY**) override {
        if (app) com::setApplication(app);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDisconnection(int, SAFEARRAY**) override {
        g_ribbon = Disp();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnAddInsUpdate(SAFEARRAY**) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStartupComplete(SAFEARRAY**) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnBeginShutdown(SAFEARRAY**) override { return S_OK; }
    // IRibbonExtensibility
    HRESULT STDMETHODCALLTYPE GetCustomUI(BSTR, BSTR* xml) override {
        std::wstring w = wide(ribbonXml());
        *xml = SysAllocStringLen(w.data(), UINT(w.size()));
        return S_OK;
    }

private:
    LONG refs_ = 1;
};

class Factory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* a = new RibbonAddIn();
        HRESULT hr = a->QueryInterface(riid, out);
        a->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

bool setKey(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY k;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

bool setDword(HKEY root, const std::wstring& path, const wchar_t* name, DWORD value) {
    HKEY k;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

} // namespace

// ------------------------------------------------------------------ public functions (ribbon.hpp)

void ribbonInvalidate() { invalidate(); }

std::string ribbonXmlForTest() { return ribbonXml(); }

bool ribbonImageForTest(const std::string& id) {
    IPictureDisp* p = picture(id);
    if (!p) return false;
    p->Release();
    return true;
}

HRESULT ribbonClassObject(REFCLSID clsid, REFIID riid, void** out) {
    static Factory f;
    if (clsid != kRibbonClsid) return CLASS_E_CLASSNOTAVAILABLE;
    return f.QueryInterface(riid, out);
}

bool registerRibbon() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(w32::module(), path, MAX_PATH);
    std::wstring clsKey = std::wstring(L"Software\\Classes\\CLSID\\") + kRibbonClsidText;
    bool ok = setKey(HKEY_CURRENT_USER, clsKey, nullptr, L"MonteCalc ribbon (MC FLO)") &&
              setKey(HKEY_CURRENT_USER, clsKey + L"\\InprocServer32", nullptr, path) &&
              setKey(HKEY_CURRENT_USER, clsKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment") &&
              setKey(HKEY_CURRENT_USER, clsKey + L"\\ProgID", nullptr, kProgId) &&
              setKey(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + kProgId + L"\\CLSID", nullptr, kRibbonClsidText);
    std::wstring addin = std::wstring(L"Software\\Microsoft\\Office\\Excel\\Addins\\") + kProgId;
    ok = ok && setKey(HKEY_CURRENT_USER, addin, L"FriendlyName", L"MonteCalc (MC FLO ribbon)") &&
         setKey(HKEY_CURRENT_USER, addin, L"Description", L"The MC FLO tab of the MonteCalc add-in") &&
         setDword(HKEY_CURRENT_USER, addin, L"LoadBehavior", 3);
    return ok;
}

void unregisterRibbon() {
    try {
        if (com::haveApplication()) com::application().obj("COMAddIns").obj("Item", {Var(std::string("MonteCalc.Ribbon"))}).put("Connect", Var(false));
    } catch (...) {
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Microsoft\\Office\\Excel\\Addins\\") + kProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + kProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + kRibbonClsidText).c_str());
}

bool connectRibbon() {
    if (!com::haveApplication() && !com::findApplication(w32::excelWindow())) return false;
    try {
        Disp addins = com::application().obj("COMAddIns");
        addins.call("Update");
        Disp a = addins.obj("Item", {Var(std::string("MonteCalc.Ribbon"))});
        if (!a.get("Connect").boolean()) a.put("Connect", Var(true));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace montecalc

// COM entry points of the .xll (the ribbon add-in); exported through montecalc_xll.def
STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    return montecalc::ribbonClassObject(clsid, riid, out);
}
STDAPI DllCanUnloadNow() { return S_FALSE; }
#endif
