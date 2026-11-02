// MonteCalc - result workbook
#include "report.hpp"
#include "util.hpp"

#include "../core/formula.h"
#include "../core/simulation.h"
#include "../core/stats.h"

#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XPropertyContainer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/chart/ChartAxisLabelPosition.hpp>
#include <com/sun/star/chart/XAxisXSupplier.hpp>
#include <com/sun/star/chart/XChartDocument.hpp>
#include <com/sun/star/chart/XDiagram.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/document/XEmbeddedObjectSupplier.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/table/XColumnRowRange.hpp>
#include <com/sun/star/table/XTableChart.hpp>
#include <com/sun/star/table/XTableCharts.hpp>
#include <com/sun/star/table/XTableChartsSupplier.hpp>
#include <com/sun/star/util/XNumberFormats.hpp>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/util/XProtectable.hpp>
#include <com/sun/star/sheet/XArrayFormulaRange.hpp>
#include <com/sun/star/text/XText.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

namespace {

using Row = std::vector<Any>;
using Vec = std::vector<double>;

Any num(double v) { return std::isfinite(v) ? Any(v) : Any(OUString()); }
Any txt(const std::string& s) { return Any(ou(s)); }

void writeBlock(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 col, sal_Int32 row, const std::vector<Row>& rows) {
    if (rows.empty()) return;
    size_t w = 0;
    for (const auto& r : rows) w = std::max(w, r.size());
    Sequence<Sequence<Any>> data(sal_Int32(rows.size()));
    auto* d = data.getArray();
    for (size_t i = 0; i < rows.size(); ++i) {
        Sequence<Any> r(static_cast<sal_Int32>(w));
        auto* p = r.getArray();
        for (size_t j = 0; j < w; ++j) p[j] = j < rows[i].size() ? rows[i][j] : Any(OUString());
        d[i] = r;
    }
    auto range = sh->getCellRangeByPosition(col, row, col + sal_Int32(w) - 1, row + sal_Int32(rows.size()) - 1);
    Reference<sheet::XCellRangeData>(range, UNO_QUERY_THROW)->setDataArray(data);
}

table::CellRangeAddress address(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 c0, sal_Int32 r0, sal_Int32 c1, sal_Int32 r1) {
    auto range = sh->getCellRangeByPosition(c0, r0, c1, r1);
    return Reference<sheet::XCellRangeAddressable>(range, UNO_QUERY_THROW)->getRangeAddress();
}

Reference<chart::XChartDocument> addChart(const Reference<sheet::XSpreadsheet>& sh, const std::string& name, const awt::Rectangle& rect,
                                          const Sequence<table::CellRangeAddress>& ranges, const std::string& title,
                                          const char* diagramService, bool legend = false) {
    Reference<table::XTableCharts> charts = Reference<table::XTableChartsSupplier>(sh, UNO_QUERY_THROW)->getCharts();
    OUString n = ou(name);
    if (charts->hasByName(n)) charts->removeByName(n);
    charts->addNewByName(n, rect, ranges, true, true);
    Reference<table::XTableChart> tc(charts->getByName(n), UNO_QUERY_THROW);
    Reference<chart::XChartDocument> doc(Reference<document::XEmbeddedObjectSupplier>(tc, UNO_QUERY_THROW)->getEmbeddedObject(), UNO_QUERY_THROW);
    if (diagramService) {
        Reference<lang::XMultiServiceFactory> f(doc, UNO_QUERY_THROW);
        doc->setDiagram(Reference<chart::XDiagram>(f->createInstance(OUString::createFromAscii(diagramService)), UNO_QUERY_THROW));
    }
    Reference<beans::XPropertySet> dp(doc, UNO_QUERY_THROW);
    dp->setPropertyValue("HasMainTitle", Any(true));
    Reference<beans::XPropertySet>(doc->getTitle(), UNO_QUERY_THROW)->setPropertyValue("String", Any(ou(title)));
    dp->setPropertyValue("HasLegend", Any(legend));
    return doc;
}

void tornadoLayout(const Reference<chart::XChartDocument>& chart) {
    Reference<beans::XPropertySet>(chart->getDiagram(), UNO_QUERY_THROW)->setPropertyValue("Vertical", Any(true));
    Reference<chart::XAxisXSupplier> xs(chart->getDiagram(), UNO_QUERY);
    if (xs.is() && xs->getXAxis().is())
        xs->getXAxis()->setPropertyValue("LabelPosition", Any(chart::ChartAxisLabelPosition_OUTSIDE_START));
}

std::string label(double x) {
    char b[64];
    if (std::fabs(x) >= 1000) {
        std::snprintf(b, sizeof b, "%.0f", x);
        std::string s = b, out;
        size_t start = s[0] == '-' ? 1 : 0;
        for (size_t i = 0; i < s.size(); ++i) {
            out += s[i];
            size_t rest = s.size() - i - 1;
            if (i >= start && rest > 0 && rest % 3 == 0) out += ',';
        }
        return out;
    }
    std::snprintf(b, sizeof b, "%.3g", x);
    return b;
}

std::string pct(double p) {
    char b[32];
    std::snprintf(b, sizeof b, "P%g%%", std::round(p * 1000) / 10);
    return b;
}

void autofit(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 ncols) {
    auto cols = Reference<table::XColumnRowRange>(sh, UNO_QUERY_THROW)->getColumns();
    for (sal_Int32 c = 0; c < ncols && c < cols->getCount(); ++c)
        Reference<beans::XPropertySet>(cols->getByIndex(c), UNO_QUERY_THROW)->setPropertyValue("OptimalWidth", Any(true));
}

sal_Int32 numberFormat(const Reference<sheet::XSpreadsheetDocument>& doc, const char* code) {
    Reference<util::XNumberFormats> nf = Reference<util::XNumberFormatsSupplier>(doc, UNO_QUERY_THROW)->getNumberFormats();
    lang::Locale loc;
    sal_Int32 k = nf->queryKey(OUString::createFromAscii(code), loc, false);
    return k != -1 ? k : nf->addNew(OUString::createFromAscii(code), loc);
}

const char* formatFor(const Vec& v) {
    double mag = std::max(std::fabs(mc::stats::min(v)), std::fabs(mc::stats::max(v)));
    return mag >= 1000 ? "#,##0" : mag >= 10 ? "#,##0.00" : "0.0000";
}

struct Results {
    std::vector<std::string> names;  // outputs first, then inputs
    std::vector<std::string> kinds;
    std::vector<Vec> data;
    std::vector<std::string> desc, cells;
    size_t nOut = 0;
    int n = 0;
    mc::Settings st;
};

Results collect() {
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) throw std::runtime_error("No simulation results available. Run a simulation first.");
    Results r;
    r.st = sim.settings();
    r.n = sim.iterations();
    auto vars = sim.resultVariables();
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& v : vars) {
            bool out = v.kind == mc::VarKind::Output;
            if (out != (pass == 0)) continue;
            Vec d;
            sim.samples(v.name, d);
            r.names.push_back(v.name);
            r.kinds.push_back(out ? "Output" : "Input");
            r.data.push_back(d);
            r.desc.push_back(v.description);
            r.cells.push_back(v.cell);
            if (out) ++r.nOut;
        }
    return r;
}

