// MonteCalc - result workbook (Summary, Statistics, Sensitivity, Variants, Data).
// Written through ModelDoc, so it is the same in LibreOffice and in Excel.
#include "report.hpp"
#include "model.hpp"
#include "util.hpp"

#include "../core/simulation.h"
#include "../core/stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
namespace table = com::sun::star::table;

namespace montecalc {

namespace {

using Row = std::vector<Any>;
using Vec = std::vector<double>;

Any num(double v) { return std::isfinite(v) ? Any(v) : Any(OUString()); }
Any txt(const std::string& s) { return Any(ou(s)); }

table::CellRangeAddress area(int sheet, int c0, int r0, int c1, int r1) {
    return table::CellRangeAddress(sal_Int16(sheet), c0, r0, c1, r1);
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

void writeSummary(ModelDoc& doc, const Results& r) {
    int sh = doc.sheetIndex("Summary");
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
    const int firstStat = int(rows.size());
    line("Iterations", [&](size_t i) { return num(mc::stats::count(r.data[i])); });
    line("Mean", [&](size_t i) { return num(mc::stats::mean(r.data[i])); });
    line("Minimum", [&](size_t i) { return num(mc::stats::min(r.data[i])); });
    line("Maximum", [&](size_t i) { return num(mc::stats::max(r.data[i])); });
    line("Variance", [&](size_t i) { return num(mc::stats::variance(r.data[i])); });
    line("Standard deviation", [&](size_t i) { return num(mc::stats::stdev(r.data[i])); });
    line("Worst case (" + pct(r.st.worst) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.worst)); });
    line("Real case (" + pct(r.st.real) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.real)); });
    line("Best case (" + pct(r.st.best) + ")", [&](size_t i) { return num(mc::stats::percentile(r.data[i], r.st.best)); });
    const int shapeRow = int(rows.size());
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
    doc.writeTable(sh, 0, 0, rows);
    for (size_t i = 0; i < r.names.size(); ++i) doc.numberFormat(sh, int(i) + 1, firstStat + 1, int(i) + 1, int(rows.size()) - 1, formatFor(r.data[i]));
    doc.numberFormat(sh, 1, shapeRow, int(r.names.size()), shapeRow + 2, "0.0000");
    doc.writeTable(sh, int(rows.size()) + 1, 0, {{txt("Seed"), Any(double(r.st.seed))}, {txt("Generated by"), txt("MonteCalc")}});
    doc.optimalWidth(sh, 0, int(r.names.size()) + 1);
}

void writeStatistics(ModelDoc& doc, const Results& r, int bins) {
    int sh = doc.sheetIndex("Statistics");
    int row = 0;
    bins = std::max(2, bins);
    const int blockH = std::max(bins + 3, 24);
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
        doc.writeTable(sh, row, 0, rows);
        // statistics next to the table
        double m = mc::stats::mean(v), s = mc::stats::stdev(v), n = mc::stats::count(v);
        double z90 = 1.6448536;
        doc.writeTable(sh, row, 5, {{txt("Statistic"), txt(r.names[i])},
                                    {txt("Mean"), num(m)},
                                    {txt("Mean, 90% confidence lower"), num(m - z90 * s / std::sqrt(n))},
                                    {txt("Mean, 90% confidence upper"), num(m + z90 * s / std::sqrt(n))},
                                    {txt("Standard deviation"), num(s)},
                                    {txt("Worst case (" + pct(r.st.worst) + ")"), num(worst)},
                                    {txt("Real case (" + pct(r.st.real) + ")"), num(mc::stats::percentile(v, r.st.real))},
                                    {txt("Best case (" + pct(r.st.best) + ")"), num(best)},
                                    {txt("P(value < 0) (limit value)"), num(double(std::count_if(v.begin(), v.end(), [](double x) { return x < 0; })) / n)}});
        int last = row + int(hst.count.size());
        int top = row * 452;
        doc.addChart(sh, "hist_" + std::to_string(i), ModelDoc::ChartKind::Column, {area(sh, 0, row, 1, last)}, 17500, top, 11000, 9000,
                     r.names[i] + " - histogram");
        doc.addChart(sh, "cdf_" + std::to_string(i), ModelDoc::ChartKind::Line, {area(sh, 0, row, 0, last), area(sh, 2, row, 2, last)}, 29000, top, 9500,
                     9000, r.names[i] + " - cumulative");
        row += blockH;
    }
    doc.optimalWidth(sh, 0, 7);
}

void writeTornados(ModelDoc& doc, const Results& r) {
    int sh = doc.sheetIndex("Sensitivity");
    int row = 0;
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
        doc.writeTable(sh, row, 0, rows);
        int last = row + int(d.size());
        int h = std::max(6000, 600 * int(d.size()) + 2500);
        doc.addChart(sh, "tornado_" + std::to_string(o), ModelDoc::ChartKind::Tornado, {area(sh, 0, row, 1, last)}, 9000, row * 452, 14000, h,
                     "Tornado: " + r.names[o]);
        row = std::max(last + 3, row + h / 452 + 2);
    }
    doc.optimalWidth(sh, 0, 2);
}

