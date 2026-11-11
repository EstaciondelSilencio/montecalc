// MonteCalc - test of the Excel XLL (xll/xll.cpp) without Excel: a mock implements the C API calls
// the add-in makes (registration, menu, caller, recalculation, cell values, names, dialogs) and a
// small workbook whose cells call the exported functions.
//
// Build and run: make xlltest
#include "../xll/xlapi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

typedef int (*EXCEL12PROC)(int xlfn, int coper, LPXLOPER12* rgpxloper12, LPXLOPER12 xloper12Res);
extern "C" {
void mcTestSetExcel(EXCEL12PROC p);
int xlAutoOpen();
int mcStart();
int mcReset();
int mcSettings();
int mcLangGerman();
int mcLangEnglish();
LPXLOPER12 mc_FLOsimula_Normal(LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_FLOsimula_Uniform(LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_FLOsimula_output(LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_FMC_Average(LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_FMC_StdS(LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_FMC_Iterations();
LPXLOPER12 mc_FMC_GetInputName(LPXLOPER12);
LPXLOPER12 mc_Simula_Normal_MC(LPXLOPER12, LPXLOPER12, LPXLOPER12);
LPXLOPER12 mc_Fmc_Name(LPXLOPER12);
}

namespace {

int failures = 0, checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL %s\n", what.c_str()); }
    else std::printf("  ok   %s\n", what.c_str());
}

// ---------------------------------------------------------------- operands
std::vector<std::vector<XCHAR>> g_strings;   // memory of strings made here (kept for the test's life)

XLOPER12 num(double d) { XLOPER12 x{}; x.xltype = xltypeNum; x.val.num = d; return x; }
XLOPER12 missing() { XLOPER12 x{}; x.xltype = xltypeMissing; return x; }
XLOPER12 err(int e) { XLOPER12 x{}; x.xltype = xltypeErr; x.val.err = e; return x; }
XLOPER12 str(const std::string& s) {
    std::vector<XCHAR> b(s.size() + 1);
    b[0] = XCHAR(s.size());
    for (size_t i = 0; i < s.size(); ++i) b[i + 1] = XCHAR(static_cast<unsigned char>(s[i]));
    g_strings.push_back(b);
    XLOPER12 x{};
    x.xltype = xltypeStr;
    x.val.str = g_strings.back().data();
    return x;
}
std::string text(const XLOPER12& x) {
    std::string r;
    if ((x.xltype & 0xFFF) != xltypeStr) return r;
    for (int i = 1; i <= int(uint16_t(x.val.str[0])); ++i) {
        uint16_t c = uint16_t(x.val.str[i]);
        if (c < 0x80) r += char(c);
        else if (c < 0x800) { r += char(0xC0 | (c >> 6)); r += char(0x80 | (c & 0x3F)); }
        else { r += char(0xE0 | (c >> 12)); r += char(0x80 | ((c >> 6) & 0x3F)); r += char(0x80 | (c & 0x3F)); }
    }
    return r;
}
double value(LPXLOPER12 x) { return x && (x->xltype & 0xFFF) == xltypeNum ? x->val.num : std::nan(""); }

// ---------------------------------------------------------------- the mock workbook
const IDSHEET kSheet = 7;
struct Cell { std::function<double()> formula; double value = 0; std::string formulaText; };
std::map<std::pair<int, int>, Cell> g_cells;   // (row, col)
std::pair<int, int> g_current{-1, -1};          // the cell being calculated (xlfCaller)
std::map<std::string, double> g_names;          // defined names
std::deque<double> g_inputs;                    // answers of INPUT dialogs
std::vector<std::string> g_alerts;
std::map<std::string, std::string> g_registered;   // Excel name -> type text
std::map<std::string, std::string> g_categories;   // Excel name -> category
std::vector<std::vector<std::string>> g_menu;
XLMREF12 g_callerRef;

void recalc() {
    for (auto& [pos, c] : g_cells) {
        if (!c.formula) continue;
        g_current = pos;
        c.value = c.formula();
        g_current = {-1, -1};
    }
}

double cellValue(int row, int col) { return g_cells[{row, col}].value; }

int mock(int fn, int count, LPXLOPER12* a, LPXLOPER12 res) {
    static XLOPER12 none;
    if (!res) res = &none;
    res->xltype = xltypeNil;
    switch (fn) {
    case xlFree: return xlretSuccess;
    case xlGetName: *res = str("C:\\MonteCalc\\MonteCalc.xll"); return xlretSuccess;
    case xlfRegister: {
        if (count >= 4) {
            std::string name = text(*a[3]);
            g_registered[name] = text(*a[2]);
            if (count >= 7) g_categories[name] = text(*a[6]);
        }
        *res = num(double(g_registered.size()));
        return xlretSuccess;
    }
    case xlfAddMenu: {
        g_menu.clear();
        XLOPER12& m = *a[1];
        for (RW r = 0; r < m.val.array.rows; ++r) {
            std::vector<std::string> row;
            for (COL c = 0; c < m.val.array.columns; ++c) row.push_back(text(m.val.array.lparray[r * m.val.array.columns + c]));
            g_menu.push_back(row);
        }
        return xlretSuccess;
    }
    case xlfDeleteMenu: return xlretSuccess;
    case xlfCaller: {
        if (g_current.first < 0) { *res = err(xlerrRef); return xlretSuccess; }
        g_callerRef.count = 1;
        g_callerRef.reftbl[0] = {g_current.first, g_current.first, g_current.second, g_current.second};
        res->xltype = xltypeRef;
        res->val.mref.lpmref = &g_callerRef;
        res->val.mref.idSheet = kSheet;
        return xlretSuccess;
    }
    case xlSheetNm: *res = str("[Book1.xlsx]Model"); return xlretSuccess;
    case xlCoerce: {
        XLOPER12& r = *a[0];
        if ((r.xltype & 0xFFF) != xltypeRef) { *res = r; return xlretSuccess; }
        auto& ref = r.val.mref.lpmref->reftbl[0];
        auto it = g_cells.find({ref.rwFirst, ref.colFirst});
        if (it == g_cells.end()) { res->xltype = xltypeNil; return xlretSuccess; }
        *res = num(it->second.value);
        return xlretSuccess;
    }
    case xlcCalculateNow: recalc(); return xlretSuccess;
    case xlcEcho:
    case xlcMessage: return xlretSuccess;
    case xlAbort: res->xltype = xltypeBool; res->val.xbool = 0; return xlretSuccess;
    case xlcAlert: g_alerts.push_back(text(*a[0])); return xlretSuccess;
    case xlfEvaluate: {
        auto it = g_names.find(text(*a[0]));
        *res = it == g_names.end() ? err(xlerrName) : num(it->second);
        return xlretSuccess;
    }
    case xlcDefineName: {
        std::string refers = text(*a[1]);
        g_names[text(*a[0])] = std::atof(refers.c_str() + (refers[0] == '=' ? 1 : 0));
        return xlretSuccess;
    }
    case xlfInput: {
        if (g_inputs.empty()) { res->xltype = xltypeBool; res->val.xbool = 0; return xlretSuccess; }
        *res = num(g_inputs.front());
        g_inputs.pop_front();
        return xlretSuccess;
    }
    case xlfGetCell: {   // GET.CELL(6, ref): the formula
        auto& ref = a[1]->val.mref.lpmref->reftbl[0];
        *res = str(g_cells[{ref.rwFirst, ref.colFirst}].formulaText);
        return xlretSuccess;
    }
    default: return xlretInvXlfn;
    }
}

} // namespace

int main() {
    // the language file lives in <home>/Documents
    std::string home = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/montecalc_xll_test";
    std::system(("mkdir -p '" + home + "/Documents'").c_str());
    setenv("HOME", home.c_str(), 1);
    { std::ofstream(home + "/Documents/MC_FLO_language.txt") << "English\n"; }
    mcTestSetExcel(mock);

    std::printf("registration\n");
    check(xlAutoOpen() == 1, "xlAutoOpen");
    check(g_registered.size() >= 178 + 9, "178 functions and 9 commands registered (" + std::to_string(g_registered.size()) + ")");
    check(g_registered["FLOsimula_Normal"] == "QQQQQQQQ!", "FLOsimula_Normal: 7 arguments, volatile");
    check(g_registered["fmc_Name"] == "QU#", "fmc_Name takes a reference (macro-sheet function)");
    check(g_registered["MCFLO.Start"] == "J", "command MCFLO.Start");
    check(g_categories["fMC_Average"] == "Descriptive Statistics", "fMC_Average in the category Descriptive Statistics");
    check(!g_menu.empty() && g_menu[0][0] == "MC FLO" && g_menu[1][0] == "Start simulation" && g_menu[1][1] == "MCFLO.Start",
          "menu MC FLO with Start simulation");

    std::printf("simulation\n");
    // B1 = FLOsimula_Normal(100;10;"X"), B2 = FLOsimula_Uniform(0;1) (unnamed), B3 = B1*2+B2+FLOsimula_output("Y")
    XLOPER12 m = missing();
    g_cells[{0, 1}] = {[&] { XLOPER12 a = num(100), b = num(10), n = str("X"); return value(mc_FLOsimula_Normal(&a, &b, &n, &m, &m, &m, &m)); }, 0,
                       "=FLOsimula_Normal(100,10,\"X\")"};
    g_cells[{1, 1}] = {[&] { XLOPER12 a = num(0), b = num(1); return value(mc_FLOsimula_Uniform(&a, &b, &m, &m, &m, &m, &m)); }, 0,
                       "=FLOsimula_Uniform(0,1)"};
    g_cells[{2, 1}] = {[&] { XLOPER12 n = str("Y"); return cellValue(0, 1) * 2 + cellValue(1, 1) + value(mc_FLOsimula_output(&n, &m)); }, 0,
                       "=B1*2+B2+FLOsimula_output(\"Y\")"};
    g_cells[{2, 0}] = {nullptr, 0, ""};
    recalc();
    check(std::fabs(cellValue(0, 1) - 100) < 1e-9 && std::fabs(cellValue(2, 1) - 200.5) < 1e-9, "expected values before the simulation");
    g_names["MonteCalc_Iterations"] = 2000;
    g_names["MonteCalc_Seed"] = 42;
    check(mcStart() == 1, "MCFLO.Start");
    check(!g_alerts.empty() && g_alerts.back().find("Simulation finished: 2000 iterations") != std::string::npos,
          "summary: " + (g_alerts.empty() ? std::string() : g_alerts.back().substr(0, 60)));
    XLOPER12 y = str("Y"), x = str("X");
    double avgY = value(mc_FMC_Average(&y, &m)), sdX = value(mc_FMC_StdS(&x, &m));
    check(std::fabs(avgY - 200.5) < 1.5, "fMC_Average(\"Y\") = " + std::to_string(avgY));
    check(std::fabs(sdX - 10) < 0.6, "fMC_StdS(\"X\") = " + std::to_string(sdX));
    check(value(mc_FMC_Iterations()) == 2000, "fMC_Iterations() = 2000");
    XLOPER12 two = num(2);
    std::string in2 = text(*mc_FMC_GetInputName(&two));
    check(in2 == "Model_B2", "the unnamed input is named after its cell: " + in2);
    check(std::fabs(cellValue(0, 1) - 100) < 1e-9, "expected values again after the simulation");

    std::printf("arguments and results\n");
    XLOPER12 e = err(xlerrDiv0);
    LPXLOPER12 r = mc_FMC_Average(&e, &m);
    check((r->xltype & 0xFFF) == xltypeErr && r->val.err == xlerrDiv0, "an error argument gives that error");
    XLOPER12 unknown = str("nope");
    r = mc_FMC_Average(&unknown, &m);
    check((r->xltype & 0xFFF) == xltypeErr && r->val.err == xlerrValue, "an unknown variable gives #VALUE!");
    XLOPER12 mean = num(5), sd = num(1), cnt = num(4);
    r = mc_Simula_Normal_MC(&mean, &sd, &cnt);
    check((r->xltype & 0xFFF) == xltypeMulti && r->val.array.rows == 4 && r->val.array.columns == 1, "_simula_Normal_MC returns a 4 x 1 array");
    XLMREF12 mr{};
    mr.count = 1;
    mr.reftbl[0] = {0, 0, 1, 1};
    XLOPER12 ref{};
    ref.xltype = xltypeRef;
    ref.val.mref.lpmref = &mr;
    ref.val.mref.idSheet = kSheet;
    check(text(*mc_Fmc_Name(&ref)) == "X", "fmc_Name(B1) = X");

    std::printf("settings and language\n");
    g_inputs = {500, 7, 0.1, 0.5, 0.9, 1};
    check(mcSettings() == 1 && g_names["MonteCalc_Iterations"] == 500 && g_names["MonteCalc_Worst"] == 0.1, "Settings stores the values in the workbook");
    check(mcLangGerman() == 1, "language Deutsch");
    check(g_menu.size() > 1 && g_menu[1][0] == "Starte Simulation", "menu in German: " + (g_menu.size() > 1 ? g_menu[1][0] : std::string()));
    check(g_categories["fMC_Average"] == "Deskriptive Statistik", "Function Wizard category in German: " + g_categories["fMC_Average"]);
    g_alerts.clear();
    mcStart();
    check(!g_alerts.empty() && g_alerts.back().find("Simulation finished") == std::string::npos &&
              g_alerts.back().find("500") != std::string::npos && g_alerts.back().find("Mittelwert") != std::string::npos,
          "summary in German: " + (g_alerts.empty() ? std::string() : g_alerts.back().substr(0, 50)));
    g_alerts.clear();
    auto keep = g_cells[{2, 1}].formula;
    g_cells[{2, 1}].formula = [&] { return cellValue(0, 1); };   // no output any more
    mcStart();
    check(!g_alerts.empty() && g_alerts.back().find("Keine Outputvariable gefunden") != std::string::npos,
          "message without output variable: " + (g_alerts.empty() ? std::string() : g_alerts.back().substr(0, 70)));
    g_cells[{2, 1}].formula = keep;
    mcLangEnglish();
    check(mcReset() == 1 && std::isnan(value(mc_FMC_Average(&y, &m))) == false, "Reset");

    std::printf("\n%s: %d of %d checks failed\n", failures ? "FAILED" : "PASSED", failures, checks);
    return failures ? 1 : 0;
}
