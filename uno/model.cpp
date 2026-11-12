// MonteCalc - access to the model document
#include "model.hpp"
#include "util.hpp"

#include "../core/flofuncs.h"

#ifndef MONTECALC_XLL
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
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XPropertyContainer.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/chart/ChartAxisLabelPosition.hpp>
#include <com/sun/star/chart/XAxisXSupplier.hpp>
#include <com/sun/star/chart/XChartDocument.hpp>
#include <com/sun/star/chart/XDiagram.hpp>
#include <com/sun/star/document/XEmbeddedObjectSupplier.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/sheet/XArrayFormulaRange.hpp>
#include <com/sun/star/table/XTableChart.hpp>
#include <com/sun/star/table/XTableCharts.hpp>
#include <com/sun/star/table/XTableChartsSupplier.hpp>
#include <com/sun/star/util/XNumberFormats.hpp>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/util/XProtectable.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <com/sun/star/sheet/XSheetAnnotationsSupplier.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/table/XColumnRowRange.hpp>
#include <com/sun/star/table/XTableColumns.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
#ifndef MONTECALC_XLL
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;
#endif

namespace montecalc {

std::string u8s(const OUString& s) { return u8(s); }

#ifndef MONTECALC_XLL
// ================================================================ LibreOffice backend
ModelDoc::ModelDoc(const Reference<frame::XModel>& m) : model_(m), doc_(m, UNO_QUERY) {}

std::string Cell::formula() const { return u8(c_->getFormula()); }
void Cell::setFormula(const std::string& f) const { c_->setFormula(ou(f)); }
double Cell::value() const { return c_->getValue(); }
void Cell::setValue(double v) const { c_->setValue(v); }
std::string Cell::text() const { return u8(Reference<text::XText>(c_, UNO_QUERY_THROW)->getString()); }
void Cell::setText(const std::string& t) const { Reference<text::XText>(c_, UNO_QUERY_THROW)->setString(ou(t)); }
CellType Cell::type() const {
    switch (c_->getType()) {
    case table::CellContentType_VALUE: return CellType::Value;
    case table::CellContentType_TEXT: return CellType::Text;
    case table::CellContentType_FORMULA: return CellType::Formula;
    default: return CellType::Empty;
    }
}
int Cell::error() const { return c_->getError(); }

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

Cell ModelDoc::cell(const table::CellAddress& a) const {
    return Cell(sheet(a.Sheet)->getCellByPosition(a.Column, a.Row));
}

std::vector<std::pair<table::CellAddress, std::string>> ModelDoc::formulaCells(int s) const {
    std::vector<std::pair<table::CellAddress, std::string>> out;
    Reference<sheet::XCellRangesQuery> q(sheet(s), UNO_QUERY_THROW);
    Reference<sheet::XSheetCellRanges> ranges = q->queryContentCells(sheet::CellFlags::FORMULA);
    if (!ranges.is()) return out;
    Reference<container::XEnumeration> en = ranges->getCells()->createEnumeration();
    while (en->hasMoreElements()) {
        Reference<table::XCell> c(en->nextElement(), UNO_QUERY);
        if (!c.is()) continue;
        out.push_back({Reference<sheet::XCellAddressable>(c, UNO_QUERY_THROW)->getCellAddress(), u8(c->getFormula())});
    }
    return out;
}

std::vector<table::CellAddress> ModelDoc::valueCells(int s) const {
    std::vector<table::CellAddress> out;
    Reference<sheet::XCellRangesQuery> q(sheet(s), UNO_QUERY_THROW);
    Reference<sheet::XSheetCellRanges> ranges = q->queryContentCells(sheet::CellFlags::VALUE);
    if (!ranges.is()) return out;
    Reference<container::XEnumeration> en = ranges->getCells()->createEnumeration();
    while (en->hasMoreElements()) {
        Reference<sheet::XCellAddressable> c(en->nextElement(), UNO_QUERY);
        if (c.is()) out.push_back(c->getCellAddress());
    }
    return out;
}

int ModelDoc::ensureSheet(const std::string& name, bool visible) {
    Reference<sheet::XSpreadsheets> sheets = doc_->getSheets();
    if (!sheets->hasByName(ou(name))) sheets->insertNewByName(ou(name), sal_Int16(sheetNames().size()));
    Reference<beans::XPropertySet>(sheets->getByName(ou(name)), UNO_QUERY_THROW)->setPropertyValue("IsVisible", Any(visible));
    return sheetIndex(name);
}

void ModelDoc::removeSheet(const std::string& name) {
    Reference<sheet::XSpreadsheets> sheets = doc_->getSheets();
    if (sheets->hasByName(ou(name))) sheets->removeByName(ou(name));
}

void ModelDoc::renameSheet(int s, const std::string& name) {
    Reference<container::XNamed>(sheet(s), UNO_QUERY_THROW)->setName(ou(name));
}

void ModelDoc::writeTable(int s, int row, int col, const std::vector<std::vector<Any>>& rows) const {
    if (rows.empty()) return;
    size_t w = 0;
    for (auto& r : rows) w = std::max(w, r.size());
    if (w == 0) return;
    uno::Sequence<uno::Sequence<Any>> data(sal_Int32(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        uno::Sequence<Any> line(static_cast<sal_Int32>(w));
        for (size_t j = 0; j < w; ++j) line.getArray()[j] = j < rows[i].size() && rows[i][j].hasValue() ? rows[i][j] : Any(OUString());
        data.getArray()[i] = line;
    }
    auto rg = sheet(s)->getCellRangeByPosition(col, row, col + sal_Int32(w) - 1, row + sal_Int32(rows.size()) - 1);
    Reference<sheet::XCellRangeData>(rg, UNO_QUERY_THROW)->setDataArray(data);
}

void ModelDoc::optimalWidth(int s, int firstCol, int nCols) const {
    auto cols = Reference<table::XColumnRowRange>(sheet(s), UNO_QUERY_THROW)->getColumns();
    for (int c = firstCol; c < firstCol + nCols; ++c)
        Reference<beans::XPropertySet>(cols->getByIndex(c), UNO_QUERY_THROW)->setPropertyValue("OptimalWidth", Any(true));
}

void ModelDoc::setComment(const table::CellAddress& a, const std::string& text) const {
    Reference<sheet::XSheetAnnotationsSupplier> s(sheet(a.Sheet), UNO_QUERY);
    if (s.is()) s->getAnnotations()->insertNew(a, ou(text));
}

void ModelDoc::setNamedArea(const std::string& name, int s, int col, int row0, int row1) {
    Reference<beans::XPropertySet> ps(doc_, UNO_QUERY_THROW);
    Reference<sheet::XNamedRanges> nr(ps->getPropertyValue("NamedRanges"), UNO_QUERY_THROW);
    if (nr->hasByName(ou(name))) nr->removeByName(ou(name));
    std::string c = mc::formula::columnName(col), sh = sheetNames()[s];
    nr->addNewByName(ou(name), ou("$'" + sh + "'.$" + c + "$" + std::to_string(row0 + 1) + ":$" + c + "$" + std::to_string(row1 + 1)),
                     table::CellAddress(sal_Int16(s), 0, 0), 0);
}

static Reference<beans::XPropertyContainer> userProps(const Reference<frame::XModel>& m) {
    Reference<document::XDocumentPropertiesSupplier> s(m, UNO_QUERY);
    if (!s.is()) return {};
    return s->getDocumentProperties()->getUserDefinedProperties();
}

static Any readPropAny(const Reference<frame::XModel>& m, const std::string& name) {
    Reference<beans::XPropertySet> ps(userProps(m), UNO_QUERY);
    OUString n = ou(name);
    if (!ps.is() || !ps->getPropertySetInfo()->hasPropertyByName(n)) return {};
    return ps->getPropertyValue(n);
}

double ModelDoc::property(const std::string& name, double def) const {
    Any a = readPropAny(model_, name);
    double d;
    if (a >>= d) return d;
    OUString s;
    if (a >>= s) { bool ok; double v = parseNumber(u8(s), ok); if (ok) return v; }
    return def;
}

std::string ModelDoc::propertyText(const std::string& name) const {
    OUString s;
    readPropAny(model_, name) >>= s;
    return u8(s);
}

void ModelDoc::setProperty(const std::string& name, const Any& v) const {
    auto c = userProps(model_);
    Reference<beans::XPropertySet> ps(c, UNO_QUERY);
    if (!ps.is()) return;
    OUString n = ou(name);
    if (ps->getPropertySetInfo()->hasPropertyByName(n)) c->removeProperty(n);
    if (v.hasValue()) c->addProperty(n, beans::PropertyAttribute::REMOVABLE, v);
}

const void* ModelDoc::key() const { return model_.get(); }

void ModelDoc::saveAs(const std::string& url) const {
    beans::PropertyValue pv;
    pv.Name = "FilterName";
    pv.Value <<= OUString("calc8");
    Reference<frame::XStorable>(model_, UNO_QUERY_THROW)->storeAsURL(ou(url), seq<beans::PropertyValue>({pv}));
}

void ModelDoc::close() const {
    Reference<util::XCloseable> cl(model_, UNO_QUERY);
    if (cl.is()) cl->close(true);
}

int ModelDoc::insertSheet(const std::string& name, int pos) {
    auto sheets = doc_->getSheets();
    if (sheets->hasByName(ou(name))) sheets->removeByName(ou(name));
    Reference<container::XIndexAccess> ia(sheets, UNO_QUERY_THROW);
    sheets->insertNewByName(ou(name), sal_Int16(std::min<sal_Int32>(pos, ia->getCount())));
    return sheetIndex(name);
}

void ModelDoc::numberFormat(int s, int c0, int r0, int c1, int r1, const std::string& code) const {
    Reference<util::XNumberFormats> nf = Reference<util::XNumberFormatsSupplier>(doc_, UNO_QUERY_THROW)->getNumberFormats();
    lang::Locale loc;
    sal_Int32 k = nf->queryKey(ou(code), loc, false);
    if (k == -1) k = nf->addNew(ou(code), loc);
    Reference<beans::XPropertySet>(sheet(s)->getCellRangeByPosition(c0, r0, c1, r1), UNO_QUERY_THROW)->setPropertyValue("NumberFormat", Any(k));
}

void ModelDoc::protectSheet(int s) const { Reference<util::XProtectable>(sheet(s), UNO_QUERY_THROW)->protect(OUString()); }

void ModelDoc::setArrayFormula(int s, int c0, int r0, int c1, int r1, const std::string& f) const {
    Reference<sheet::XArrayFormulaRange>(sheet(s)->getCellRangeByPosition(c0, r0, c1, r1), UNO_QUERY_THROW)->setArrayFormula(ou(f));
}

void ModelDoc::activateSheet(int s) const {
    if (!model_->getCurrentController().is()) return;
    Reference<sheet::XSpreadsheetView> view(model_->getCurrentController(), UNO_QUERY);
    if (view.is()) view->setActiveSheet(sheet(s));
}

std::string ModelDoc::url() const { return u8(model_->getURL()); }

void ModelDoc::addChart(int s, const std::string& name, ChartKind kind, const std::vector<table::CellRangeAddress>& ranges, int x, int y, int w, int h,
                        const std::string& title, bool legend) const {
    Reference<table::XTableCharts> charts = Reference<table::XTableChartsSupplier>(sheet(s), UNO_QUERY_THROW)->getCharts();
    OUString n = ou(name);
    if (charts->hasByName(n)) charts->removeByName(n);
    uno::Sequence<table::CellRangeAddress> rs(sal_Int32(ranges.size()));
    for (size_t i = 0; i < ranges.size(); ++i) rs.getArray()[i] = ranges[i];
    charts->addNewByName(n, awt::Rectangle(x, y, w, h), rs, true, true);
    Reference<table::XTableChart> tc(charts->getByName(n), UNO_QUERY_THROW);
    Reference<chart::XChartDocument> doc(Reference<document::XEmbeddedObjectSupplier>(tc, UNO_QUERY_THROW)->getEmbeddedObject(), UNO_QUERY_THROW);
    if (kind == ChartKind::Line) {
        Reference<lang::XMultiServiceFactory> f(doc, UNO_QUERY_THROW);
        doc->setDiagram(Reference<chart::XDiagram>(f->createInstance("com.sun.star.chart.LineDiagram"), UNO_QUERY_THROW));
    }
    Reference<beans::XPropertySet> dp(doc, UNO_QUERY_THROW);
    dp->setPropertyValue("HasMainTitle", Any(true));
    Reference<beans::XPropertySet>(doc->getTitle(), UNO_QUERY_THROW)->setPropertyValue("String", Any(ou(title)));
    dp->setPropertyValue("HasLegend", Any(legend));
    if (kind == ChartKind::Tornado) {
        Reference<beans::XPropertySet>(doc->getDiagram(), UNO_QUERY_THROW)->setPropertyValue("Vertical", Any(true));
        Reference<chart::XAxisXSupplier> xs(doc->getDiagram(), UNO_QUERY);
        if (xs.is() && xs->getXAxis().is()) xs->getXAxis()->setPropertyValue("LabelPosition", Any(chart::ChartAxisLabelPosition_OUTSIDE_START));
    }
}

ModelDoc ModelDoc::newDocument(const Reference<uno::XComponentContext>& ctx, bool hidden) {
    Reference<frame::XComponentLoader> loader(frame::Desktop::create(ctx), UNO_QUERY_THROW);
    beans::PropertyValue h;
    h.Name = "Hidden";
    h.Value <<= hidden;
    Reference<frame::XModel> m(loader->loadComponentFromURL("private:factory/scalc", "_blank", 0, seq<beans::PropertyValue>({h})), UNO_QUERY_THROW);
    return ModelDoc(m);
}
#endif   // LibreOffice backend

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
    if (c.type() != CellType::Text && c.type() != CellType::Formula) return {};
    std::string text = c.text();
    bool numeric = false;
    parseNumber(text, numeric);
    return numeric ? std::string() : text;
}

std::vector<VarCell> ModelDoc::scan() const {
    std::vector<VarCell> out;
    auto names = sheetNames();
    for (size_t s = 0; s < names.size(); ++s) {
        if (names[s] == "FLO_Hidden" || names[s] == "FLO_Results" || names[s] == "FLO_Context") continue;
        for (auto& [cellAddr, f] : formulaCells(int(s))) {
            std::string uf = f;
            for (auto& ch : uf) ch = char(std::toupper((unsigned char)ch));
            if (uf.find("FLOSIMULA_") == std::string::npos) continue;
            VarCell v;
            std::vector<std::string> args;
            if (!mc::formula::findCall(f, "FLOSIMULA_", v.function, args)) continue;
            const mc::FloFunction* fl = mc::findFlo(v.function);
            if (!fl) continue;
            v.addr = cellAddr;
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
                    if (parseAddress(v.name, ref)) v.name = cell(ref).text();
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
                    n = cell(ref).text();
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

#ifndef MONTECALC_XLL
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
    Reference<beans::XPropertySet>(cell(a).xcell(), UNO_QUERY_THROW)->setPropertyValue("CellBackColor", Any(rgb));
}

void ModelDoc::recalc() const {
    Reference<sheet::XCalculatable> c(model_, UNO_QUERY);
    if (c.is()) c->calculateAll();
}

#endif   // LibreOffice backend

// ---------------------------------------------------------------- formula source
namespace {
class CalcSource : public mc::formula::Source {
public:
    explicit CalcSource(const ModelDoc& d) : d_(d), names_(d.sheetNames()) {}
    mc::formula::CellContent cell(const mc::formula::CellAddr& a) override {
        mc::formula::CellContent c;
        if (a.sheet < 0 || size_t(a.sheet) >= names_.size()) return c;
        Cell x = d_.cell(table::CellAddress(sal_Int16(a.sheet), a.col, a.row));
        switch (x.type()) {
        case CellType::Value: c.type = mc::formula::CellContent::Number; c.value = x.value(); break;
        case CellType::Text: c.type = mc::formula::CellContent::Text; c.text = x.text(); break;
        case CellType::Formula: c.type = mc::formula::CellContent::Formula; c.text = x.formula(); break;
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
};
} // namespace

std::unique_ptr<mc::formula::Source> ModelDoc::formulaSource() const { return std::make_unique<CalcSource>(*this); }

} // namespace montecalc