void writeVariants(ModelDoc& doc) {
    auto& sim = mc::Simulation::instance();
    auto labels = sim.resultSetLabels();
    int sh = doc.sheetIndex("Variants");
    auto outs = sim.resultNames(mc::VarKind::Output);
    mc::Settings st = sim.settings();
    int row = 0;
    for (const auto& o : outs) {
        std::vector<Row> rows{{txt(o), txt("Mean"), txt("Worst (" + pct(st.worst) + ")"), txt("Real (" + pct(st.real) + ")"),
                               txt("Best (" + pct(st.best) + ")"), txt("Std dev")}};
        for (size_t v = 0; v < labels.size(); ++v) {
            Vec d;
            sim.samplesIn(int(v), o, d);
            rows.push_back({txt(labels[v]), num(mc::stats::mean(d)), num(mc::stats::percentile(d, st.worst)), num(mc::stats::percentile(d, st.real)),
                            num(mc::stats::percentile(d, st.best)), num(mc::stats::stdev(d))});
        }
        doc.writeTable(sh, row, 0, rows);
        int last = row + int(labels.size());
        doc.addChart(sh, "variants_" + o, ModelDoc::ChartKind::Column, {area(sh, 0, row, 4, last)}, 15000, row * 452, 12000, 7000, o + " by variant",
                     true);
        row = std::max(last + 3, row + 17);
    }
    doc.optimalWidth(sh, 0, 6);
}

} // namespace

std::string createReport(const Reference<com::sun::star::uno::XComponentContext>& ctx, bool hidden, ModelDoc* created,
                         const Reference<com::sun::star::frame::XModel>& model) {
    Results r = collect();
    auto& sim = mc::Simulation::instance();
    std::string modelUrl = ModelDoc(model).url();
    ModelDoc doc = ModelDoc::newDocument(ctx, hidden);
    if (created) *created = doc;
    doc.renameSheet(0, "Summary");
    doc.insertSheet("Statistics", 1);
    doc.insertSheet("Sensitivity", 2);
    int next = 3;
    if (sim.resultSetLabels().size() > 1) doc.insertSheet("Variants", next++);
    int data = doc.insertSheet("Data", next);
    std::string note;
    // ---------------- Data ----------------
    const int maxRows = 1000000;
    if (r.n > maxRows) {
        std::string path = homeDir() + "/MonteCalc_data.csv";
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
        doc.writeTable(data, 0, 0, {{txt(note)}});
    } else {
        std::vector<Row> rows;
        rows.reserve(size_t(r.n) + 1);
        Row h{txt("Iteration")};
        for (auto& a : r.names) h.push_back(txt(a));
        rows.push_back(h);
        for (int i = 0; i < r.n; ++i) {
            Row x{Any(double(i + 1))};
            for (auto& d : r.data) x.push_back(num(d[i]));
            rows.push_back(std::move(x));
        }
        doc.writeTable(data, 0, 0, rows);
    }
    doc.protectSheet(data);
    writeSummary(doc, r);
    writeStatistics(doc, r, r.st.bins);
    writeTornados(doc, r);
    if (sim.resultSetLabels().size() > 1) writeVariants(doc);
    doc.setProperty("MonteCalc.Model", Any(ou(modelUrl)));
    auto labels = sim.resultSetLabels();
    if (labels.size() > 1) doc.setProperty("MonteCalc.Variant", Any(ou(labels[size_t(std::max(0, sim.activeResultSet()))])));
    doc.activateSheet(0);
    return note;
}

void rebuildStatistics(const Reference<com::sun::star::uno::XComponentContext>&, const Reference<com::sun::star::frame::XModel>& report, int bins) {
    Results r = collect();
    ModelDoc doc(report);
    doc.insertSheet("Statistics", 1);
    writeStatistics(doc, r, bins);
}

