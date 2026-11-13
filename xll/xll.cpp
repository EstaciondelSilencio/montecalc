// MonteCalc for Microsoft Excel - XLL add-in (Excel 2007 or later, 64 and 32 bit).
//
// The spreadsheet functions are the same code as in the LibreOffice extension (uno/addin.cpp with
// xll/uno_shim.hpp); this file connects them to Excel's C API:
//   - registers every FLOsimula_ / fMC_ function (Function Wizard categories as on the MC FLO ribbon),
//   - converts arguments and results,
//   - runs the simulation (commands MCFLO.Start, MCFLO.Reset, MCFLO.Settings, ...): every
//     FLOsimula_ function remembers its cell during a registration pass, then each iteration
//     recalculates the workbook and records the variables' cell values,
//   - adds the "MC FLO" menu (Add-ins tab) in English, German or Spanish.
#include "xlapi.h"

#include "../core/simulation.h"
#include "../core/stats.h"
#include "../core/formula.h"
#include "../uno/addin.hpp"
#include "../uno/engine.hpp"
#include "../uno/i18n.hpp"
#include "../uno/model.hpp"
#ifdef _WIN32
#include "../uno/ui.hpp"        // before com.hpp (montecalc::com)
#include "../uno/windows.hpp"
#include "com.hpp"
#include "ribbon.hpp"
#include "win32ui.hpp"
#endif

#include <chrono>
#include <tuple>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace montecalc;
using ::com::sun::star::uno::Any;
using ::com::sun::star::uno::Sequence;

// ============================================================ the call back into Excel

#ifdef _WIN32
typedef int(__stdcall* EXCEL12PROC)(int xlfn, int coper, LPXLOPER12* rgpxloper12, LPXLOPER12 xloper12Res);
static EXCEL12PROC excelProc() {
    static EXCEL12PROC p = reinterpret_cast<EXCEL12PROC>(GetProcAddress(GetModuleHandleW(nullptr), "MdCallBack12"));
    return p;
}
#else
typedef int (*EXCEL12PROC)(int xlfn, int coper, LPXLOPER12* rgpxloper12, LPXLOPER12 xloper12Res);
static EXCEL12PROC g_testProc = nullptr;
static EXCEL12PROC excelProc() { return g_testProc; }
// test builds: the mock Excel of tests/xll_test.cpp
XLL_EXPORT void mcTestSetExcel(EXCEL12PROC p) { g_testProc = p; }
#endif

int Excel12v(int xlfn, LPXLOPER12 operRes, int count, LPXLOPER12 opers[]) {
    EXCEL12PROC p = excelProc();
    if (!p) return xlretFailed;
    return p(xlfn, count, opers, operRes);
}

namespace montecalc {
namespace xll {

// ============================================================ strings

std::u16string utf16(const std::string& s) {
    std::u16string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            r += char16_t(0xD800 + (cp >> 10));
            r += char16_t(0xDC00 + (cp & 0x3FF));
        } else {
            r += char16_t(cp);
        }
    }
    return r;
}

std::string utf8(const XCHAR* p, size_t n) {
    std::string r;
    for (size_t i = 0; i < n; ++i) {
        uint32_t cp = uint16_t(p[i]);
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n) cp = 0x10000 + ((cp - 0xD800) << 10) + (uint16_t(p[++i]) - 0xDC00);
        if (cp < 0x80) r += char(cp);
        else if (cp < 0x800) { r += char(0xC0 | (cp >> 6)); r += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { r += char(0xE0 | (cp >> 12)); r += char(0x80 | ((cp >> 6) & 0x3F)); r += char(0x80 | (cp & 0x3F)); }
        else { r += char(0xF0 | (cp >> 18)); r += char(0x80 | ((cp >> 12) & 0x3F)); r += char(0x80 | ((cp >> 6) & 0x3F)); r += char(0x80 | (cp & 0x3F)); }
    }
    return r;
}

std::string textOf(const XLOPER12& x) { return x.val.str ? utf8(x.val.str + 1, size_t(uint16_t(x.val.str[0]))) : std::string(); }

std::string fmt(double d) {
    char b[64];
    std::snprintf(b, sizeof b, "%.15g", d);
    return b;
}

// ============================================================ operands passed to Excel

// an XLOPER12 that owns its memory (strings, arrays)
class Oper {
public:
    Oper() { x_.xltype = xltypeMissing; }
    explicit Oper(double d) { x_.xltype = xltypeNum; x_.val.num = d; }
    explicit Oper(int i) { x_.xltype = xltypeInt; x_.val.w = i; }
    explicit Oper(bool b) { x_.xltype = xltypeBool; x_.val.xbool = b; }
    explicit Oper(const std::string& s) { setStr(s); }
    explicit Oper(const char* s) { setStr(s); }
    // a table of texts (ADD.MENU)
    explicit Oper(const std::vector<std::vector<std::string>>& rows) {
        size_t w = 0;
        for (auto& r : rows) w = std::max(w, r.size());
        items_.resize(rows.size() * w);
        for (size_t i = 0; i < rows.size(); ++i)
            for (size_t j = 0; j < w; ++j) {
                items_[i * w + j] = std::make_unique<Oper>(j < rows[i].size() ? rows[i][j] : std::string());
            }
        cells_.resize(items_.size());
        for (size_t k = 0; k < items_.size(); ++k) cells_[k] = *items_[k]->get();
        x_.xltype = xltypeMulti;
        x_.val.array.lparray = cells_.data();
        x_.val.array.rows = RW(rows.size());
        x_.val.array.columns = COL(w);
    }
    Oper(const Oper&) = delete;
    Oper& operator=(const Oper&) = delete;
    LPXLOPER12 get() { return &x_; }

private:
    void setStr(const std::string& s) {
        std::u16string u = utf16(s);
        if (u.size() > 32767) u.resize(32767);
        buf_.assign(u.size() + 1, 0);
        buf_[0] = XCHAR(u.size());
        for (size_t i = 0; i < u.size(); ++i) buf_[i + 1] = XCHAR(u[i]);
        x_.xltype = xltypeStr;
        x_.val.str = buf_.data();
    }
    XLOPER12 x_{};
    std::vector<XCHAR> buf_;
    std::vector<std::unique_ptr<Oper>> items_;
    std::vector<XLOPER12> cells_;
};

