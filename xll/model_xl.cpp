// MonteCalc for Excel - the model document (uno/model.hpp) on Excel's object model.
//
// Formulas are exchanged in LibreOffice syntax ("; " separators, $'Sheet'.A1 references), so the
// shared code (commands, windows, formula engine) works unchanged; this file converts.
#include "../uno/model.hpp"
#include "../uno/util.hpp"
#include "com.hpp"
#include "xlimpl.hpp"
#include "xlformula.hpp"
#include "win32ui.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using montecalc::com::Disp;
using montecalc::com::Var;

namespace montecalc {

// file:///C:/a/b.xlsx -> C:\a\b.xlsx
static std::string w32path(const std::string& url) {
    if (url.rfind("file:", 0) != 0) return url;
    std::string p = url.substr(5);
    while (p.size() > 1 && p[0] == '/' && p[1] == '/') p = p.substr(1);
    if (p.size() > 2 && p[0] == '/' && p[2] == ':') p = p.substr(1);
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '%' && i + 2 < p.size()) { out += char(std::stoi(p.substr(i + 1, 2), nullptr, 16)); i += 2; }
        else out += p[i] == '/' ? '\\' : p[i];
    }
    return out;
}


namespace xlf {

long bgr(sal_Int32 rgb) { return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16); }

} // namespace xlf

// ================================================================ cells

static Var value2(const Disp& r) { return r.get("Value2"); }

std::string Cell::formula() const {
    Var f = c_->range.get("Formula");
    std::string s = f.str();
    return !s.empty() && s[0] == '=' ? xlf::fromExcel(s) : s;
}

void Cell::setFormula(const std::string& f) const {
    std::string x = !f.empty() && f[0] == '=' ? xlf::toExcel(f) : f;
    try {
        c_->range.put("Formula", Var(x));
    } catch (const std::exception& first) {
        // refused from a window's event: once more inside a command that Excel runs itself
        try {
            com::Disp range = c_->range;
            runInExcelCommand([range, x] { range.put("Formula", Var(x)); });
        } catch (const std::exception&) {
            throw std::runtime_error(std::string(first.what()) + "\n" + x);   // the formula Excel did not accept
        }
    }
}

double Cell::value() const { return value2(c_->range).num(0); }
void Cell::setValue(double v) const { c_->range.put("Value2", Var(v)); }

std::string Cell::text() const {
    Var v = value2(c_->range);
    if (v.type() == VT_BSTR) return v.str();
    if (v.isEmpty() || v.isError()) return {};
    if (v.type() == VT_BOOL) return v.boolean() ? "TRUE" : "FALSE";
    char b[64];
    std::snprintf(b, sizeof b, "%.15g", v.num());
    return b;
}

void Cell::setText(const std::string& t) const {
    bool numeric = false;
    parseNumber(t, numeric);
    bool needsQuote = numeric || (!t.empty() && (t[0] == '=' || t[0] == '+' || t[0] == '-' || t[0] == '\''));
    c_->range.put("Value2", Var(needsQuote ? "'" + t : t));
}

CellType Cell::type() const {
    if (c_->range.get("HasFormula").boolean()) return CellType::Formula;
    Var v = value2(c_->range);
    if (v.isEmpty()) return CellType::Empty;
    if (v.type() == VT_BSTR) return CellType::Text;
    return CellType::Value;
}

int Cell::error() const {
    Var v = value2(c_->range);
    return v.isError() ? int(v.raw().scode & 0xFFFF) : 0;
}

// ================================================================ document

ModelDoc::ModelDoc() : x_(std::make_shared<XlDocImpl>()) {
    try {
        if (com::haveApplication() || com::findApplication(w32::excelWindow())) x_->wb = com::application().obj("ActiveWorkbook");
    } catch (...) {
    }
}

bool ModelDoc::valid() const { return x_ && x_->wb.valid(); }

static Disp sheetOf(const ModelDoc& d, int i) {
    if (!d.valid()) throw std::runtime_error("No workbook is open.");
    return d.impl()->wb.obj("Worksheets", {Var(i + 1)});
}