Reference<sheet::XSpreadsheet> sheetNamed(const Reference<sheet::XSpreadsheetDocument>& doc, const std::string& name) {
    return Reference<sheet::XSpreadsheet>(doc->getSheets()->getByName(ou(name)), UNO_QUERY_THROW);
}

void ensureSheet(const Reference<sheet::XSpreadsheetDocument>& doc, const std::string& name, sal_Int16 pos) {
    auto sheets = doc->getSheets();
    if (sheets->hasByName(ou(name))) sheets->removeByName(ou(name));
    Reference<container::XIndexAccess> ia(sheets, UNO_QUERY_THROW);
    sheets->insertNewByName(ou(name), std::min<sal_Int16>(pos, sal_Int16(ia->getCount())));
}

void writeSummary(const Reference<sheet::XSpreadsheetDocument>& doc, const Results& r) {
    auto sh = sheetNamed(doc, "Summary");
    std::vector<Row> rows;
    Row h{txt("Statistic")};
    for (auto& a : r.names) h.push_back(txt(a));
    rows.push_back(h);
    auto line = [&](const std::string& lbl, auto fn) {
        Row x{txt(lbl)};
        for (size_t i = 0; i < r.names.size(); ++i) x.push_back(fn(i));
        rows.push_back(std::move(x));
    };
    line("Kind", [&](size_t i) { return txt(r.kinds[i]); });
    line("Definition", [&](size_t i) { return txt(r.desc[i]); });
    line("Cell", [&](size_t i) { return txt(r.cells[i]); });
    const sal_Int32 firstStat = sal_Int32(rows.size());
    line("Iterations", [&](size_t i) { return num(mc::stats::count(r.data[i])); });
    line("Mean", [&](size_t i) { return num(mc::stats::mean(r.data[i])); });
    line("Minimum", [&](size_t i) { return num(mc::stats::min(r.data[i])); });
    line("Maximum", [&](size_t i) { return num(mc::stats::max(r.data[i])); });
    line("Variance", [&](size_t i) { return num(mc::stats::variance(r.data[i])); });
    line("Standard deviation", [&](size_t i) { return num(mc::stats::stdev(r.data[i])); });
    line("Worst case (" + pct(r.st.worst) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.worst)); });
    line("Real case (" + pct(r.st.real) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.real)); });
    line("Best case (" + pct(r.st.best) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.best)); });
    const sal_Int32 shapeRow = sal_Int32(rows.size());
    line("Skewness", [&](size_t i) { return num(mc::stats::skewness(r.data[i])); });
    line("Kurtosis (excess)", [&](size_t i) { return num(mc::stats::kurtosis(r.data[i])); });
    line("Coefficient of variation", [&](size_t i) { return num(mc::stats::cv(r.data[i])); });
    std::vector<double> ps;
    for (double p : {0.01, 0.02, 0.03, 0.04}) ps.push_back(p);
    for (int k = 5; k <= 95; k += 5) ps.push_back(k / 100.0);
    for (double p : {0.96, 0.97, 0.98, 0.99}) ps.push_back(p);
    for (double p : ps) line(pct(p), [&, p](size_t i) { return num(mc::stats::percentile(r.data[i], p)); });
    // +/- 3 percentage points around the real case
    for (int k = -3; k <= 3; ++k) {
        if (k == 0) continue;
        double p = std::min(1.0, std::max(0.0, r.st.real + k / 100.0));
        line("Real case " + std::string(k > 0 ? "+" : "") + std::to_string(k) + "% (" + pct(p) + ")",
             [&, p](size_t i) { return num(mc::stats::percentile(r.data[i], p)); });
    }
    writeBlock(sh, 0, 0, rows);
    for (size_t i = 0; i < r.names.size(); ++i) {
        auto col = sh->getCellRangeByPosition(sal_Int32(i) + 1, firstStat + 1, sal_Int32(i) + 1, sal_Int32(rows.size()) - 1);
        Reference<beans::XPropertySet>(col, UNO_QUERY_THROW)->setPropertyValue("NumberFormat", Any(numberFormat(doc, formatFor(r.data[i]))));
    }
    auto shape = sh->getCellRangeByPosition(1, shapeRow, sal_Int32(r.names.size()), shapeRow + 2);
    Reference<beans::XPropertySet>(shape, UNO_QUERY_THROW)->setPropertyValue("NumberFormat", Any(numberFormat(doc, "0.0000")));
    writeBlock(sh, 0, sal_Int32(rows.size()) + 1, {{txt("Seed"), Any(double(r.st.seed))}, {txt("Generated by"), txt("MonteCalc")}});
    autofit(sh, sal_Int32(r.names.size()) + 1);
}

