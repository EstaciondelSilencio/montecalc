// MonteCalc - access to the model document
#include "model.hpp"
#include "util.hpp"

#include "../core/flofuncs.h"

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/sheet/CellFlags.hpp>
#include <com/sun/star/sheet/XCalculatable.hpp>
#include <com/sun/star/sheet/XCellAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XCellRangeReferrer.hpp>
#include <com/sun/star/sheet/XCellRangesQuery.hpp>
#include <com/sun/star/sheet/XNamedRanges.hpp>
#include <com/sun/star/sheet/XSheetCellRanges.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

std::string fmtNum(double v) {
    char b[64];
    if (std::fabs(v) >= 1e5 && std::fabs(v) < 1e15) {
        std::snprintf(b, sizeof b, "%.0f", v);
        std::string s = b, out;
        size_t start = s[0] == '-' ? 1 : 0;
        for (size_t i = 0; i < s.size(); ++i) {
            out += s[i];
            size_t rest = s.size() - i - 1;
            if (i >= start && rest > 0 && rest % 3 == 0) out += ',';
        }
        return out;
    }
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

ModelDoc::ModelDoc(const Reference<frame::XModel>& m) : model_(m), doc_(m, UNO_QUERY) {}

std::vector<std::string> ModelDoc::sheetNames() const {
    std::vector<std::string> r;
    for (const auto& n : doc_->getSheets()->getElementNames()) r.push_back(u8(n));
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

Reference<sheet::XSpreadsheet> ModelDoc::sheet(int i) const {
    Reference<container::XIndexAccess> ia(doc_->getSheets(), UNO_QUERY_THROW);
    return Reference<sheet::XSpreadsheet>(ia->getByIndex(i), UNO_QUERY_THROW);
}

Reference<table::XCell> ModelDoc::cell(const table::CellAddress& a) const {
    return sheet(a.Sheet)->getCellByPosition(a.Column, a.Row);
}

std::string ModelDoc::cellName(const table::CellAddress& a, bool withSheet) const {
    std::string s = mc::formula::columnName(a.Column) + std::to_string(a.Row + 1);
    if (withSheet) {
        auto n = sheetNames();
        if (a.Sheet >= 0 && size_t(a.Sheet) < n.size()) s = n[a.Sheet] + "." + s;
    }
    return s;
}

bool ModelDoc::parseAddress(const std::string& text0, table::CellAddress& a) const {
    std::string text;
    for (char c : text0) if (c != '$' && c != ' ') text += c;
    std::string sh, ce = text;
    size_t dot = text.rfind('.');
    if (dot == std::string::npos) dot = text.rfind('!');
    if (dot != std::string::npos) { sh = text.substr(0, dot); ce = text.substr(dot + 1); }
    if (sh.size() >= 2 && sh.front() == '\'' && sh.back() == '\'') sh = sh.substr(1, sh.size() - 2);
    int col, row;
    if (!mc::formula::parseCellName(ce, col, row)) return false;
    if (sh.empty()) {
        table::CellAddress cur;
        a.Sheet = currentCell(cur) ? cur.Sheet : 0;
    } else {
        int i = sheetIndex(sh);
        if (i < 0) return false;
        a.Sheet = sal_Int16(i);
    }
    a.Column = col;
    a.Row = row;
    return true;
}

std::string ModelDoc::defaultName(const table::CellAddress& a) const {
    auto n = sheetNames();
    std::string sh = a.Sheet >= 0 && size_t(a.Sheet) < n.size() ? n[a.Sheet] : "Sheet";
    return mc::formula::sanitizeName(sh) + "_" + mc::formula::columnName(a.Column) + std::to_string(a.Row + 1);
}

std::string ModelDoc::variableNameFromArgs(const VarCell& v, const std::vector<std::string>& args,
                                           const table::CellAddress& a) const {
    const mc::FloFunction* fl = mc::findFlo(v.function);
    if (!fl) return {};
    size_t idx = mc::nameArgIndex(*fl, args);
    if (idx >= args.size()) return {};
    bool isStr;
    std::string t = mc::formula::unquote(args[idx], isStr);
    if (isStr) {
        bool numeric = false;
        parseNumber(t, numeric);
        return numeric ? std::string() : t;
    }
    // a reference to a cell holding the name
    table::CellAddress ref;
    std::string r = t;
    if (!parseAddress(r, ref)) return {};
    if (r.find('.') == std::string::npos && r.find('!') == std::string::npos) ref.Sheet = a.Sheet;
    auto c = cell(ref);
    if (c->getType() != table::CellContentType_TEXT && c->getType() != table::CellContentType_FORMULA) return {};
    std::string text = u8(Reference<text::XText>(c, UNO_QUERY_THROW)->getString());
    bool numeric = false;
    parseNumber(text, numeric);
    return numeric ? std::string() : text;
}

std::vector<VarCell> ModelDoc::scan() const {
    std::vector<VarCell> out;
    auto names = sheetNames();
    for (size_t s = 0; s < names.size(); ++s) {
        if (names[s] == "FLO_Hidden" || names[s] == "FLO_Results" || names[s] == "FLO_Context") continue;
        Reference<sheet::XCellRangesQuery> q(sheet(int(s)), UNO_QUERY_THROW);
        Reference<sheet::XSheetCellRanges> ranges = q->queryContentCells(sheet::CellFlags::FORMULA);
        if (!ranges.is()) continue;
        Reference<container::XEnumeration> en = ranges->getCells()->createEnumeration();
        while (en->hasMoreElements()) {
            Reference<table::XCell> c(en->nextElement(), UNO_QUERY);
            if (!c.is()) continue;
            std::string f = u8(c->getFormula());
            std::string uf = f;
            for (auto& ch : uf) ch = char(std::toupper((unsigned char)ch));
            if (uf.find("FLOSIMULA_") == std::string::npos) continue;
            VarCell v;
            std::vector<std::string> args;
            if (!mc::formula::findCall(f, "FLOSIMULA_", v.function, args)) continue;
            const mc::FloFunction* fl = mc::findFlo(v.function);
            if (!fl) continue;
            v.addr = Reference<sheet::XCellAddressable>(c, UNO_QUERY_THROW)->getCellAddress();
            v.formula = f;
            std::string a;
            for (size_t i = 0; i < args.size(); ++i) a += (i ? "; " : "") + args[i];
            v.description = fl->label + "(" + a + ")";
            if (fl->kind == mc::FloFunction::Output) {
                v.kind = mc::VarKind::Output;
                bool isStr;
                v.name = args.empty() ? std::string() : mc::formula::unquote(args[0], isStr);
                if (!isStr && !v.name.empty()) {
                    table::CellAddress ref;
                    if (parseAddress(v.name, ref)) v.name = u8(Reference<text::XText>(cell(ref), UNO_QUERY_THROW)->getString());
                }
                v.named = !v.name.empty();
                if (!v.named) v.name = defaultName(v.addr);
                v.description = "Output";
            } else if (fl->kind == mc::FloFunction::Correlation) {
                v.kind = mc::VarKind::Correlation;
                bool s1, s2;
                v.corrA = args.size() > 0 ? mc::formula::unquote(args[0], s1) : "";
                v.corrB = args.size() > 1 ? mc::formula::unquote(args[1], s2) : "";
                // names may also be given as references to cells holding them (=FLOsimula_correlation(A1; B1; B2; B4))
                auto resolve = [&](std::string& n, bool isStr) {
                    table::CellAddress ref;
                    std::string r = n;
                    if (isStr || !parseAddress(r, ref)) return;
                    if (r.find('.') == std::string::npos && r.find('!') == std::string::npos) ref.Sheet = v.addr.Sheet;
                    n = u8(Reference<text::XText>(cell(ref), UNO_QUERY_THROW)->getString());
                };
                if (args.size() > 0) resolve(v.corrA, s1);
                if (args.size() > 1) resolve(v.corrB, s2);
                v.name = mc::formula::sanitizeName(v.corrA + "_" + v.corrB);
                v.named = true;
            } else {
                v.kind = mc::VarKind::Input;
                v.name = variableNameFromArgs(v, args, v.addr);
                v.named = !v.name.empty();
                if (!v.named) v.name = defaultName(v.addr);
            }
            out.push_back(v);
        }
    }
    std::sort(out.begin(), out.end(), [](const VarCell& a, const VarCell& b) {
        if (a.addr.Sheet != b.addr.Sheet) return a.addr.Sheet < b.addr.Sheet;
        if (a.addr.Row != b.addr.Row) return a.addr.Row < b.addr.Row;
        return a.addr.Column < b.addr.Column;
    });
    return out;
}

// ---------------------------------------------------------------- named ranges
static Reference<sheet::XNamedRanges> namedRangesOf(const Reference<sheet::XSpreadsheetDocument>& doc) {
    Reference<beans::XPropertySet> ps(doc, UNO_QUERY_THROW);
    return Reference<sheet::XNamedRanges>(ps->getPropertyValue("NamedRanges"), UNO_QUERY_THROW);
}

void ModelDoc::setNamedRange(const std::string& name, const table::CellAddress& a) {
    auto nr = namedRangesOf(doc_);
    OUString n = ou(name);
    if (nr->hasByName(n)) nr->removeByName(n);
    auto sn = sheetNames();
    std::string sh = sn[a.Sheet];
    std::string quoted = "$'" ;
    for (char c : sh) quoted += c == '\'' ? std::string("''") : std::string(1, c);
    quoted += "'.$" + mc::formula::columnName(a.Column) + "$" + std::to_string(a.Row + 1);
    nr->addNewByName(n, ou(quoted), a, 0);
}

void ModelDoc::removeNamedRange(const std::string& name) {
    auto nr = namedRangesOf(doc_);
    if (nr.is() && nr->hasByName(ou(name))) nr->removeByName(ou(name));
}

void ModelDoc::removeNamedRangesWithPrefix(const std::string& prefix) {
    auto nr = namedRangesOf(doc_);
    for (const auto& n : nr->getElementNames())
        if (u8(n).rfind(prefix, 0) == 0) nr->removeByName(n);
}

bool ModelDoc::namedRange(const std::string& name, table::CellRangeAddress& r) const {
    auto nr = namedRangesOf(doc_);
    OUString n = ou(name);
    if (!nr->hasByName(n)) return false;
    Reference<sheet::XCellRangeReferrer> ref(nr->getByName(n), UNO_QUERY);
    if (!ref.is()) return false;
    Reference<table::XCellRange> cr = ref->getReferredCells();
    if (!cr.is()) return false;
    r = Reference<sheet::XCellRangeAddressable>(cr, UNO_QUERY_THROW)->getRangeAddress();
    return true;
}

std::vector<std::pair<std::string, table::CellRangeAddress>> ModelDoc::namedRanges(const std::string& prefix) const {
    std::vector<std::pair<std::string, table::CellRangeAddress>> out;
    auto nr = namedRangesOf(doc_);
    for (const auto& n : nr->getElementNames()) {
        std::string s = u8(n);
        if (s.rfind(prefix, 0) != 0) continue;
        table::CellRangeAddress r;
        if (namedRange(s, r)) out.push_back({s, r});
    }
    return out;
}

// ---------------------------------------------------------------- selection
bool ModelDoc::selectionRange(table::CellRangeAddress& r) const {
    if (!model_.is() || !model_->getCurrentController().is()) return false;
    Reference<view::XSelectionSupplier> sel(model_->getCurrentController(), UNO_QUERY);
    if (!sel.is()) return false;
    Any s = sel->getSelection();
    Reference<sheet::XCellRangeAddressable> ra(s, UNO_QUERY);
    if (ra.is()) { r = ra->getRangeAddress(); return true; }
    Reference<sheet::XSheetCellRanges> multi(s, UNO_QUERY);
    if (multi.is() && multi->getRangeAddresses().getLength() > 0) { r = multi->getRangeAddresses()[0]; return true; }
    return false;
}

bool ModelDoc::currentCell(table::CellAddress& a) const {
    table::CellRangeAddress r;
    if (!selectionRange(r)) return false;
    a.Sheet = r.Sheet;
    a.Column = r.StartColumn;
    a.Row = r.StartRow;
    return true;
}

void ModelDoc::selectRange(const table::CellRangeAddress& r) const {
    if (!model_->getCurrentController().is()) return;
    Reference<sheet::XSpreadsheetView> view(model_->getCurrentController(), UNO_QUERY);
    if (view.is()) view->setActiveSheet(sheet(r.Sheet));
    Reference<view::XSelectionSupplier> sel(model_->getCurrentController(), UNO_QUERY);
    auto range = sheet(r.Sheet)->getCellRangeByPosition(r.StartColumn, r.StartRow, r.EndColumn, r.EndRow);
    if (sel.is()) sel->select(Any(range));
}

void ModelDoc::select(const table::CellAddress& a) const {
    selectRange(table::CellRangeAddress(a.Sheet, a.Column, a.Row, a.Column, a.Row));
}

std::vector<std::vector<double>> ModelDoc::selectionValues(std::vector<std::string>* label) const {
    table::CellRangeAddress r;
    std::vector<std::vector<double>> rows;
    if (!selectionRange(r)) return rows;
    auto range = sheet(r.Sheet)->getCellRangeByPosition(r.StartColumn, r.StartRow, r.EndColumn, r.EndRow);
    auto data = Reference<sheet::XCellRangeData>(range, UNO_QUERY_THROW)->getDataArray();
    for (sal_Int32 i = 0; i < data.getLength(); ++i) {
        std::vector<double> line;
        for (sal_Int32 j = 0; j < data[i].getLength(); ++j) {
            double d;
            OUString s;
            if (data[i][j] >>= d) line.push_back(d);
            else {
                if (label && (data[i][j] >>= s) && !s.isEmpty()) label->push_back(u8(s));
                line.push_back(std::nan(""));
            }
        }
        rows.push_back(line);
    }
    return rows;
}

void ModelDoc::setBackground(const table::CellAddress& a, sal_Int32 rgb) const {
    Reference<beans::XPropertySet>(cell(a), UNO_QUERY_THROW)->setPropertyValue("CellBackColor", Any(rgb));
}

void ModelDoc::recalc() const {
    Reference<sheet::XCalculatable> c(model_, UNO_QUERY);
    if (c.is()) c->calculateAll();
}

// ---------------------------------------------------------------- formula source
namespace {
class CalcSource : public mc::formula::Source {
public:
    explicit CalcSource(const ModelDoc& d) : d_(d), names_(d.sheetNames()) {
        for (size_t i = 0; i < names_.size(); ++i) sheets_.push_back(d.sheet(int(i)));
    }
    mc::formula::CellContent cell(const mc::formula::CellAddr& a) override {
        mc::formula::CellContent c;
        if (a.sheet < 0 || size_t(a.sheet) >= sheets_.size()) return c;
        Reference<table::XCell> x = sheets_[a.sheet]->getCellByPosition(a.col, a.row);
        switch (x->getType()) {
        case table::CellContentType_VALUE: c.type = mc::formula::CellContent::Number; c.value = x->getValue(); break;
        case table::CellContentType_TEXT:
            c.type = mc::formula::CellContent::Text;
            c.text = u8(Reference<text::XText>(x, UNO_QUERY_THROW)->getString());
            break;
        case table::CellContentType_FORMULA: c.type = mc::formula::CellContent::Formula; c.text = u8(x->getFormula()); break;
        default: break;
        }
        return c;
    }
    int sheetIndex(const std::string& n) override { return d_.sheetIndex(n); }
    bool namedRange(const std::string& n, mc::formula::RangeAddr& r) override {
        table::CellRangeAddress a;
        if (!d_.namedRange(n, a)) return false;
        r.a = {a.Sheet, a.StartColumn, a.StartRow};
        r.b = {a.Sheet, a.EndColumn, a.EndRow};
        return true;
    }

private:
    const ModelDoc& d_;
    std::vector<std::string> names_;
    std::vector<Reference<sheet::XSpreadsheet>> sheets_;
};
} // namespace

std::unique_ptr<mc::formula::Source> ModelDoc::formulaSource() const { return std::make_unique<CalcSource>(*this); }

} // namespace montecalc