static Disp rangeOf(const ModelDoc& d, int sheet, int c0, int r0, int c1, int r1) {
    Disp sh = sheetOf(d, sheet);
    Disp a = sh.obj("Cells", {Var(r0 + 1), Var(c0 + 1)}), b = sh.obj("Cells", {Var(r1 + 1), Var(c1 + 1)});
    return sh.obj("Range", {Var(a), Var(b)});
}

ModelDoc ModelDoc::newDocument(const uno::Reference<uno::XComponentContext>&, bool) {
    ModelDoc d;
    d.x_->wb = com::application().obj("Workbooks").obj("Add");
    return d;
}

std::vector<std::string> ModelDoc::sheetNames() const {
    std::vector<std::string> r;
    if (!valid()) return r;
    Disp ws = x_->wb.obj("Worksheets");
    int n = int(ws.get("Count").num());
    for (int i = 1; i <= n; ++i) r.push_back(ws.obj("Item", {Var(i)}).get("Name").str());
    return r;
}

int ModelDoc::sheetIndex(const std::string& name) const {
    auto n = sheetNames();
    for (size_t i = 0; i < n.size(); ++i)
        if (n[i] == name) return int(i);
    for (size_t i = 0; i < n.size(); ++i)
        if (mc::lower(n[i]) == mc::lower(name)) return int(i);
    return -1;
}

Cell ModelDoc::cell(const table::CellAddress& a) const {
    auto c = std::make_shared<XlCellImpl>();
    c->range = sheetOf(*this, a.Sheet).obj("Cells", {Var(int(a.Row) + 1), Var(int(a.Column) + 1)});
    return Cell(c);
}

// the used range of a sheet: its formulas (row/column offset of the first cell)
static bool usedRange(const ModelDoc& d, int s, Disp& used, int& row0, int& col0) {
    used = sheetOf(d, s).obj("UsedRange");
    if (!used.valid()) return false;
    row0 = int(used.get("Row").num()) - 1;
    col0 = int(used.get("Column").num()) - 1;
    return true;
}

std::vector<std::pair<table::CellAddress, std::string>> ModelDoc::formulaCells(int s) const {
    std::vector<std::pair<table::CellAddress, std::string>> out;
    Disp used;
    int r0, c0;
    if (!usedRange(*this, s, used, r0, c0)) return out;
    auto rows = used.get("Formula").rows();
    for (size_t i = 0; i < rows.size(); ++i)
        for (size_t j = 0; j < rows[i].size(); ++j) {
            if (rows[i][j].type() != VT_BSTR) continue;
            std::string f = rows[i][j].str();
            if (f.size() > 1 && f[0] == '=')
                out.push_back({table::CellAddress(sal_Int16(s), c0 + sal_Int32(j), r0 + sal_Int32(i)), xlf::fromExcel(f)});
        }
    return out;
}

std::vector<table::CellAddress> ModelDoc::valueCells(int s) const {
    std::vector<table::CellAddress> out;
    Disp used;
    int r0, c0;
    if (!usedRange(*this, s, used, r0, c0)) return out;
    auto f = used.get("Formula").rows(), v = used.get("Value2").rows();
    for (size_t i = 0; i < v.size() && i < f.size(); ++i)
        for (size_t j = 0; j < v[i].size() && j < f[i].size(); ++j) {
            std::string ft = f[i][j].str();
            if (!(ft.size() > 1 && ft[0] == '=') && v[i][j].isNumber())
                out.push_back(table::CellAddress(sal_Int16(s), c0 + sal_Int32(j), r0 + sal_Int32(i)));
        }
    return out;
}

int ModelDoc::ensureSheet(const std::string& name, bool visible) {
    int i = sheetIndex(name);
    Disp sh;
    if (i < 0) {
        Disp app = com::application();
        Disp active = app.obj("ActiveSheet");
        Disp ws = x_->wb.obj("Worksheets");
        Disp last = ws.obj("Item", {Var(int(ws.get("Count").num()))});
        sh = ws.obj("Add", {Var::missing(), Var(last)});
        sh.put("Name", Var(name));
        if (active.valid()) try { active.call("Activate"); } catch (...) {}
        i = sheetIndex(name);
    } else {
        sh = sheetOf(*this, i);
    }
    sh.put("Visible", Var(visible ? -1 : 0));   // xlSheetVisible / xlSheetHidden
    return i;
}