// a result returned by Excel; its memory is given back with xlFree
struct XlValue {
    XLOPER12 x{};
    int rc = xlretFailed;
    XlValue() { x.xltype = xltypeNil; }
    ~XlValue() {
        if (rc == xlretSuccess && (x.xltype & (xltypeStr | xltypeMulti | xltypeRef))) {
            LPXLOPER12 p = &x;
            Excel12v(xlFree, nullptr, 1, &p);
        }
    }
    XlValue(const XlValue&) = delete;
    XlValue& operator=(const XlValue&) = delete;
    bool ok() const { return rc == xlretSuccess && x.xltype != xltypeErr; }
};

int excel(int fn, XlValue* res, std::initializer_list<LPXLOPER12> args) {
    std::vector<LPXLOPER12> a(args);
    XLOPER12 dummy{};
    int rc = Excel12v(fn, res ? &res->x : &dummy, int(a.size()), a.data());
    if (res) res->rc = rc;
    return rc;
}

// ============================================================ arguments from Excel

Any toAny(const XLOPER12& x);

AnyMatrix toMatrix(const XLOPER12& x) {
    AnyMatrix m;
    if ((x.xltype & 0xFFF) == xltypeMulti) {
        m = AnyMatrix(x.val.array.rows);
        for (RW r = 0; r < x.val.array.rows; ++r) {
            Sequence<Any> row(x.val.array.columns);
            for (COL c = 0; c < x.val.array.columns; ++c) row[c] = toAny(x.val.array.lparray[size_t(r) * x.val.array.columns + c]);
            m[r] = row;
        }
    } else if ((x.xltype & 0xFFF) != xltypeMissing) {
        m = AnyMatrix(1);
        Sequence<Any> row(1);
        row[0] = toAny(x);
        m[0] = row;
    }
    return m;
}

Any toAny(const XLOPER12& x) {
    switch (x.xltype & 0xFFF) {
    case xltypeNum: return Any(x.val.num);
    case xltypeInt: return Any(double(x.val.w));
    case xltypeBool: return Any(bool(x.val.xbool != 0));
    case xltypeStr: return Any(OUString(textOf(x)));
    case xltypeMulti: {
        if (x.val.array.rows == 1 && x.val.array.columns == 1) return toAny(x.val.array.lparray[0]);
        return Any(toMatrix(x));
    }
    default: return Any();   // missing, empty, error
    }
}

Any argA(LPXLOPER12 x) { return x ? toAny(*x) : Any(); }
AnyMatrix argR(LPXLOPER12 x) { return x ? toMatrix(*x) : AnyMatrix(); }
double argD(LPXLOPER12 x) {
    Any a = argA(x);
    double d = 0;
    bool b;
    OUString s;
    if (a >>= d) return d;
    if (a >>= b) return b ? 1 : 0;
    if (a >>= s) {
        bool ok;
        double v = parseNumber(u8(s), ok);
        if (ok) return v;
        throw std::invalid_argument("a number is expected: " + u8(s));
    }
    return 0;
}
OUString argS(LPXLOPER12 x) {
    Any a = argA(x);
    OUString s;
    double d;
    bool b;
    if (a >>= s) return s;
    if (a >>= d) return OUString(fmt(d));
    if (a >>= b) return OUString(b ? "TRUE" : "FALSE");
    return OUString();
}

std::string sheetNameOf(LPXLOPER12 ref) {
    XlValue nm;
    excel(xlSheetNm, &nm, {ref});
    if (!nm.ok() || (nm.x.xltype & 0xFFF) != xltypeStr) return "Sheet";
    std::string s = textOf(nm.x);
    size_t b = s.find(']');
    return b == std::string::npos ? s : s.substr(b + 1);
}

// fmc_Name: the referenced cell (registered as macro-sheet function "#", which may read formulas)
XlCell argC(LPXLOPER12 x) {
    XlCell c;
    if (!x) return c;
    DWORD t = x->xltype & 0xFFF;
    if (t != xltypeRef && t != xltypeSRef) return c;
    XLREF12 r = t == xltypeSRef ? x->val.sref.ref : x->val.mref.lpmref->reftbl[0];
    c.valid = true;
    c.row = r.rwFirst;
    c.col = r.colFirst;
    c.sheet = sheetNameOf(x);
    Oper six(6.0);
    XlValue f;
    excel(xlfGetCell, &f, {six.get(), x});
    if (f.ok() && (f.x.xltype & 0xFFF) == xltypeStr) c.formula = textOf(f.x);
    XLOPER12 base = *x;
    XLMREF12 m{};
    c.textAt = [base, m](int row, int col) mutable {
        XLOPER12 ref = base;
        if ((ref.xltype & 0xFFF) == xltypeSRef) {
            ref.val.sref.ref = {row, row, col, col};
        } else {
            m.count = 1;
            m.reftbl[0] = {row, row, col, col};
            ref.val.mref.lpmref = &m;
        }
        ref.xltype &= 0xFFF;
        XlValue v;
        excel(xlCoerce, &v, {&ref});
        return v.ok() && (v.x.xltype & 0xFFF) == xltypeStr ? textOf(v.x) : std::string();
    };
    return c;
}