void writeStatistics(const Reference<sheet::XSpreadsheetDocument>& doc, const Results& r, int bins) {
    auto sh = sheetNamed(doc, "Statistics");
    sal_Int32 row = 0;
    bins = std::max(2, bins);
    const sal_Int32 blockH = std::max<sal_Int32>(bins + 3, 24);
    for (size_t i = 0; i < r.names.size(); ++i) {
        const Vec& v = r.data[i];
        auto hst = mc::stats::histogram(v, bins);
        if (hst.count.empty()) continue;
        double total = 0, cum = 0;
        for (double c : hst.count) total += c;
        double worst = mc::stats::percentile(v, r.st.worst), best = mc::stats::percentile(v, r.st.best);
        std::vector<Row> rows{{txt(r.names[i]), txt("Percent"), txt("Cumulative"), txt("Count"), txt("Scenario")}};
        for (size_t b = 0; b < hst.count.size(); ++b) {
            cum += hst.count[b];
            std::string sc;
            if (worst >= hst.lo[b] && worst <= hst.hi[b]) sc += "worst ";
            if (best >= hst.lo[b] && best <= hst.hi[b]) sc += "best ";
            if (0 >= hst.lo[b] && 0 <= hst.hi[b]) sc += "limit 0";
            rows.push_back({txt(label((hst.lo[b] + hst.hi[b]) / 2)), Any(hst.count[b] / total), Any(cum / total), Any(hst.count[b]), txt(sc)});
        }
        writeBlock(sh, 0, row, rows);
        // statistics next to the table
        double m = mc::stats::mean(v), s = mc::stats::stdev(v), n = mc::stats::count(v);
        double z90 = 1.6448536;
        writeBlock(sh, 5, row, {{txt("Statistic"), txt(r.names[i])},
                                {txt("Mean"), num(m)},
                                {txt("Mean, 90% confidence lower"), num(m - z90 * s / std::sqrt(n))},
                                {txt("Mean, 90% confidence upper"), num(m + z90 * s / std::sqrt(n))},
                                {txt("Standard deviation"), num(s)},
                                {txt("Worst case (" + pct(r.st.worst) + ")"), num(worst)},
                                {txt("Real case (" + pct(r.st.real) + ")"), num(mc::stats::percentile(v, r.st.real))},
                                {txt("Best case (" + pct(r.st.best) + ")"), num(best)},
                                {txt("P(value < 0) (limit value)"), num(double(std::count_if(v.begin(), v.end(), [](double x) { return x < 0; })) / n)}});
        sal_Int32 last = row + sal_Int32(hst.count.size());
        sal_Int32 top = row * 452;
        addChart(sh, "hist_" + std::to_string(i), awt::Rectangle(17500, top, 11000, 9000), seq<table::CellRangeAddress>({address(sh, 0, row, 1, last)}),
                 r.names[i] + " - histogram", nullptr);
        addChart(sh, "cdf_" + std::to_string(i), awt::Rectangle(29000, top, 9500, 9000),
                 seq<table::CellRangeAddress>({address(sh, 0, row, 0, last), address(sh, 2, row, 2, last)}), r.names[i] + " - cumulative",
                 "com.sun.star.chart.LineDiagram");
        row += blockH;
    }
    autofit(sh, 7);
}