void ModelDoc::renameSheet(int s, const std::string& name) { sheetOf(*this, s).put("Name", Var(name)); }

void ModelDoc::removeSheet(const std::string& name) {
    int i = sheetIndex(name);
    if (i < 0) return;
    Disp app = com::application();
    app.put("DisplayAlerts", Var(false));
    try {
        Disp sh = sheetOf(*this, i);
        sh.put("Visible", Var(-1));
        sh.call("Delete");
    } catch (...) {
    }
    app.put("DisplayAlerts", Var(true));
}

void ModelDoc::writeTable(int s, int row, int col, const std::vector<std::vector<Any>>& rows) const {
    if (rows.empty()) return;
    size_t w = 0;
    for (auto& r : rows) w = std::max(w, r.size());
    if (w == 0) return;
    std::vector<std::vector<Var>> t(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
        for (size_t j = 0; j < w; ++j) {
            Any a = j < rows[i].size() ? rows[i][j] : Any();
            double d;
            bool b;
            OUString str;
            if (a >>= d) t[i].push_back(Var(d));
            else if (a >>= b) t[i].push_back(Var(b));
            else if (a >>= str) t[i].push_back(Var(u8(str)));
            else t[i].push_back(Var(std::string()));
        }
    rangeOf(*this, s, col, row, col + int(w) - 1, row + int(rows.size()) - 1).put("Value2", Var::table(t));
}

void ModelDoc::optimalWidth(int s, int firstCol, int nCols) const {
    Disp sh = sheetOf(*this, s);
    for (int c = firstCol; c < firstCol + nCols; ++c) sh.obj("Columns", {Var(c + 1)}).call("AutoFit");
}

void ModelDoc::setComment(const table::CellAddress& a, const std::string& text) const {
    Disp r = cell(a).impl()->range;
    r.call("ClearComments");
    r.call("AddComment", {Var(text)});
}

static std::string sheetRef(const std::string& sheet) {
    std::string q = "'";
    for (char c : sheet) q += c == '\'' ? std::string("''") : std::string(1, c);
    return q + "'!";
}

void ModelDoc::setNamedArea(const std::string& name, int s, int col, int row0, int row1) {
    removeNamedRange(name);
    std::string c = mc::formula::columnName(col);
    x_->wb.obj("Names").call("Add", {Var(name), Var("=" + sheetRef(sheetNames()[s]) + "$" + c + "$" + std::to_string(row0 + 1) + ":$" + c + "$" +
                                                    std::to_string(row1 + 1))});
}

void ModelDoc::setNamedRange(const std::string& name, const table::CellAddress& a) {
    removeNamedRange(name);
    x_->wb.obj("Names").call("Add", {Var(name), Var("=" + sheetRef(sheetNames()[a.Sheet]) + "$" + mc::formula::columnName(a.Column) + "$" +
                                                    std::to_string(a.Row + 1))});
}

static std::vector<std::pair<std::string, Disp>> allNames(const Disp& wb) {
    std::vector<std::pair<std::string, Disp>> r;
    Disp names = wb.obj("Names");
    int n = int(names.get("Count").num());
    for (int i = 1; i <= n; ++i) {
        Disp nm = names.obj("Item", {Var(i)});
        std::string s = nm.get("Name").str();
        size_t bang = s.find('!');   // sheet-scoped names "Sheet1!x"
        r.push_back({bang == std::string::npos ? s : s.substr(bang + 1), nm});
    }
    return r;
}

void ModelDoc::removeNamedRange(const std::string& name) {
    if (!valid()) return;
    for (auto& [n, d] : allNames(x_->wb))
        if (mc::lower(n) == mc::lower(name)) { d.call("Delete"); return; }
}

void ModelDoc::removeNamedRangesWithPrefix(const std::string& prefix) {
    if (!valid()) return;
    for (auto& [n, d] : allNames(x_->wb))
        if (n.rfind(prefix, 0) == 0) d.call("Delete");
}

static bool rangeAddress(const Disp& r, const ModelDoc& d, table::CellRangeAddress& out) {
    if (!r.valid()) return false;
    std::string sheet = r.obj("Worksheet").get("Name").str();
    int s = d.sheetIndex(sheet);
    if (s < 0) return false;
    int row = int(r.get("Row").num()) - 1, col = int(r.get("Column").num()) - 1;
    int rows = int(r.obj("Rows").get("Count").num()), cols = int(r.obj("Columns").get("Count").num());
    out = table::CellRangeAddress(sal_Int16(s), col, row, col + cols - 1, row + rows - 1);
    return true;
}

bool ModelDoc::namedRange(const std::string& name, table::CellRangeAddress& r) const {
    if (!valid()) return false;
    for (auto& [n, d] : allNames(x_->wb))
        if (mc::lower(n) == mc::lower(name)) {
            try { return rangeAddress(d.obj("RefersToRange"), *this, r); } catch (...) { return false; }
        }
    return false;
}

std::vector<std::pair<std::string, table::CellRangeAddress>> ModelDoc::namedRanges(const std::string& prefix) const {
    std::vector<std::pair<std::string, table::CellRangeAddress>> out;
    if (!valid()) return out;
    for (auto& [n, d] : allNames(x_->wb)) {
        if (n.rfind(prefix, 0) != 0) continue;
        table::CellRangeAddress r;
        try {
            if (rangeAddress(d.obj("RefersToRange"), *this, r)) out.push_back({n, r});
        } catch (...) {
        }
    }
    return out;
}

bool ModelDoc::selectionRange(table::CellRangeAddress& r) const {
    if (!valid()) return false;
    try {
        Disp sel = com::application().obj("Selection");
        Disp area = sel.obj("Areas", {Var(1)});
        return rangeAddress(area, *this, r);
    } catch (...) {
        return false;
    }
}

bool ModelDoc::documentScreenRect(int& x, int& y, int& w, int& h) const {
    RECT r;
    HWND main = w32::excelWindow();
    if (!main || !GetWindowRect(main, &r)) return false;
    x = r.left;
    y = r.top;
    w = r.right - r.left;
    h = r.bottom - r.top;
    return true;
}

bool ModelDoc::cellScreenRect(const table::CellAddress& a, int& x, int& y, int& w, int& h) const {
    if (!valid()) return false;
    try {
        Disp app = com::application();
        Disp win = app.obj("ActiveWindow");
        auto names = sheetNames();
        if (a.Sheet < 0 || a.Sheet >= int(names.size()) || app.obj("ActiveSheet").get("Name").str() != names[size_t(a.Sheet)]) return false;
        // scrolled away?
        Disp vis = win.obj("VisibleRange");
        int r0 = int(vis.get("Row").num()), c0 = int(vis.get("Column").num());
        int nr = int(vis.obj("Rows").get("Count").num()), nc = int(vis.obj("Columns").get("Count").num());
        if (a.Row + 1 < r0 || a.Row + 1 >= r0 + nr || a.Column + 1 < c0 || a.Column + 1 >= c0 + nc) return false;
        Disp rng = cell(a).impl()->range;
        double l = rng.get("Left").num(), t = rng.get("Top").num(), rw = rng.get("Width").num(), rh = rng.get("Height").num();
        // the pane converts sheet points to screen pixels, with zoom and scrolling
        Disp pane = win.obj("ActivePane");
        int x0 = int(pane.call("PointsToScreenPixelsX", {Var(l)}).num()), x1 = int(pane.call("PointsToScreenPixelsX", {Var(l + rw)}).num());
        int y0 = int(pane.call("PointsToScreenPixelsY", {Var(t)}).num()), y1 = int(pane.call("PointsToScreenPixelsY", {Var(t + rh)}).num());
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
        return w > 0 && h > 0;
    } catch (...) {
        return false;
    }
}

bool ModelDoc::currentCell(table::CellAddress& a) const {
    table::CellRangeAddress r;
    if (!selectionRange(r)) return false;
    a = table::CellAddress(r.Sheet, r.StartColumn, r.StartRow);
    return true;
}

void ModelDoc::selectRange(const table::CellRangeAddress& r) const {
    sheetOf(*this, r.Sheet).call("Activate");
    rangeOf(*this, r.Sheet, r.StartColumn, r.StartRow, r.EndColumn, r.EndRow).call("Select");
}

void ModelDoc::select(const table::CellAddress& a) const {
    selectRange(table::CellRangeAddress(a.Sheet, a.Column, a.Row, a.Column, a.Row));
}

std::vector<std::vector<double>> ModelDoc::selectionValues(std::vector<std::string>* label) const {
    std::vector<std::vector<double>> rows;
    table::CellRangeAddress r;
    if (!selectionRange(r)) return rows;
    auto v = rangeOf(*this, r.Sheet, r.StartColumn, r.StartRow, r.EndColumn, r.EndRow).get("Value2").rows();
    for (auto& line : v) {
        std::vector<double> out;
        for (auto& c : line) {
            if (c.isNumber()) out.push_back(c.num());
            else {
                if (label && c.type() == VT_BSTR && !c.str().empty()) label->push_back(c.str());
                out.push_back(std::nan(""));
            }
        }
        rows.push_back(out);
    }
    return rows;
}

void ModelDoc::setBackground(const table::CellAddress& a, sal_Int32 rgb) const {
    Disp interior = cell(a).impl()->range.obj("Interior");
    if (rgb < 0) interior.put("ColorIndex", Var(-4142));   // xlNone
    else interior.put("Color", Var(int(xlf::bgr(rgb))));
}

void ModelDoc::recalc() const {
    try { com::application().call("Calculate"); } catch (...) {}
}

// ---- settings: hidden names MonteCalc_<key> in the workbook
static std::string propName(const std::string& name) {
    std::string n = name;
    for (auto& c : n) if (c == '.') c = '_';
    return n;
}

static bool propRefersTo(const ModelDoc& d, const std::string& name, std::string& refersTo) {
    if (!d.valid()) return false;
    std::string pn = mc::lower(propName(name));
    for (auto& [n, nm] : allNames(d.impl()->wb))
        if (mc::lower(n) == pn) { refersTo = nm.get("RefersTo").str(); return true; }
    return false;
}

double ModelDoc::property(const std::string& name, double def) const {
    std::string r;
    if (!propRefersTo(*this, name, r)) return def;
    if (!r.empty() && r[0] == '=') r = r.substr(1);
    if (r == "TRUE") return 1;
    if (r == "FALSE") return 0;
    bool ok;
    double v = parseNumber(r, ok);
    return ok ? v : def;
}

std::string ModelDoc::propertyText(const std::string& name) const {
    std::string r;
    if (!propRefersTo(*this, name, r)) return {};
    if (!r.empty() && r[0] == '=') r = r.substr(1);
    if (r.size() >= 2 && r.front() == '"' && r.back() == '"') {
        std::string t;
        for (size_t i = 1; i + 1 < r.size(); ++i) {
            t += r[i];
            if (r[i] == '"' && r[i + 1] == '"') ++i;
        }
        return t;
    }
    return r;
}

void ModelDoc::setProperty(const std::string& name, const Any& v) const {
    if (!valid()) return;
    std::string pn = propName(name);
    const_cast<ModelDoc*>(this)->removeNamedRange(pn);
    if (!v.hasValue()) return;
    double d;
    bool b;
    OUString s;
    std::string refersTo;
    if (v >>= d) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "=%.15g", d);
        refersTo = buf;
    } else if (v >>= b) {
        refersTo = b ? "=1" : "=0";
    } else if (v >>= s) {
        refersTo = "=\"";
        for (char c : u8(s)) refersTo += c == '"' ? std::string("\"\"") : std::string(1, c);
        refersTo += "\"";
    } else {
        return;
    }
    // Names.Add(Name, RefersTo, Visible)
    x_->wb.obj("Names").call("Add", {Var(pn), Var(refersTo), Var(false)});
}