// ============================================================ results to Excel

// the value returned to Excel: kept until the next call (Excel copies it right away)
struct Result {
    XLOPER12 x{};
    std::vector<XCHAR> strings;
    std::vector<XLOPER12> cells;
    std::vector<size_t> strOffsets;   // where each cell's text starts in `strings`
};
thread_local Result g_result;

Result result(double d) {
    Result r;
    if (std::isfinite(d)) {
        r.x.xltype = xltypeNum;
        r.x.val.num = d;
    } else {
        r.x.xltype = xltypeErr;
        r.x.val.err = xlerrNum;
    }
    return r;
}

void putText(Result& r, size_t cell, const std::string& s) {
    std::u16string u = utf16(s);
    if (u.size() > 32767) u.resize(32767);
    r.strOffsets[cell] = r.strings.size();
    r.strings.push_back(XCHAR(u.size()));
    for (char16_t ch : u) r.strings.push_back(XCHAR(ch));
}

Result result(const OUString& s) {
    Result r;
    r.cells.resize(1);
    r.strOffsets.assign(1, size_t(-1));
    putText(r, 0, u8(s));
    r.cells[0].xltype = xltypeStr;
    r.x = r.cells[0];
    return r;
}

Result result(const AnyMatrix& m) {
    Result r;
    size_t rows = size_t(m.getLength()), cols = 0;
    for (const auto& row : m) cols = std::max(cols, size_t(row.getLength()));
    if (rows == 0 || cols == 0) { r.x.xltype = xltypeErr; r.x.val.err = xlerrNA; return r; }
    r.cells.resize(rows * cols);
    r.strOffsets.assign(rows * cols, size_t(-1));
    for (size_t i = 0; i < rows; ++i)
        for (size_t j = 0; j < cols; ++j) {
            XLOPER12& c = r.cells[i * cols + j];
            if (j >= size_t(m[sal_Int32(i)].getLength())) { c.xltype = xltypeStr; putText(r, i * cols + j, ""); continue; }
            const Any& a = m[sal_Int32(i)][sal_Int32(j)];
            double d;
            bool b;
            OUString s;
            if (a >>= d) {
                if (std::isfinite(d)) { c.xltype = xltypeNum; c.val.num = d; }
                else { c.xltype = xltypeErr; c.val.err = xlerrNum; }
            } else if (a >>= b) {
                c.xltype = xltypeBool;
                c.val.xbool = b;
            } else {
                a >>= s;
                c.xltype = xltypeStr;
                putText(r, i * cols + j, u8(s));
            }
        }
    r.x.xltype = xltypeMulti;
    r.x.val.array.rows = RW(rows);
    r.x.val.array.columns = COL(cols);
    return r;
}

Result result(const DoubleMatrix& m) {
    AnyMatrix a(m.getLength());
    for (sal_Int32 i = 0; i < m.getLength(); ++i) {
        Sequence<Any> row(m[i].getLength());
        for (sal_Int32 j = 0; j < m[i].getLength(); ++j) row[j] = Any(m[i][j]);
        a[i] = row;
    }
    return result(a);
}

LPXLOPER12 publish(Result&& r) {
    g_result = std::move(r);
    // pointers into the vectors are fixed up only now that the result has its final place
    for (size_t k = 0; k < g_result.cells.size(); ++k)
        if (g_result.strOffsets[k] != size_t(-1)) g_result.cells[k].val.str = g_result.strings.data() + g_result.strOffsets[k];
    if ((g_result.x.xltype & 0xFFF) == xltypeMulti) g_result.x.val.array.lparray = g_result.cells.data();
    else if ((g_result.x.xltype & 0xFFF) == xltypeStr) g_result.x = g_result.cells[0];
    return &g_result.x;
}

LPXLOPER12 errorResult(int err) {
    Result r;
    r.x.xltype = xltypeErr;
    r.x.val.err = err;
    return publish(std::move(r));
}

MonteCalcAddIn& addin() {
    static MonteCalcAddIn a;
    return a;
}

// ============================================================ the cell of each variable

struct CellPos {
    IDSHEET sheet = 0;
    RW row = 0;
    COL col = 0;
    std::string sheetName;
    bool operator<(const CellPos& o) const { return std::tie(sheet, row, col) < std::tie(o.sheet, o.row, o.col); }
};
struct VarCellXl {
    CellPos pos;
    std::string name;   // "" = unnamed input (named <Sheet>_<Cell>)
    mc::VarKind kind;
};

bool g_haveCaller = false;
CellPos g_caller;
std::map<CellPos, VarCellXl> g_cells;   // filled during the registration pass