void addSensitivitySheet(const Reference<com::sun::star::uno::XComponentContext>& ctx, const Reference<com::sun::star::frame::XModel>& current,
                         const std::string& xName, const std::string& yName, bool hidden) {
    Results r = collect();
    // use the active result workbook, or create one
    ModelDoc doc(current);
    bool isReport = doc.valid() && doc.sheetIndex("Data") >= 0 && doc.sheetIndex("Summary") >= 0;
    if (!isReport) createReport(ctx, hidden, &doc, current);
    auto find = [&](const std::string& n) {
        for (size_t i = 0; i < r.names.size(); ++i)
            if (mc::lower(r.names[i]) == mc::lower(n)) return int(i);
        throw std::runtime_error("unknown variable " + n);
    };
    int xi = find(xName), yi = find(yName);
    std::string sname = ("Sensitivity " + r.names[size_t(xi)] + "-" + r.names[size_t(yi)]).substr(0, 31);
    for (char& c : sname) if (std::string("[]*?:/\\").find(c) != std::string::npos) c = '_';
    int sh = doc.insertSheet(sname, 3);
    std::string xc = mc::formula::columnName(xi + 1), yc = mc::formula::columnName(yi + 1);
    std::string last = std::to_string(r.n + 1);
    std::string X = "$Data.$" + xc + "$2:$" + xc + "$" + last, Y = "$Data.$" + yc + "$2:$" + yc + "$" + last;
    // left: correlations of all variables with Y
    std::vector<Row> left{{txt("Correlation with " + r.names[size_t(yi)]), txt("Spearman")}};
    for (size_t i = 0; i < r.names.size(); ++i)
        if (int(i) != yi) left.push_back({txt(r.names[i]), num(mc::stats::spearman(r.data[i], r.data[size_t(yi)]))});
    doc.writeTable(sh, 0, 0, left);
    // middle: regression and forecast (I4 = X, I5 = estimate, K5/L5 = 95% forecast interval)
    auto at = [&](const char* cell) {
        int col, row;
        mc::formula::parseCellName(cell, col, row);
        return doc.cell(table::CellAddress(sal_Int16(sh), col, row));
    };
    auto setF = [&](const char* cell, const std::string& f) { at(cell).setFormula(f); };
    auto setT = [&](const char* cell, const std::string& t) { at(cell).setText(t); };
    setT("D1", "Linear regression: " + r.names[size_t(yi)] + " = a + b * " + r.names[size_t(xi)]);
    setT("D2", "Slope b"); setF("E2", "=SLOPE(" + Y + ";" + X + ")");
    setT("D3", "Intercept a"); setF("E3", "=INTERCEPT(" + Y + ";" + X + ")");
    setT("D4", "R squared"); setF("E4", "=RSQ(" + Y + ";" + X + ")");
    setT("D5", "Standard error"); setF("E5", "=STEYX(" + Y + ";" + X + ")");
    setT("D6", "Observations"); setF("E6", "=COUNT(" + X + ")");
    setT("H4", "New " + r.names[size_t(xi)] + " (enter):"); setF("I4", "=AVERAGE(" + X + ")");
    setT("H5", "Estimated " + r.names[size_t(yi)] + ":"); setF("I5", "=E3+E2*I4");
    setT("K4", "95% forecast lower"); setT("L4", "95% forecast upper");
    std::string se = "E5*SQRT(1+1/E6+(I4-AVERAGE(" + X + "))^2/DEVSQ(" + X + "))";
    setF("K5", "=I5-1.959964*" + se);
    setF("L5", "=I5+1.959964*" + se);
    // correlation of Y with X by 10% quantile steps of Y
    std::vector<Row> q{{txt("Quantile of " + r.names[size_t(yi)]), txt("from"), txt("to"), txt("Correlation (Spearman)")}};
    for (int k = 0; k < 10; ++k) {
        double lo = mc::stats::percentile(r.data[size_t(yi)], k / 10.0), hi = mc::stats::percentile(r.data[size_t(yi)], (k + 1) / 10.0);
        Vec a, b;
        for (size_t i = 0; i < r.data[size_t(yi)].size(); ++i)
            if (r.data[size_t(yi)][i] >= lo && r.data[size_t(yi)][i] <= hi) { a.push_back(r.data[size_t(xi)][i]); b.push_back(r.data[size_t(yi)][i]); }
        q.push_back({txt(std::to_string(k * 10) + "-" + std::to_string((k + 1) * 10) + "%"), num(lo), num(hi), num(mc::stats::spearman(a, b))});
    }
    doc.writeTable(sh, 8, 3, q);
    doc.addChart(sh, "quantile_corr", ModelDoc::ChartKind::Column, {area(sh, 3, 8, 3, 18), area(sh, 6, 8, 6, 18)}, 9000, 9000, 12000, 7000,
                 "Correlation by quantile of " + r.names[size_t(yi)]);
    // further regression statistics (from row 52, as in MC FLO)
    setT("D52", "Further regression statistics (LINEST)");
    doc.setArrayFormula(sh, 3, 52, 4, 56, "LINEST(" + Y + ";" + X + ";1;1)");
    // right: tornado of Y
    if (yi < int(r.nOut)) {
        std::vector<std::pair<std::string, double>> d;
        for (size_t i = r.nOut; i < r.names.size(); ++i) d.push_back({r.names[i], mc::stats::spearman(r.data[i], r.data[size_t(yi)])});
        std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) < std::fabs(b.second); });
        std::vector<Row> t{{txt("Input"), txt("Tornado")}};
        for (auto& [n, c] : d) t.push_back({txt(n), num(c)});
        doc.writeTable(sh, 0, 13, t);
        doc.addChart(sh, "tornado", ModelDoc::ChartKind::Tornado, {area(sh, 13, 0, 14, int(d.size()))}, 33000, 0, 12000, 7000,
                     "Tornado: " + r.names[size_t(yi)]);
    }
    doc.optimalWidth(sh, 0, 15);
    doc.activateSheet(sh);
}

} // namespace montecalc
