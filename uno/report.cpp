// MonteCalc - result workbook
#include "report.hpp"
#include "util.hpp"

#include "../core/simulation.h"
#include "../core/stats.h"

#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/chart/XChartDocument.hpp>
#include <com/sun/star/chart/ChartAxisLabelPosition.hpp>
#include <com/sun/star/chart/XAxisXSupplier.hpp>
#include <com/sun/star/chart/XDiagram.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/document/XEmbeddedObjectSupplier.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
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
#include <com/sun/star/lang/Locale.hpp>

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

Any num(double v) { return std::isfinite(v) ? Any(v) : Any(OUString()); }
Any txt(const std::string& s) { return Any(ou(s)); }

void writeBlock(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 col, sal_Int32 row,
                const std::vector<Row>& rows) {
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

table::CellRangeAddress address(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 c0, sal_Int32 r0,
                                sal_Int32 c1, sal_Int32 r1) {
    auto range = sh->getCellRangeByPosition(c0, r0, c1, r1);
    return Reference<sheet::XCellRangeAddressable>(range, UNO_QUERY_THROW)->getRangeAddress();
}

Reference<chart::XChartDocument> addChart(const Reference<sheet::XSpreadsheet>& sh, const std::string& name,
                                          const awt::Rectangle& rect,
                                          const Sequence<table::CellRangeAddress>& ranges,
                                          const std::string& title, const char* diagramService) {
    Reference<table::XTableCharts> charts =
        Reference<table::XTableChartsSupplier>(sh, UNO_QUERY_THROW)->getCharts();
    OUString n = ou(name);
    if (charts->hasByName(n)) charts->removeByName(n);
    charts->addNewByName(n, rect, ranges, true, true);
    Reference<table::XTableChart> tc(charts->getByName(n), UNO_QUERY_THROW);
    Reference<chart::XChartDocument> doc(
        Reference<document::XEmbeddedObjectSupplier>(tc, UNO_QUERY_THROW)->getEmbeddedObject(), UNO_QUERY_THROW);
    if (diagramService) {
        Reference<lang::XMultiServiceFactory> f(doc, UNO_QUERY_THROW);
        doc->setDiagram(Reference<chart::XDiagram>(f->createInstance(OUString::createFromAscii(diagramService)),
                                                   UNO_QUERY_THROW));
    }
    Reference<beans::XPropertySet> dp(doc, UNO_QUERY_THROW);
    dp->setPropertyValue("HasMainTitle", Any(true));
    Reference<beans::XPropertySet>(doc->getTitle(), UNO_QUERY_THROW)->setPropertyValue("String", Any(ou(title)));
    dp->setPropertyValue("HasLegend", Any(false));
    return doc;
}

// Readable axis label: thousands separators for large values, 3 significant digits otherwise.
std::string label(double x) {
    char b[64];
    if (std::fabs(x) >= 1000) {
        std::snprintf(b, sizeof b, "%.0f", x);
        std::string s = b, out;
        size_t start = (s[0] == '-') ? 1 : 0;
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
    std::snprintf(b, sizeof b, "P%g%%", p * 100);
    return b;
}

void autofit(const Reference<sheet::XSpreadsheet>& sh, sal_Int32 ncols) {
    Reference<table::XColumnRowRange> crr(sh, UNO_QUERY_THROW);
    auto cols = crr->getColumns();
    for (sal_Int32 c = 0; c < ncols && c < cols->getCount(); ++c)
        Reference<beans::XPropertySet>(cols->getByIndex(c), UNO_QUERY_THROW)
            ->setPropertyValue("OptimalWidth", Any(true));
}

} // namespace

std::string createReport(const Reference<uno::XComponentContext>& ctx, bool hidden,
                         Reference<lang::XComponent>* created) {
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) throw std::runtime_error("No simulation results available. Run a simulation first.");
    mc::Settings st = sim.settings();
    std::vector<mc::Variable> vars;
    for (const auto& v : sim.variables()) vars.push_back(v);  // descriptions
    auto inputs = sim.resultNames(mc::VarKind::Input);
    auto outputs = sim.resultNames(mc::VarKind::Output);
    std::vector<std::string> all = outputs;
    all.insert(all.end(), inputs.begin(), inputs.end());
    std::vector<std::vector<double>> data(all.size());
    for (size_t i = 0; i < all.size(); ++i) sim.samples(all[i], data[i]);
    const int n = sim.iterations();
    auto descOf = [&](const std::string& name) {
        for (const auto& v : vars)
            if (mc::lower(v.name) == mc::lower(name)) return v.description;
        return std::string();
    };

    Reference<frame::XComponentLoader> loader(frame::Desktop::create(ctx), UNO_QUERY_THROW);
    Sequence<beans::PropertyValue> props(1);
    props.getArray()[0].Name = "Hidden";
    props.getArray()[0].Value <<= hidden;
    Reference<lang::XComponent> comp = loader->loadComponentFromURL("private:factory/scalc", "_blank", 0, props);
    if (created) *created = comp;
    Reference<sheet::XSpreadsheetDocument> doc(comp, UNO_QUERY_THROW);
    Reference<sheet::XSpreadsheets> sheets = doc->getSheets();
    Reference<container::XIndexAccess> ia(sheets, UNO_QUERY_THROW);
    Reference<container::XNamed>(ia->getByIndex(0), UNO_QUERY_THROW)->setName("Summary");
    sheets->insertNewByName("Charts", 1);
    sheets->insertNewByName("Sensitivity", 2);
    sheets->insertNewByName("Data", 3);
    auto sheet = [&](const char* name) {
        return Reference<sheet::XSpreadsheet>(sheets->getByName(OUString::createFromAscii(name)), UNO_QUERY_THROW);
    };
    std::string note;

    // ---------------- Data ----------------
    {
        auto sh = sheet("Data");
        const int maxRows = 1000000;
        if (n > maxRows) {
            // Too many rows for a sheet: export to CSV instead.
            std::string path = std::string(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") + "/MonteCalc_data.csv";
            std::ofstream f(path);
            f << "Iteration";
            for (auto& a : all) f << "," << a;
            f << "\n";
            for (int i = 0; i < n; ++i) {
                f << (i + 1);
                for (auto& d : data) f << "," << d[i];
                f << "\n";
            }
            note = "More than 1,000,000 iterations: the iteration data was written to " + path;
            writeBlock(sh, 0, 0, {{txt(note)}});
        } else {
            std::vector<Row> rows;
            rows.reserve(n + 1);
            Row h{txt("Iteration")};
            for (auto& a : all) h.push_back(txt(a));
            rows.push_back(h);
            for (int i = 0; i < n; ++i) {
                Row r{Any(double(i + 1))};
                for (auto& d : data) r.push_back(num(d[i]));
                rows.push_back(std::move(r));
            }
            writeBlock(sh, 0, 0, rows);
        }
        Reference<util::XProtectable>(sh, UNO_QUERY_THROW)->protect(OUString());
    }

    // ---------------- Summary ----------------
    {
        auto sh = sheet("Summary");
        std::vector<Row> rows;
        Row h{txt("Statistic")};
        for (auto& a : all) h.push_back(txt(a));
        rows.push_back(h);
        auto line = [&](const std::string& label, auto fn) {
            Row r{txt(label)};
            for (size_t i = 0; i < all.size(); ++i) r.push_back(fn(i));
            rows.push_back(std::move(r));
        };
        line("Kind", [&](size_t i) { return txt(i < outputs.size() ? "Output" : "Input"); });
        line("Definition", [&](size_t i) { return txt(descOf(all[i])); });
        line("Iterations", [&](size_t i) { return num(mc::stats::count(data[i])); });
        line("Mean", [&](size_t i) { return num(mc::stats::mean(data[i])); });
        line("Standard deviation", [&](size_t i) { return num(mc::stats::stdev(data[i])); });
        line("Variance", [&](size_t i) { return num(mc::stats::variance(data[i])); });
        line("Minimum", [&](size_t i) { return num(mc::stats::min(data[i])); });
        line("Maximum", [&](size_t i) { return num(mc::stats::max(data[i])); });
        line("Range", [&](size_t i) { return num(mc::stats::max(data[i]) - mc::stats::min(data[i])); });
        line("Skewness", [&](size_t i) { return num(mc::stats::skewness(data[i])); });
        line("Kurtosis (excess)", [&](size_t i) { return num(mc::stats::kurtosis(data[i])); });
        line("Coefficient of variation", [&](size_t i) { return num(mc::stats::cv(data[i])); });
        line("Mean, 90% CI lower", [&](size_t i) {
            return num(mc::stats::mean(data[i]) - 1.6448536 * mc::stats::stdev(data[i]) / std::sqrt(mc::stats::count(data[i])));
        });
        line("Mean, 90% CI upper", [&](size_t i) {
            return num(mc::stats::mean(data[i]) + 1.6448536 * mc::stats::stdev(data[i]) / std::sqrt(mc::stats::count(data[i])));
        });
        line("Worst case (" + pct(st.worst) + ")", [&](size_t i) { return num(mc::stats::percentile(data[i], st.worst)); });
        line("Real case (" + pct(st.real) + ")", [&](size_t i) { return num(mc::stats::percentile(data[i], st.real)); });
        line("Best case (" + pct(st.best) + ")", [&](size_t i) { return num(mc::stats::percentile(data[i], st.best)); });
        std::vector<double> ps = {0.01, 0.02, 0.03, 0.04};
        for (int k = 5; k <= 95; k += 5) ps.push_back(k / 100.0);
        for (double p : {0.96, 0.97, 0.98, 0.99}) ps.push_back(p);
        for (double p : ps)
            line(pct(p), [&, p](size_t i) { return num(mc::stats::percentile(data[i], p)); });
        writeBlock(sh, 0, 0, rows);
        // number format per variable, chosen from the magnitude of its values
        Reference<util::XNumberFormats> nf =
            Reference<util::XNumberFormatsSupplier>(doc, UNO_QUERY_THROW)->getNumberFormats();
        auto key = [&](const char* code) {
            lang::Locale loc;
            sal_Int32 k = nf->queryKey(OUString::createFromAscii(code), loc, false);
            return k != -1 ? k : nf->addNew(OUString::createFromAscii(code), loc);
        };
        const sal_Int32 firstStat = 4, lastStat = sal_Int32(rows.size()) - 1;  // rows "Mean".. last percentile
        for (size_t i = 0; i < all.size(); ++i) {
            double mag = std::max(std::fabs(mc::stats::min(data[i])), std::fabs(mc::stats::max(data[i])));
            const char* code = mag >= 1000 ? "#,##0" : mag >= 10 ? "#,##0.00" : "0.0000";
            auto col = sh->getCellRangeByPosition(sal_Int32(i) + 1, firstStat, sal_Int32(i) + 1, lastStat);
            Reference<beans::XPropertySet>(col, UNO_QUERY_THROW)->setPropertyValue("NumberFormat", Any(key(code)));
        }
        // shape statistics are dimensionless
        auto shape = sh->getCellRangeByPosition(1, 10, sal_Int32(all.size()), 12);
        Reference<beans::XPropertySet>(shape, UNO_QUERY_THROW)->setPropertyValue("NumberFormat", Any(key("0.0000")));
        writeBlock(sh, 0, sal_Int32(rows.size()) + 1,
                   {{txt("Seed"), Any(double(st.seed))},
                    {txt("Generated by"), txt("MonteCalc")}});
        autofit(sh, sal_Int32(all.size()) + 1);
    }

    // ---------------- Charts: histogram + cumulative per variable ----------------
    {
        auto sh = sheet("Charts");
        sal_Int32 row = 0;
        const sal_Int32 bins = std::max(2, st.bins);
        const sal_Int32 blockH = std::max<sal_Int32>(bins + 3, 22);
        for (size_t i = 0; i < all.size(); ++i) {
            auto hst = mc::stats::histogram(data[i], bins);
            if (hst.count.empty()) continue;
            double total = 0, cum = 0;
            for (double c : hst.count) total += c;
            std::vector<Row> rows;
            rows.push_back({txt(all[i]), txt("Percent"), txt("Cumulative"), txt("Count")});
            for (size_t b = 0; b < hst.count.size(); ++b) {
                cum += hst.count[b];
                rows.push_back({txt(label((hst.lo[b] + hst.hi[b]) / 2)), Any(hst.count[b] / total), Any(cum / total), Any(hst.count[b])});
            }
            writeBlock(sh, 0, row, rows);
            sal_Int32 last = row + sal_Int32(hst.count.size());
            // 1/100 mm; one row ~ 0.45 cm
            sal_Int32 top = row * 452;
            addChart(sh, "hist_" + std::to_string(i), awt::Rectangle(9000, top, 11000, 8500),
                     seq<table::CellRangeAddress>({address(sh, 0, row, 1, last)}), all[i] + " - histogram", nullptr);
            addChart(sh, "cdf_" + std::to_string(i), awt::Rectangle(20500, top, 9500, 8500),
                     seq<table::CellRangeAddress>({address(sh, 0, row, 0, last), address(sh, 2, row, 2, last)}),
                     all[i] + " - cumulative", "com.sun.star.chart.LineDiagram");
            row += blockH;
        }
        autofit(sh, 4);
    }

    // ---------------- Sensitivity: tornado per output ----------------
    {
        auto sh = sheet("Sensitivity");
        sal_Int32 row = 0;
        for (size_t o = 0; o < outputs.size(); ++o) {
            std::vector<std::pair<std::string, double>> d;
            for (size_t i = outputs.size(); i < all.size(); ++i) {
                double r = mc::stats::spearman(data[i], data[o]);
                d.push_back({all[i], std::isfinite(r) ? r : 0.0});
            }
            if (d.empty()) break;
            std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) < std::fabs(b.second); });
            std::vector<Row> rows{{txt("Input"), txt("Rank correlation with " + outputs[o])}};
            for (auto& [nm, r] : d) rows.push_back({txt(nm), Any(r)});
            writeBlock(sh, 0, row, rows);
            sal_Int32 last = row + sal_Int32(d.size());
            sal_Int32 h = std::max<sal_Int32>(6000, 600 * sal_Int32(d.size()) + 2500);
            auto chart = addChart(sh, "tornado_" + std::to_string(o), awt::Rectangle(9000, row * 452, 14000, h),
                                  seq<table::CellRangeAddress>({address(sh, 0, row, 1, last)}), "Tornado: " + outputs[o], nullptr);
            Reference<beans::XPropertySet>(chart->getDiagram(), UNO_QUERY_THROW)->setPropertyValue("Vertical", Any(true));
            // keep the input names left of the plot instead of on the zero line
            Reference<chart::XAxisXSupplier> xs(chart->getDiagram(), UNO_QUERY);
            if (xs.is() && xs->getXAxis().is())
                xs->getXAxis()->setPropertyValue("LabelPosition", Any(chart::ChartAxisLabelPosition_OUTSIDE_START));
            row = std::max(last + 3, row + h / 452 + 2);
        }
        autofit(sh, 2);
    }
    return note;
}

} // namespace montecalc