// the cell of the function being calculated, as an absolute reference
bool callerCell(CellPos& out) {
    XlValue c;
    if (excel(xlfCaller, &c, {}) != xlretSuccess) return false;
    DWORD t = c.x.xltype & 0xFFF;
    if (t == xltypeRef && c.x.val.mref.lpmref && c.x.val.mref.lpmref->count > 0) {
        out.sheet = c.x.val.mref.idSheet;
        out.row = c.x.val.mref.lpmref->reftbl[0].rwFirst;
        out.col = c.x.val.mref.lpmref->reftbl[0].colFirst;
    } else if (t == xltypeSRef) {
        out.row = c.x.val.sref.ref.rwFirst;
        out.col = c.x.val.sref.ref.colFirst;
        XlValue nm;
        excel(xlSheetNm, &nm, {&c.x});
        if (!nm.ok()) return false;
        XlValue id;
        excel(xlSheetId, &id, {&nm.x});
        if (!id.ok() || (id.x.xltype & 0xFFF) != xltypeRef) return false;
        out.sheet = id.x.val.mref.idSheet;
    } else {
        return false;
    }
    XLMREF12 m{};
    m.count = 1;
    m.reftbl[0] = {out.row, out.row, out.col, out.col};
    XLOPER12 ref{};
    ref.xltype = xltypeRef;
    ref.val.mref.lpmref = &m;
    ref.val.mref.idSheet = out.sheet;
    out.sheetName = sheetNameOf(&ref);
    return true;
}

void observe(const std::string& name, mc::VarKind kind) {
    if (!g_haveCaller) return;
    auto it = g_cells.find(g_caller);
    // a cell with an input and an output (=FLOsimula_Normal(...)+FLOsimula_output(...)) is an output
    if (it != g_cells.end() && it->second.kind == mc::VarKind::Output) return;
    g_cells[g_caller] = {g_caller, name, kind};
}

// ============================================================ calling a function

template <class Fn>
LPXLOPER12 call(bool flo, std::initializer_list<LPXLOPER12> ops, Fn fn) {
    for (LPXLOPER12 o : ops)
        if (o && (o->xltype & 0xFFF) == xltypeErr) return errorResult(o->val.err);
    auto& sim = mc::Simulation::instance();
    g_haveCaller = flo && sim.mode() == mc::Mode::Register && callerCell(g_caller);
    try {
        Result r = fn();
        g_haveCaller = false;
        return publish(std::move(r));
    } catch (...) {
        g_haveCaller = false;
        return errorResult(xlerrValue);
    }
}

} // namespace xll
} // namespace montecalc

using namespace montecalc::xll;

// ============================================================ the spreadsheet functions

#include "xll_exports.inc"