int ModelDoc::insertSheet(const std::string& name, int pos) {
    removeSheet(name);
    Disp app = com::application();
    Disp active = app.obj("ActiveSheet");
    Disp ws = x_->wb.obj("Worksheets");
    int n = int(ws.get("Count").num());
    Disp sh = pos < n ? ws.obj("Add", {Var(ws.obj("Item", {Var(pos + 1)}))}) : ws.obj("Add", {Var::missing(), Var(ws.obj("Item", {Var(n)}))});
    sh.put("Name", Var(name));
    if (active.valid()) try { active.call("Activate"); } catch (...) {}
    return sheetIndex(name);
}

void ModelDoc::numberFormat(int s, int c0, int r0, int c1, int r1, const std::string& code) const {
    rangeOf(*this, s, c0, r0, c1, r1).put("NumberFormat", Var(code));
}

void ModelDoc::protectSheet(int s) const { sheetOf(*this, s).call("Protect"); }

void ModelDoc::setArrayFormula(int s, int c0, int r0, int c1, int r1, const std::string& f) const {
    rangeOf(*this, s, c0, r0, c1, r1).put("FormulaArray", Var(xlf::toExcel("=" + f)));
}

void ModelDoc::activateSheet(int s) const { sheetOf(*this, s).call("Activate"); }