void writeTornados(const Reference<sheet::XSpreadsheetDocument>& doc, const Results& r) {
    auto sh = sheetNamed(doc, "Sensitivity");
    sal_Int32 row = 0;
    for (size_t o = 0; o < r.nOut; ++o) {
        std::vector<std::pair<std::string, double>> d;
        for (size_t i = r.nOut; i < r.names.size(); ++i) {
            double c = mc::stats::spearman(r.data[i], r.data[o]);
            d.push_back({r.names[i], std::isfinite(c) ? c : 0.0});
        }
        if (d.empty()) break;
        std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) < std::fabs(b.second); });
        std::vector<Row> rows{{txt("Input"), txt("Rank correlation (Spearman) with " + r.names[o])}};
        for (auto& [nm, c] : d) rows.push_back({txt(nm), Any(c)});
        writeBlock(sh, 0, row, rows);
        sal_Int32 last = row + sal_Int32(d.size());
        sal_Int32 h = std::max<sal_Int32>(6000, 600 * sal_Int32(d.size()) + 2500);
        auto chart = addChart(sh, "tornado_" + std::to_string(o), awt::Rectangle(9000, row * 452, 14000, h),
                              seq<table::CellRangeAddress>({address(sh, 0, row, 1, last)}), "Tornado: " + r.names[o], nullptr);
        tornadoLayout(chart);
        row = std::max(last + 3, row + h / 452 + 2);
    }
    autofit(sh, 2);
}