namespace {

struct FunctionReg {
    const char* proc;
    const char* name;
    const char* type;
    const char* args;
    const char* category;
    const char* help;
    std::vector<const char*> argHelp;
};

const std::vector<FunctionReg>& functions() {
    static const std::vector<FunctionReg> f = {
#include "xll_register.inc"
    };
    return f;
}

std::string moduleName() {
    XlValue n;
    excel(xlGetName, &n, {});
    return n.ok() ? textOf(n.x) : std::string();
}

void registerFunctions() {
    std::string dll = moduleName();
    for (const auto& f : functions()) {
        std::vector<std::unique_ptr<Oper>> ops;
        ops.push_back(std::make_unique<Oper>(dll));
        ops.push_back(std::make_unique<Oper>(f.proc));
        ops.push_back(std::make_unique<Oper>(f.type));
        ops.push_back(std::make_unique<Oper>(f.name));
        ops.push_back(std::make_unique<Oper>(f.args));
        ops.push_back(std::make_unique<Oper>(1.0));   // worksheet function
        ops.push_back(std::make_unique<Oper>(tr(f.category)));
        ops.push_back(std::make_unique<Oper>(""));   // shortcut
        ops.push_back(std::make_unique<Oper>(""));   // help topic
        ops.push_back(std::make_unique<Oper>(f.help));
        for (size_t i = 0; i < f.argHelp.size(); ++i)
            // the Function Wizard cuts the last two characters of the last description
            ops.push_back(std::make_unique<Oper>(std::string(f.argHelp[i]) + (i + 1 == f.argHelp.size() ? "  " : "")));
        std::vector<LPXLOPER12> a;
        for (auto& o : ops) a.push_back(o->get());
        XlValue id;
        id.rc = Excel12v(xlfRegister, &id.x, int(a.size()), a.data());
    }
}

struct Command { const char* proc; const char* name; const char* menu; const char* status; };
const std::vector<Command>& commands() {
    static const std::vector<Command> c = {
        {"mcStart", "MCFLO.Start", "Start simulation", "Runs the Monte Carlo simulation"},
        {"mcReset", "MCFLO.Reset", "Reset", "Forgets the simulation results"},
        {"mcSettings", "MCFLO.Settings", "Settings...", "Iterations, seed, worst/real/best case, use correlations"},
        {"mcInsertOutput", "MCFLO.InsertOutput", "Insert output variable...", "Marks the selected cell as output variable"},
        {"mcLangAuto", "MCFLO.LanguageAuto", "Language: Auto", "Language of MonteCalc: as Windows"},
        {"mcLangEnglish", "MCFLO.LanguageEnglish", "Language: English", "English"},
        {"mcLangGerman", "MCFLO.LanguageGerman", "Language: Deutsch", "Deutsch"},
        {"mcLangSpanish", "MCFLO.LanguageSpanish", "Language: Espa\xC3\xB1ol", "Espa\xC3\xB1ol"},
        {"mcAbout", "MCFLO.About", "About MonteCalc", "Version and licence"},
        // not in the menu
        {"mcRunInternal", "MCFLO.RunInternal", nullptr, nullptr},
        {"mcRelabel", "MCFLO.Relabel", nullptr, nullptr},
        {"mcConnectRibbon", "MCFLO.ConnectRibbon", nullptr, nullptr},
    };
    return c;
}

void registerCommands() {
    std::string dll = moduleName();
    for (const auto& c : commands()) {
        Oper m(dll), p(c.proc), t("J"), n(c.name), a(""), kind(2.0);
        XlValue id;
        excel(xlfRegister, &id, {m.get(), p.get(), t.get(), n.get(), a.get(), kind.get()});
    }
}

const char* kMenu = "MC FLO";
bool g_menuShown = false;
bool g_ribbonLoaded = false;
bool haveRibbon() { return g_ribbonLoaded; }
const double kBar = 10;   // worksheet menu bar (shown on the Add-ins tab)

void removeMenu() {
    Oper bar(kBar), menu(kMenu);
    excel(xlfDeleteMenu, nullptr, {bar.get(), menu.get()});
}

void addMenu() {
    removeMenu();
    std::vector<std::vector<std::string>> rows{{kMenu, "", "", "", ""}};
    for (const auto& c : commands()) {
        if (!c.menu) continue;
        if (std::string(c.proc) == "mcLangAuto" || std::string(c.proc) == "mcAbout") rows.push_back({"-", "", "", "", ""});
        std::string label = c.menu;
        if (label.rfind("Language: ", 0) == 0) label = tr("Language") + ": " + label.substr(10);
        else label = tr(label);
        rows.push_back({label, c.name, "", tr(c.status), ""});
    }
    Oper bar(kBar), menu(rows);
    excel(xlfAddMenu, nullptr, {bar.get(), menu.get()});
    g_menuShown = true;
}

// MCFLO.ConnectRibbon again in two seconds (Application.OnTime)
void scheduleRetry() {
    XlValue now;
    excel(74 /* xlfNow */, &now, {});
    if (!now.ok()) return;
    Oper when(now.x.val.num + 2.0 / 86400.0), macro("MCFLO.ConnectRibbon");
    excel(148 | xlCommand /* xlcOnTime */, nullptr, {when.get(), macro.get()});
}

void alert(const std::string& text, bool error = false) {
    std::string t;
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {   // translate line by line (composed messages)
        if (!first) t += "\n";
        size_t lead = line.find_first_not_of(' ');
        t += lead == std::string::npos ? line : line.substr(0, lead) + tr(line.substr(lead));
        first = false;
    }
    Oper msg(t), type(error ? 3.0 : 2.0);
    excel(xlcAlert, nullptr, {msg.get(), type.get()});
}

void status(const std::string& text) {
    if (text.empty()) {
        Oper off(false);
        excel(xlcMessage, nullptr, {off.get()});
        return;
    }
    Oper on(true), t(text);
    excel(xlcMessage, nullptr, {on.get(), t.get()});
}

void calcNow() { excel(xlcCalculateNow, nullptr, {}); }

// ---- settings: hidden names in the workbook (like the document properties in LibreOffice)
double loadSetting(const char* key, double def) {
    Oper ref(std::string("MonteCalc_") + key);
    XlValue v;
    excel(xlfEvaluate, &v, {ref.get()});
    if (v.ok() && (v.x.xltype & 0xFFF) == xltypeNum) return v.x.val.num;
    if (v.ok() && (v.x.xltype & 0xFFF) == xltypeBool) return v.x.val.xbool ? 1 : 0;
    return def;
}

void saveSetting(const char* key, double value) {
    Oper name(std::string("MonteCalc_") + key), refers("=" + fmt(value)), macro, shortcut, hidden(true);
    excel(xlcDefineName, nullptr, {name.get(), refers.get(), macro.get(), shortcut.get(), hidden.get()});
}

mc::Settings loadSettings() {
    mc::Settings s;
    s.iterations = int(loadSetting("Iterations", s.iterations));
    s.seed = (long long)loadSetting("Seed", double(s.seed));
    s.worst = loadSetting("Worst", s.worst);
    s.real = loadSetting("Real", s.real);
    s.best = loadSetting("Best", s.best);
    s.bins = int(loadSetting("Bins", s.bins));
    s.useCorrelations = loadSetting("UseCorrelations", 1) != 0;
    return s;
}

// ---- simulation
std::string cellText(const CellPos& p) { return p.sheetName + "!" + mc::formula::columnName(p.col) + std::to_string(p.row + 1); }

bool readCell(const CellPos& p, double& out) {
    XLMREF12 m{};
    m.count = 1;
    m.reftbl[0] = {p.row, p.row, p.col, p.col};
    XLOPER12 ref{};
    ref.xltype = xltypeRef;
    ref.val.mref.lpmref = &m;
    ref.val.mref.idSheet = p.sheet;
    XlValue v;
    excel(xlCoerce, &v, {&ref});
    if (!v.ok()) return false;
    switch (v.x.xltype & 0xFFF) {
    case xltypeNum: out = v.x.val.num; return true;
    case xltypeInt: out = v.x.val.w; return true;
    case xltypeBool: out = v.x.val.xbool ? 1 : 0; return true;
    default: return false;
    }
}

bool escapePressed() {
    Oper keep(false);
    XlValue v;
    excel(xlAbort, &v, {keep.get()});
    return v.ok() && (v.x.xltype & 0xFFF) == xltypeBool && v.x.val.xbool;
}

// one run (one variant): registration pass, then every iteration recalculates (or evaluates the
// model in memory) and records the variables' cells
// returns false when the user stopped the run (the completed iterations are kept)
bool runOneXl(const RunOptions& o, const std::string& label, RunResult& rr, LiveRun& live) {
    auto& sim = mc::Simulation::instance();
    const mc::Settings& s = o.s;
    g_cells.clear();
    sim.beginRegistration();
    calcNow();
    struct Rec { CellPos pos; int index; };
    std::vector<Rec> rec;
    int outputs = 0;
    std::vector<std::pair<std::string, VarCellXl>> named;
    for (auto& [pos, v] : g_cells) {
        std::string name = v.name.empty() ? mc::formula::sanitizeName(pos.sheetName) + "_" + mc::formula::columnName(pos.col) +
                                                std::to_string(pos.row + 1)
                                          : v.name;
        sim.setVariableInfo(name, v.kind, "", cellText(pos));
        named.push_back({name, v});
        outputs += v.kind == mc::VarKind::Output;
    }
    if (outputs == 0) {
        sim.setMode(mc::Mode::Expected);
        calcNow();
        throw std::runtime_error("No output variable found. Mark a result with =<cell>+FLOsimula_output(\"Name\") (Insert output variable).");
    }
    std::vector<std::string> warnings;
    try {
        warnings = sim.prepare(s);
        for (auto& [name, v] : named) rec.push_back({v.pos, sim.variableIndex(name)});
    } catch (...) {
        sim.setMode(mc::Mode::Expected);
        calcNow();
        throw;
    }
    for (auto& w : warnings) rr.warnings.push_back((label.empty() ? "" : label + ": ") + w);
    // Memory Calculation: the model's formulas evaluated in memory instead of recalculating Excel
    std::unique_ptr<mc::formula::Model> mem;
    std::unique_ptr<mc::formula::Source> src;
    std::vector<mc::formula::CellAddr> memAddr;
#ifdef _WIN32
    ModelDoc doc;
    if (o.memory && doc.valid()) {
        src = doc.formulaSource();
        mem = std::make_unique<mc::formula::Model>();
        for (auto& r : rec) memAddr.push_back({doc.sheetIndex(r.pos.sheetName), r.pos.col, r.pos.row});
        std::string why;
        if (!mem->build(*src, memAddr, why)) {
            rr.warnings.push_back("Memory calculation not possible (" + why + "); the spreadsheet was recalculated instead.");
            mem.reset();
        }
    }
#endif
    rr.memoryUsed = mem != nullptr;
    Oper echoOff(false), echoOn(true);
    bool echo = o.visualize && !mem;
#ifdef _WIN32
    startPreview(Ctx(), Frame(), o, label, live);
#endif
    if (!echo) excel(xlcEcho, nullptr, {echoOff.get()});
    std::vector<int> index;
    for (auto& r : rec) index.push_back(r.index);
    auto recalc = [&](int) {   // Excel recalculates (on this thread: the C API is single-threaded)
        calcNow();
        for (auto& r : rec) {
            double v;
            sim.recordAt(r.index, readCell(r.pos, v) ? v : std::nan(""));
        }
    };
    bool completed;
    try {
        completed = runIterations(s.iterations, mem.get(), memAddr, index, recalc, o.threads, live);
        if (!completed && sim.completedIterations() == 0) throw std::runtime_error("The simulation was stopped before the first iteration.");
    } catch (...) {
        sim.abort();
        sim.setMode(mc::Mode::Expected);
        calcNow();
        excel(xlcEcho, nullptr, {echoOn.get()});
        status("");
        throw;
    }
    rr.threads = live.ctl.threads;
    sim.finish(label);
    if (!echo) excel(xlcEcho, nullptr, {echoOn.get()});
    status("");
    return completed;
}

// all variants of a multiple simulation (or one run), as runSimulation in the LibreOffice engine
RunResult runAllXl(const RunOptions& o) {
    RunResult rr;
    auto& sim = mc::Simulation::instance();
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Variant> variants;
#ifdef _WIN32
    ModelDoc d;
    if (d.valid()) {
        restoreReplay(d);
        variants = loadVariants(d);
    }
#endif
    sim.clearResultSets();
    // between iterations (about every 50 ms, on Excel's thread): status bar, Esc, preview window
    LiveRun live;
    std::string label;
    live.idle = [&] {
        int done = sim.completedIterations();
        status("MonteCalc" + (label.empty() ? std::string() : " (" + label + ")") + ": " + std::to_string(done * 100 / std::max(1, o.s.iterations)) + "%");
        if (escapePressed()) live.ctl.stop = true;   // Esc = Stop: the completed iterations are the result
#ifdef _WIN32
        updatePreview(live);
        if (o.preview) processEvents(Ctx());
#endif
    };
    struct ClosePreview {   // also when the run fails
        LiveRun& l;
        ~ClosePreview() {
#ifdef _WIN32
            closePreview(l);
#endif
        }
    } closePreview{live};
    if (variants.empty()) {
        rr.stopped = !runOneXl(o, "", rr, live);
    } else {
#ifdef _WIN32
        std::map<std::string, std::pair<css::table::CellAddress, std::string>> saved;
        for (auto& v : variants) {
            std::string k = d.cellName(v.ref);
            if (!saved.count(k)) saved[k] = {v.ref, d.cell(v.ref).formula()};
        }
        try {
            for (auto& v : variants) {
                d.cell(v.ref).setValue(v.value);
                label = v.label;
                if (!runOneXl(o, v.label, rr, live)) {   // Stop: no further variants
                    rr.stopped = true;
                    break;
                }
            }
        } catch (...) {
            for (auto& [k, sv] : saved) d.cell(sv.first).setFormula(sv.second);
            throw;
        }
        for (auto& [k, sv] : saved) d.cell(sv.first).setFormula(sv.second);
        sim.selectResultSet(0);
#endif
    }
    rr.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sim.setMode(o.s.seed == -1 ? mc::Mode::Random : mc::Mode::Expected);
    calcNow();   // expected values again; the fMC_ functions show the results
#ifdef _WIN32
    if (o.dip && d.valid()) storeDip(d);
#endif
    rr.summary = runSummary(o, rr);
    return rr;
}

// options of a run requested from a window or the ribbon (MCFLO.RunInternal)
bool g_inCommand = false;
RunOptions g_pending;
RunResult g_pendingResult;
std::string g_pendingError;

double askNumber(const std::string& prompt, double def, bool& cancelled) {
    Oper msg(tr(prompt)), type(1.0), title(tr("MonteCalc - Settings")), deflt(def);
    XlValue v;
    excel(xlfInput, &v, {msg.get(), type.get(), title.get(), deflt.get()});
    if (!v.ok() || (v.x.xltype & 0xFFF) == xltypeBool) { cancelled = true; return def; }
    if ((v.x.xltype & 0xFFF) == xltypeNum) return v.x.val.num;
    return def;
}

template <class Fn>
int command(Fn fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        alert(e.what(), true);
    } catch (...) {
        alert("MonteCalc: unexpected error", true);
    }
    return 1;
}