std::string ModelDoc::url() const {
    if (!valid()) return {};
    std::string path = x_->wb.get("Path").str();
    if (path.empty()) return {};
    std::string full = x_->wb.get("FullName").str();
    std::string out = "file:///";
    for (char c : full) {
        if (c == '\\') out += '/';
        else if (c == ' ') out += "%20";
        else out += c;
    }
    return out;
}

static std::string a1(const ModelDoc& d, const table::CellRangeAddress& r) {
    std::string s = sheetRef(d.sheetNames()[r.Sheet]);
    return s + "$" + mc::formula::columnName(r.StartColumn) + "$" + std::to_string(r.StartRow + 1) + ":$" + mc::formula::columnName(r.EndColumn) + "$" +
           std::to_string(r.EndRow + 1);
}

void ModelDoc::addChart(int s, const std::string& name, ChartKind kind, const std::vector<table::CellRangeAddress>& ranges, int x, int y, int w, int h,
                        const std::string& title, bool legend) const {
    if (ranges.empty()) return;
    Disp sh = sheetOf(*this, s);
    const double pt = 72.0 / 2540.0;   // 1/100 mm -> points
    Disp objs = sh.obj("ChartObjects");
    try { objs.obj("Item", {Var(name)}).call("Delete"); } catch (...) {}
    Disp co = objs.obj("Add", {Var(x * pt), Var(y * pt), Var(w * pt), Var(h * pt)});
    co.put("Name", Var(name));
    Disp ch = co.obj("Chart");
    std::string src;
    for (auto& r : ranges) src += (src.empty() ? "" : ",") + a1(*this, r);
    ch.call("SetSourceData", {Var(sh.obj("Range", {Var(src)}))});
    ch.put("ChartType", Var(kind == ChartKind::Line ? 4 : kind == ChartKind::Tornado ? 57 : 51));   // xlLine, xlBarClustered, xlColumnClustered
    ch.put("HasTitle", Var(true));
    ch.obj("ChartTitle").put("Text", Var(title));
    ch.put("HasLegend", Var(legend));
    if (kind == ChartKind::Tornado) {
        // labels at the left edge (xlTickLabelPositionLow), bars around zero
        try { ch.obj("Axes", {Var(1)}).put("TickLabelPosition", Var(-4134)); } catch (...) {}
    }
}

void ModelDoc::saveAs(const std::string& url) const {
    Disp app = com::application();
    app.put("DisplayAlerts", Var(false));
    try {
        x_->wb.call("SaveAs", {Var(w32path(url)), Var(51)});   // xlOpenXMLWorkbook
    } catch (...) {
        app.put("DisplayAlerts", Var(true));
        throw;
    }
    app.put("DisplayAlerts", Var(true));
}

void ModelDoc::close() const { x_->wb.call("Close", {Var(false)}); }

const void* ModelDoc::key() const {
    static std::set<std::string> keys;
    if (!valid()) return nullptr;
    return &*keys.insert(x_->wb.get("FullName").str()).first;
}

} // namespace montecalc