void writeVariants(const Reference<sheet::XSpreadsheetDocument>& doc) {
    auto& sim = mc::Simulation::instance();
    auto labels = sim.resultSetLabels();
    auto sh = sheetNamed(doc, "Variants");
    auto outs = sim.resultNames(mc::VarKind::Output);
    mc::Settings st = sim.settings();
    sal_Int32 row = 0;
    for (const auto& o : outs) {
        std::vector<Row> rows{{txt(o), txt("Mean"), txt("Worst (" + pct(st.worst) + ")"), txt("Real (" + pct(st.real) + ")"),
                               txt("Best (" + pct(st.best) + ")"), txt("Std dev")}};
        for (size_t v = 0; v < labels.size(); ++v) {
            Vec d;
            sim.samplesIn(int(v), o, d);
            rows.push_back({txt(labels[v]), num(mc::stats::mean(d)), num(mc::stats::percentile(d, st.worst)), num(mc::stats::percentile(d, st.real)),
                            num(mc::stats::percentile(d, st.best)), num(mc::stats::stdev(d))});
        }
        writeBlock(sh, 0, row, rows);
        sal_Int32 last = row + sal_Int32(labels.size());
        addChart(sh, "variants_" + o, awt::Rectangle(15000, row * 452, 12000, 7000), seq<table::CellRangeAddress>({address(sh, 0, row, 4, last)}),
                 o + " by variant", nullptr, true);
        row = std::max(last + 3, row + 17);
    }
    autofit(sh, 6);
}

void setDocProp(const Reference<lang::XComponent>& comp, const char* name, const Any& v) {
    Reference<document::XDocumentPropertiesSupplier> s(comp, UNO_QUERY);
    if (!s.is()) return;
    auto c = s->getDocumentProperties()->getUserDefinedProperties();
    Reference<beans::XPropertySet> ps(c, UNO_QUERY);
    OUString n = OUString::createFromAscii(name);
    if (ps->getPropertySetInfo()->hasPropertyByName(n)) c->removeProperty(n);
    c->addProperty(n, beans::PropertyAttribute::REMOVABLE, v);
}

} // namespace