void setLanguage(const char* choice) {
    setLanguageSetting(choice);
    registerFunctions();   // Function Wizard categories in the new language
    if (g_menuShown || !haveRibbon()) addMenu();
#ifdef _WIN32
    ribbonInvalidate();
#endif
}

} // namespace

// ============================================================ commands

namespace montecalc {
// the simulation for the shared commands (Engine::cmdRun, result window "Start"): inside a command the
// C API loop runs directly, from a window or the ribbon through Application.Run("MCFLO.RunInternal")
RunResult runSimulation(const css::uno::Reference<css::uno::XComponentContext>&, const css::uno::Reference<css::frame::XFrame>&, const RunOptions& o) {
#ifdef _WIN32
    if (!g_inCommand) {
        g_pending = o;
        g_pendingResult = RunResult();
        g_pendingError.clear();
        montecalc::com::runMacro("MCFLO.RunInternal");
        if (!g_pendingError.empty()) throw std::runtime_error(g_pendingError);
        return g_pendingResult;
    }
#endif
    return runAllXl(o);
}
} // namespace montecalc

namespace {
// Excel's object model reachable (the windows and most commands need it); not in the mock test
bool haveObjectModel() {
#ifdef _WIN32
    return montecalc::com::haveApplication() || montecalc::com::findApplication(montecalc::w32::excelWindow());
#else
    return false;
#endif
}

// a shared MC FLO command (uno/engine.cpp) from a C API command
int engineCommand(const char* name) {
    return command([&] {
        g_inCommand = true;
        struct Reset { ~Reset() { g_inCommand = false; } } reset;
        Engine().run(name);
    });
}
} // namespace

XLL_EXPORT int XLL_CALL mcRunInternal() {
    g_inCommand = true;
    try {
        g_pendingResult = runAllXl(g_pending);
    } catch (const std::exception& e) {
        g_pendingError = e.what();
    } catch (...) {
        g_pendingError = "MonteCalc: unexpected error";
    }
    g_inCommand = false;
    return 1;
}

XLL_EXPORT int XLL_CALL mcStart() {
    if (haveObjectModel()) return engineCommand("Start");
    return command([] {   // without Excel's object model (tests): the run and its summary
        auto& sim = mc::Simulation::instance();
        if (sim.busy()) throw std::runtime_error("a simulation is running");
        RunOptions o;
        o.s = loadSettings();
        sim.setSettings(o.s);
        alert(runAllXl(o).summary);
    });
}

XLL_EXPORT int XLL_CALL mcReset() {
    if (haveObjectModel()) return engineCommand("Reset");
    return command([] {
        auto& sim = mc::Simulation::instance();
        sim.reset();
        sim.setMode(mc::Mode::Expected);
        calcNow();
    });
}

XLL_EXPORT int XLL_CALL mcSettings() {
    if (haveObjectModel()) return engineCommand("Settings");
    return command([] {
        mc::Settings s = loadSettings();
        bool c = false;
        double it = askNumber("# iterations", s.iterations, c);
        if (c) return;
        double seed = askNumber("Seed (0 = random, -1 = preview mode)", double(s.seed), c);
        if (c) return;
        double worst = askNumber("Worst case level", s.worst, c);
        if (c) return;
        double real = askNumber("Real case level", s.real, c);
        if (c) return;
        double best = askNumber("Best case level", s.best, c);
        if (c) return;
        double corr = askNumber("Use correlations (1 = yes, 0 = no)", s.useCorrelations ? 1 : 0, c);
        if (c) return;
        if (it < 1 || it > 100000000) throw std::runtime_error("# iterations must be between 1 and 100,000,000.");
        if (!(worst > 0 && worst < real && real < best && best < 1)) throw std::runtime_error("The levels must satisfy 0 < worst < real < best < 1.");
        saveSetting("Iterations", std::round(it));
        saveSetting("Seed", std::round(seed));
        saveSetting("Worst", worst);
        saveSetting("Real", real);
        saveSetting("Best", best);
        saveSetting("UseCorrelations", corr != 0 ? 1 : 0);
        mc::Simulation::instance().setSettings(loadSettings());
    });
}