std::string createReport(const Reference<uno::XComponentContext>& ctx, bool hidden, Reference<lang::XComponent>* created,
                         const Reference<frame::XModel>& model) {
    Results r = collect();
    auto& sim = mc::Simulation::instance();
    Reference<frame::XComponentLoader> loader(frame::Desktop::create(ctx), UNO_QUERY_THROW);
    beans::PropertyValue pv;
    pv.Name = "Hidden";
    pv.Value <<= hidden;
    Reference<lang::XComponent> comp = loader->loadComponentFromURL("private:factory/scalc", "_blank", 0, seq<beans::PropertyValue>({pv}));
    if (created) *created = comp;
    Reference<sheet::XSpreadsheetDocument> doc(comp, UNO_QUERY_THROW);
    Reference<container::XIndexAccess> ia(doc->getSheets(), UNO_QUERY_THROW);
    Reference<container::XNamed>(ia->getByIndex(0), UNO_QUERY_THROW)->setName("Summary");
    doc->getSheets()->insertNewByName("Statistics", 1);
    doc->getSheets()->insertNewByName("Sensitivity", 2);
    sal_Int16 next = 3;
    if (sim.resultSetLabels().size() > 1) doc->getSheets()->insertNewByName("Variants", next++);
    doc->getSheets()->insertNewByName("Data", next);
    std::string note;
    // ---------------- Data ----------------
    {
        auto sh = sheetNamed(doc, "Data");
        const int maxRows = 1000000;
        if (r.n > maxRows) {
            std::string path = std::string(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") + "/MonteCalc_data.csv";
            std::ofstream f(path);
            f << "Iteration";
            for (auto& a : r.names) f << "," << a;
            f << "\n";
            for (int i = 0; i < r.n; ++i) {
                f << (i + 1);
                for (auto& d : r.data) f << "," << d[i];
                f << "\n";
            }
            note = "More than 1,000,000 iterations: the iteration data was written to " + path;
            writeBlock(sh, 0, 0, {{txt(note)}});
        } else {
            std::vector<Row> rows;
            rows.reserve(r.n + 1);
            Row h{txt("Iteration")};
            for (auto& a : r.names) h.push_back(txt(a));
            rows.push_back(h);
            for (int i = 0; i < r.n; ++i) {
                Row x{Any(double(i + 1))};
                for (auto& d : r.data) x.push_back(num(d[i]));
                rows.push_back(std::move(x));
            }
            writeBlock(sh, 0, 0, rows);
        }
        Reference<util::XProtectable>(sh, UNO_QUERY_THROW)->protect(OUString());
    }
    writeSummary(doc, r);
    writeStatistics(doc, r, r.st.bins);
    writeTornados(doc, r);
    if (sim.resultSetLabels().size() > 1) writeVariants(doc);
    Reference<frame::XModel> m(model);
    setDocProp(comp, "MonteCalc.Model", Any(m.is() ? m->getURL() : OUString()));
    auto labels = sim.resultSetLabels();
    if (labels.size() > 1) setDocProp(comp, "MonteCalc.Variant", Any(ou(labels[std::max(0, sim.activeResultSet())])));
    return note;
}

void rebuildStatistics(const Reference<uno::XComponentContext>&, const Reference<frame::XModel>& report, int bins) {
    Results r = collect();
    Reference<sheet::XSpreadsheetDocument> doc(report, UNO_QUERY_THROW);
    ensureSheet(doc, "Statistics", 1);
    writeStatistics(doc, r, bins);
}

void addSensitivitySheet(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XModel>& current, const std::string& xName,
                         const std::string& yName, bool hidden) {
    Results r = collect();
    // use the active result workbook, or create one
    Reference<sheet::XSpreadsheetDocument> doc(current, UNO_QUERY);
    bool isReport = doc.is() && doc->getSheets()->hasByName("Data") && doc->getSheets()->hasByName("Summary");
    if (!isReport) {
        Reference<lang::XComponent> comp;
        createReport(ctx, hidden, &comp, current);
        doc.set(comp, UNO_QUERY_THROW);
    }
    auto find = [&](const std::string& n) {
        for (size_t i = 0; i < r.names.size(); ++i)
            if (mc::lower(r.names[i]) == mc::lower(n)) return int(i);
        throw std::runtime_error("unknown variable " + n);
    };
    int xi = find(xName), yi = find(yName);
    std::string sname = ("Sensitivity " + r.names[xi] + "-" + r.names[yi]).substr(0, 31);
    for (char& c : sname) if (std::string("[]*?:/\\").find(c) != std::string::npos) c = '_';
    ensureSheet(doc, sname, 3);
    auto sh = sheetNamed(doc, sname);
    std::string xc = mc::formula::columnName(xi + 1), yc = mc::formula::columnName(yi + 1);
    std::string last = std::to_string(r.n + 1);
    std::string X = "$Data.$" + xc + "$2:$" + xc + "$" + last, Y = "$Data.$" + yc + "$2:$" + yc + "$" + last;
    // left: correlations of all variables with Y
    std::vector<Row> left{{txt("Correlation with " + r.names[yi]), txt("Spearman")}};
    for (size_t i = 0; i < r.names.size(); ++i)
        if (int(i) != yi) left.push_back({txt(r.names[i]), num(mc::stats::spearman(r.data[i], r.data[yi]))});
    writeBlock(sh, 0, 0, left);
    // middle: regression and forecast (I4 = X, I5 = estimate, K5/L5 = 95% forecast interval)
    auto setF = [&](const char* cell, const std::string& f) { sh->getCellRangeByName(OUString::createFromAscii(cell))->getCellByPosition(0, 0)->setFormula(ou(f)); };
    auto setT = [&](const char* cell, const std::string& t) {
        Reference<text::XText>(sh->getCellRangeByName(OUString::createFromAscii(cell))->getCellByPosition(0, 0), UNO_QUERY_THROW)->setString(ou(t));
    };
    setT("D1", "Linear regression: " + r.names[yi] + " = a + b * " + r.names[xi]);
    setT("D2", "Slope b"); setF("E2", "=SLOPE(" + Y + ";" + X + ")");
    setT("D3", "Intercept a"); setF("E3", "=INTERCEPT(" + Y + ";" + X + ")");
    setT("D4", "R squared"); setF("E4", "=RSQ(" + Y + ";" + X + ")");
    setT("D5", "Standard error"); setF("E5", "=STEYX(" + Y + ";" + X + ")");
    setT("D6", "Observations"); setF("E6", "=COUNT(" + X + ")");
    setT("H4", "New " + r.names[xi] + " (enter):"); setF("I4", "=AVERAGE(" + X + ")");
    setT("H5", "Estimated " + r.names[yi] + ":"); setF("I5", "=E3+E2*I4");
    setT("K4", "95% forecast lower"); setT("L4", "95% forecast upper");
    std::string se = "E5*SQRT(1+1/E6+(I4-AVERAGE(" + X + "))^2/DEVSQ(" + X + "))";
    setF("K5", "=I5-1.959964*" + se);
    setF("L5", "=I5+1.959964*" + se);
    // correlation of Y with X by 10% quantile steps of Y
    std::vector<Row> q{{txt("Quantile of " + r.names[yi]), txt("from"), txt("to"), txt("Correlation (Spearman)")}};
    for (int k = 0; k < 10; ++k) {
        double lo = mc::stats::percentile(r.data[yi], k / 10.0), hi = mc::stats::percentile(r.data[yi], (k + 1) / 10.0);
        Vec a, b;
        for (size_t i = 0; i < r.data[yi].size(); ++i)
            if (r.data[yi][i] >= lo && r.data[yi][i] <= hi) { a.push_back(r.data[xi][i]); b.push_back(r.data[yi][i]); }
        q.push_back({txt(std::to_string(k * 10) + "-" + std::to_string((k + 1) * 10) + "%"), num(lo), num(hi), num(mc::stats::spearman(a, b))});
    }
    writeBlock(sh, 3, 8, q);
    addChart(sh, "quantile_corr", awt::Rectangle(9000, 9000, 12000, 7000), seq<table::CellRangeAddress>({address(sh, 3, 8, 3, 18), address(sh, 6, 8, 6, 18)}),
             "Correlation by quantile of " + r.names[yi], nullptr);
    // further regression statistics (from row 52, as in MC FLO)
    setT("D52", "Further regression statistics (LINEST)");
    Reference<sheet::XArrayFormulaRange>(sh->getCellRangeByName("D53:E57"), UNO_QUERY_THROW)->setArrayFormula(ou("LINEST(" + Y + ";" + X + ";1;1)"));
    // right: tornado of Y
    if (yi < int(r.nOut)) {
        std::vector<std::pair<std::string, double>> d;
        for (size_t i = r.nOut; i < r.names.size(); ++i) d.push_back({r.names[i], mc::stats::spearman(r.data[i], r.data[yi])});
        std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) < std::fabs(b.second); });
        std::vector<Row> t{{txt("Input"), txt("Tornado")}};
        for (auto& [n, c] : d) t.push_back({txt(n), num(c)});
        writeBlock(sh, 13, 0, t);
        auto chart = addChart(sh, "tornado", awt::Rectangle(33000, 0, 12000, 7000),
                              seq<table::CellRangeAddress>({address(sh, 13, 0, 14, sal_Int32(d.size()))}), "Tornado: " + r.names[yi], nullptr);
        tornadoLayout(chart);
    }
    autofit(sh, 15);
    Reference<frame::XModel> rm(doc, UNO_QUERY);
    if (rm.is() && rm->getCurrentController().is()) {
        Reference<sheet::XSpreadsheetView> view(rm->getCurrentController(), UNO_QUERY);
        if (view.is()) view->setActiveSheet(sh);
    }
}

} // namespace montecalc