XLL_EXPORT int XLL_CALL mcInsertOutput() {
    if (haveObjectModel()) return engineCommand("InsertOutput");
    return command([] {
        XlValue cell;
        excel(xlfActiveCell, &cell, {});
        if (!cell.ok()) throw std::runtime_error("Select the cell with the result to analyse.");
        XlValue f;
        excel(xlfGetFormula, &f, {&cell.x});
        std::string cur = f.ok() && (f.x.xltype & 0xFFF) == xltypeStr ? textOf(f.x) : std::string();
        Oper msg(tr("Name")), type(2.0), title(tr("MonteCalc - Insert output variable"));
        XlValue name;
        excel(xlfInput, &name, {msg.get(), type.get(), title.get()});
        if (!name.ok() || (name.x.xltype & 0xFFF) != xltypeStr) return;
        std::string n = mc::formula::sanitizeName(textOf(name.x));
        if (n.empty()) throw std::runtime_error("Please enter a name.");
        std::string out = "FLOsimula_output(\"" + n + "\")", nf;
        if (cur.empty()) nf = "=+" + out;
        else if (cur[0] == '=') nf = "=(" + cur.substr(1) + ")+" + out;
        else nf = "=" + cur + "+" + out;
        Oper formula(nf);
        excel(xlcFormula, nullptr, {formula.get(), &cell.x});
    });
}

XLL_EXPORT int XLL_CALL mcLangAuto() { return command([] { setLanguage("Auto"); }); }
XLL_EXPORT int XLL_CALL mcLangEnglish() { return command([] { setLanguage("English"); }); }
XLL_EXPORT int XLL_CALL mcLangGerman() { return command([] { setLanguage("Deutsch"); }); }
XLL_EXPORT int XLL_CALL mcLangSpanish() { return command([] { setLanguage("Espa\xC3\xB1ol"); }); }
XLL_EXPORT int XLL_CALL mcLanguage() { return mcLangAuto(); }
// after a language change in the ribbon: Function Wizard categories (and the fallback menu)
XLL_EXPORT int XLL_CALL mcRelabel() {
    return command([] {
        registerFunctions();
        if (g_menuShown) addMenu();
    });
}

XLL_EXPORT int XLL_CALL mcAbout() {
    if (haveObjectModel()) return engineCommand("About");
    return command([] {
        alert("MonteCalc for Excel - Monte Carlo simulation with the MC FLO functions.\n"
              "FLOsimula_ model functions, fMC_ result and tool functions (Insert Function, categories as on the MC FLO ribbon).\n"
              "Menu MC FLO on the Add-ins tab: Start simulation, Reset, Settings, Insert output variable, Language.");
    });
}

namespace montecalc {
void ribbonLoaded() { g_ribbonLoaded = true; }
void ribbonLanguageChanged() {
#ifdef _WIN32
    montecalc::com::runMacro("MCFLO.Relabel");   // Function Wizard categories (C API)
    ribbonInvalidate();
#endif
}
} // namespace montecalc

// connects the ribbon once Excel's object model is ready (at start-up without a workbook it is not)
XLL_EXPORT int XLL_CALL mcConnectRibbon() {
#ifdef _WIN32
    static int tries = 0;
    if (connectRibbon()) return 1;
    if (++tries < 30) scheduleRetry();
    else addMenu();   // no ribbon: the menu on the Add-ins tab
#endif
    return 1;
}

// ============================================================ add-in manager interface

XLL_EXPORT int XLL_CALL xlAutoOpen() {
    mc::Simulation::instance().setRegistrationObserver(observe);
    registerFunctions();
    registerCommands();
#ifdef _WIN32
    // the MC FLO ribbon tab (a COM add-in in this .xll); the Add-ins menu only if it cannot be shown
    registerRibbon();
    if (!connectRibbon()) scheduleRetry();
#else
    addMenu();
#endif
    return 1;
}

XLL_EXPORT int XLL_CALL xlAutoClose() {
    if (g_menuShown) removeMenu();
#ifdef _WIN32
    montecalc::w32::removeKeyboardHook();   // the hook procedure lives in this DLL
#endif
    return 1;
}

// the add-in is removed in the Add-in Manager: the ribbon goes too
XLL_EXPORT int XLL_CALL xlAutoRemove() {
#ifdef _WIN32
    unregisterRibbon();
#endif
    return 1;
}

XLL_EXPORT void XLL_CALL xlAutoFree12(LPXLOPER12) {}   // results are owned by the add-in (no xlbitDLLFree)

XLL_EXPORT LPXLOPER12 XLL_CALL xlAddInManagerInfo12(LPXLOPER12 action) {
    bool name = action && (((action->xltype & 0xFFF) == xltypeNum && action->val.num == 1) || ((action->xltype & 0xFFF) == xltypeInt && action->val.w == 1));
    if (!name) return errorResult(xlerrValue);
    return publish(result(OUString("MonteCalc - MC FLO for Excel")));
}
