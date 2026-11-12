// MonteCalc - interactive windows: result window, Choose Wizard, fitting, correlogram,
// variable list, influence diagram (with results tab) and the FLO_Context sheet.
#include "windows.hpp"
#include "canvaswin.hpp"
#include "charts.hpp"
#include "deps.hpp"
#include "engine.hpp"
#include "model.hpp"
#include "report.hpp"
#include "ui.hpp"
#include "util.hpp"

#include "../core/copula.h"
#include "../core/fitting.h"
#include "../core/flofuncs.h"
#include "../core/formula.h"
#include "../core/stats.h"

#ifndef MONTECALC_XLL
#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/table/XColumnRowRange.hpp>
#include <com/sun/star/text/XText.hpp>
#else
#include "../xll/uno_shim.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
#ifndef MONTECALC_XLL
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;
#endif

namespace montecalc {

namespace {

std::string pctText(double p) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%%", p * 100);
    return b;
}

std::string quote(const std::string& s) {
    std::string r = "\"";
    for (char c : s) r += c == '"' ? std::string("\"\"") : std::string(1, c);
    return r + "\"";
}

double parseOr(const std::string& s, double def, bool* ok = nullptr) {
    bool k;
    double v = parseNumber(s, k);
    if (ok) *ok = k;
    return k ? v : def;
}

ModelDoc docOf(const Frame& frame) { return ModelDoc(frame->getController()->getModel()); }

// Samples of a cell's formula with random inputs (preview mode, manual: seed -1).
// Uses the in-memory evaluator; falls back to recalculating the spreadsheet.
Vec previewSamples(const ModelDoc& d, const table::CellAddress& a, int n) {
    auto& sim = mc::Simulation::instance();
    if (sim.busy()) throw std::runtime_error("a simulation is running");
    mc::Mode prev = sim.mode();
    Vec out;
    auto src = d.formulaSource();
    mc::formula::Model m;
    std::string why;
    sim.setMode(mc::Mode::Random);
    try {
        if (m.build(*src, {{a.Sheet, a.Column, a.Row}}, why)) {
            for (int i = 0; i < n; ++i) {
                m.beginIteration();
                mc::Value v = m.value({a.Sheet, a.Column, a.Row});
                if (v.kind == mc::Value::Number) out.push_back(v.num);
            }
        } else {
            auto c = d.cell(a);
            for (int i = 0; i < std::min(n, 2000); ++i) {
                d.recalc();
                if (c.error() == 0) out.push_back(c.value());
            }
        }
    } catch (...) {
        sim.setMode(prev);
        d.recalc();
        throw;
    }
    sim.setMode(prev);
    d.recalc();
    return out;
}

bool hasResults(const std::string& name) {
    Vec v;
    return mc::Simulation::instance().samples(name, v);
}

void insertIntoCurrentCell(const Frame& frame, const std::string& formula, const std::string& name, mc::VarKind kind) {
    ModelDoc d = docOf(frame);
    table::CellAddress a;
    if (!d.currentCell(a)) throw std::runtime_error("select a cell in the model first");
    d.cell(a).setFormula(ou(formula));
    if (!name.empty()) {
        d.setNamedRange((kind == mc::VarKind::Input ? "FLO_i_" : kind == mc::VarKind::Output ? "FLO_o_" : "FLO_c_") + mc::formula::sanitizeName(name), a);
    }
    d.setBackground(a, kind == mc::VarKind::Input ? kColorInput : kind == mc::VarKind::Output ? kColorOutput : kColorCorrelation);
}

} // namespace

// ============================================================ result window

class ResultWindow : public CanvasWindow {
public:
    enum View { Histogram, Cumulative, Tornado, Swing, DynSens, Spider, SpiderCum, Scatter, Variants, BoxWhisker, Decision, Statistics, Overview };
    std::vector<std::pair<View, std::string>> viewList() const {
        if (preview_ && pair_) return {{Scatter, "Scatter / regression"}, {Histogram, "Histogram"}, {Cumulative, "Cumulative"}, {Statistics, "Statistics"}};
        if (preview_) return {{Histogram, "Histogram"}, {Cumulative, "Cumulative"}, {Statistics, "Statistics"}};
        return {{Histogram, "Histogram"}, {Cumulative, "Cumulative"}, {Swing, "Tornado chart"}, {Tornado, "Correlation (Spearman)"}, {DynSens, "Dynamic sensitivity"},
                {Spider, "Spider diagram"}, {SpiderCum, "Spider (cumulative)"}, {Scatter, "Scatter / regression"}, {Variants, "Variants"},
                {BoxWhisker, "Box-Whisker"}, {Decision, "Decision matrix"}, {Statistics, "Statistics"}, {Overview, "Overview (selected variables)"}};
    }

    // previewName/previewData: a preview (no simulation results); with previewNameB/previewDataB a pair
    // (the two variables of a correlation): previewName on the x axis, previewNameB on the y axis
    ResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& previewName = "", Vec previewData = {},
                 const std::string& previewNameB = "", Vec previewDataB = {})
        : CanvasWindow(ctx, frame, previewName.empty() ? "MonteCalc - Results" : "MonteCalc - Preview " + previewName, 452, 270, 132),
          preview_(!previewName.empty()) {
        auto& sim = mc::Simulation::instance();
        if (preview_) {
            mc::Variable v;
            v.name = previewName;
            v.kind = mc::VarKind::Input;
            vars_.push_back(v);
            previewData_ = std::move(previewData);
            if (!previewNameB.empty()) {
                pair_ = true;
                corrSelected_ = true;
                v.name = previewNameB;
                vars_.push_back(v);
                previewDataB_ = std::move(previewDataB);
            }
        } else {
            vars_ = sim.resultVariables();
            // outputs first (as in MC FLO's output window)
            std::stable_partition(vars_.begin(), vars_.end(), [](const mc::Variable& v) { return v.kind == mc::VarKind::Output; });
        }
        if (vars_.empty()) throw std::runtime_error("no simulation results available - run a simulation (Start) first");
        cur_ = 0;
        for (size_t i = 0; i < vars_.size(); ++i)
            if (mc::lower(vars_[i].name) == mc::lower(variable)) cur_ = int(i);
        bins_ = sim.settings().bins;
        int x = panelX(), w = panelW(), y = 6;
        std::vector<std::string> items;
        for (auto& v : vars_) items.push_back((v.kind == mc::VarKind::Output ? "[out] " : "[in] ") + v.name);
        // correlations of the simulation: choosing one shows the scatter of its two variables
        if (!preview_)
            for (auto& c : sim.resultCorrelations())
                if (index(c.a) >= 0 && index(c.b) >= 0) {
                    corrs_.push_back(c);
                    items.push_back("[corr] " + c.a + " ~ " + c.b + " (" + niceNum(c.rho) + ")");
                }
        addLabel("lvar", "Variable", x, y, w); y += 10;
        addList("var", items, x, y, w - 30, 12, true);
        addButton("prev", "<", x + w - 28, y, 13, 12);
        addButton("next", ">", x + w - 14, y, 13, 12); y += 16;
        auto labels = sim.resultSetLabels();
        if (!preview_ && labels.size() > 1) {   // as in MC FLO: the variant (scenario) choice at the top right
            addLabel("lvariant", "Variant", x, y, w); y += 10;
            addList("variant", labels, x, y, w, 12, true); y += 16;
        }
        addLabel("lview", "View", x, y, w); y += 10;
        std::vector<std::string> views;
        for (auto& [v, label] : viewList()) views.push_back(label);
        addList("view", views, x, y, w, 12, true); y += 16;
        addLabel("lbins", "Number of bins", x, y + 2, 60);
        addEdit("bins", std::to_string(bins_), x + 62, y, 26);
        addButton("applyBins", "Set", x + 90, y, w - 90, 12); y += 16;
        addLabel("lcert", "Certainty level %", x, y + 2, 62);
        addEdit("certainty", "95", x + 64, y, w - 90);
        addButton("setCertainty", "Set", x + w - 24, y, 24, 12); y += 14;
        addLabel("lthr", "Lower / upper limit", x, y, w); y += 10;
        addEdit("lower", "", x, y, w / 2 - 14);
        addEdit("upper", "", x + w / 2 - 12, y, w / 2 - 14);
        addButton("thresholds", "Set", x + w - 24, y, 24, 12); y += 16;
        addLabel("lstripe", "Magic stripe (x, or ;p for a percentile)", x, y, w); y += 10;
        addEdit("stripe", "", x, y, w - 26);
        addButton("setStripe", "Set", x + w - 24, y, 24, 12); y += 16;
        addLabel("ltarget", "Target value (decision matrix)", x, y, w); y += 10;
        addList("targetDir", {">", "<"}, x, y, 22, 12, true);
        addEdit("target", "", x + 24, y, w - 50);
        addButton("applyTarget", "Set", x + w - 24, y, 24, 12); y += 16;
        addButton("store", "Store results", x, y, w / 2 - 1); addButton("export", "Export image", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("comment", "Store as comment", x, y, w / 2 - 1); addButton("rerun", "Start", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("zoomOut", "Zoom -", x, y, w / 3 - 1); addButton("zoomIn", "Zoom +", x + w / 3, y, w / 3 - 1);
        addButton("close", "Close", x + 2 * w / 3, y, w - 2 * w / 3); y += 20;
        // only shown while a correlation is selected (its two variables); for inputs and outputs the
        // scatter takes the strongest driver, or the bar clicked in the tornado / correlation chart
        std::vector<std::string> inputs = itemsOf("input");
        addLabel("linput", "X-variable (scatter)", x, y, w); y += 10;
        addList("input", inputs.empty() ? std::vector<std::string>{"-"} : inputs, x, y, w, 12, true);
        resetVariable();
    }

    void afterShow() {
        select("var", cur_);
        updateXVisibility();
        if (has("variant")) select("variant", std::max(0, mc::Simulation::instance().activeResultSet()));
    }

    Vec data(const std::string& name) {
        if (preview_) return pair_ && mc::lower(name) == mc::lower(vars_[1].name) ? previewDataB_ : previewData_;
        Vec v;
        mc::Simulation::instance().samples(name, v);
        return v;
    }

    void resetVariable() {
        Vec v = data(vars_[cur_].name);
        stripeX_ = mc::stats::percentile(v, 0.5);
        lower_ = mc::stats::percentile(v, (1 - certainty_) / 2);
        upper_ = mc::stats::percentile(v, 1 - (1 - certainty_) / 2);
        target_ = mc::stats::percentile(v, 0.5);
    }
    void setOverview(const std::vector<std::string>& names) { overview_ = names; }

    bool has(const std::string& name) {
        try { selected(name); return true; } catch (...) { return false; }
    }

    void paint(Painter& p, int w, int h) override {
        p.rect(0, 0, w, h, &col::white, nullptr);
        const auto& var = vars_[cur_];
        Vec v = data(var.name);
        Color c = var.kind == mc::VarKind::Output ? col::output : col::input;
        auto& sim = mc::Simulation::instance();
        mc::Settings st = sim.settings();
        std::string head = var.name + (var.description.empty() || var.description == "Output" ? "" : "  -  " + var.description);
        std::vector<std::pair<std::string, Vec>> inputs;
        if (!preview_)
            for (auto& x : vars_) if (x.kind == mc::VarKind::Input && x.name != var.name) inputs.push_back({x.name, data(x.name)});
        switch (view_) {
        case Histogram:
        case Cumulative: {
            HistogramOptions o;
            o.title = head;
            o.bins = bins_;
            o.color = c;
            o.cumulative = view_ == Cumulative;
            o.stripe = true;
            o.stripeX = stripeX_;
            o.thresholds = true;
            o.lower = lower_;
            o.upper = upper_;
            if (!preview_) {
                o.markers = {{"worst", mc::stats::percentile(v, st.worst)}, {"best", mc::stats::percentile(v, st.best)}};
            }
            int sth = p.textHeight();   // two statistics lines below the chart
            plot_ = drawHistogram(p, 0, 0, w, h - 2 * sth - 18, v, o);

            // statistics strip
            std::string s1 = "Mean " + niceNum(mc::stats::mean(v)) + "   Min " + niceNum(mc::stats::min(v)) + "   Max " + niceNum(mc::stats::max(v)) +
                             "   Std dev " + niceNum(mc::stats::stdev(v)) + "   Variance " + niceNum(mc::stats::variance(v));
            std::string s2;
            for (double q : {0.05, 0.10, 0.25, 0.5, 0.75, 0.90, 0.95}) s2 += "P" + std::to_string(int(q * 100)) + " " + niceNum(mc::stats::percentile(v, q)) + "   ";
            p.text(8, h - 2 * sth - 12, s1, col::text);
            p.text(8, h - sth - 6, s2, col::muted);
            break;
        }
        case Tornado: {
            // correlation analysis: all inputs and outputs with the selected variable (Spearman)
            std::vector<std::pair<std::string, double>> t;
            for (auto& x : vars_) {
                if (x.name == var.name) continue;
                double r = mc::stats::spearman(data(x.name), v);
                t.push_back({x.name, std::isfinite(r) ? r : 0});
            }
            if (t.size() > 25) t.resize(25);
            std::stable_sort(t.begin(), t.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
            tornadoRows_.clear();
            tornadoNames_.clear();
            for (auto& x : t) tornadoNames_.push_back(x.first);
            std::stable_sort(t.begin(), t.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
            tornadoNames_.clear();
            for (auto& x : t) tornadoNames_.push_back(x.first);
            tornadoRows_.clear();
            drawTornado(p, 0, 0, w, h, "Correlation with " + var.name + " (Spearman) - click a bar for the regression", t, &tornadoRows_);
            break;
        }
        case Swing:
            swingRows_.clear();
            drawSwingTornado(p, 0, 0, w, h, var.name, v, inputs, 0.10, 15, &swingRows_);
            break;
        case DynSens:
            paintDynSens(p, w, h, var, v, inputs);
            break;
        case BoxWhisker:
            paintBoxWhisker(p, w, h, var);
            break;
        case Overview:
            paintOverview(p, w, h);
            break;
        case Spider:
        case SpiderCum:
            drawSpider(p, 0, 0, w, h, var.name, v, inputs, view_ == SpiderCum);
            break;
        case Scatter: {
            std::string in = scatterX_.empty() ? strongestDriver(var.name) : scatterX_;
            Vec x = data(in);
            if (x.empty() || mc::lower(in) == mc::lower(var.name)) {
                p.text(10, 10, "Click a bar in the tornado chart to choose the variable for the scatter plot.", col::text);
                break;
            }
            drawScatter(p, 0, 0, w, h * 6 / 10, x, v, in, var.name, true);
            drawDynamicSensitivity(p, 0, h * 6 / 10, w, h * 4 / 10, x, v, 10, in, var.name);
            break;
        }
        case Variants: {
            auto labels = sim.resultSetLabels();
            if (labels.size() < 2) { p.text(10, 10, "Run a multiple simulation (variants) to compare them here.", col::text); break; }
            std::vector<std::pair<std::string, Vec>> series;
            for (size_t i = 0; i < labels.size(); ++i) {
                Vec d;
                sim.samplesIn(int(i), var.name, d);
                series.push_back({labels[i], d});
            }
            p.text(8, 4, var.name + " by variant", col::text, true);
            drawOverlay(p, 0, p.textHeight(kBoldFont) + 8, w, h - p.textHeight(kBoldFont) - 8, series, bins_, true, probe_, probeOn_);
            break;
        }
        case Decision:
            paintDecision(p, w, h, var, v, inputs);
            break;
        case Statistics:
            drawStatsGrid(p, 10, 10, std::min(w - 20, 900), var.name, var.description, var.cell, v, bins_, 11, h - 20);
            break;
        }
    }

    // Dynamic sensitivity (MC FLO): range of the output's interval means for each input, sorted by
    // range, with the expected value of the output as an orange line. Click a bar for the regression.
    void paintDynSens(Painter& p, int w, int h, const mc::Variable& var, const Vec& v, const std::vector<std::pair<std::string, Vec>>& inputs) {
        struct R { std::string name; double lo, hi; };
        std::vector<R> rs;
        for (auto& [n, x] : inputs) {
            Vec ds = mc::stats::dynamicSensitivity(x, v, 10);
            if (ds.size() >= 3 && std::isfinite(ds[1]) && std::isfinite(ds[2])) rs.push_back({n, ds[1], ds[2]});
        }
        std::stable_sort(rs.begin(), rs.end(), [](const R& a, const R& b) { return a.hi - a.lo > b.hi - b.lo; });
        if (rs.size() > 20) rs.resize(20);
        tornadoNames_.clear();
        tornadoRows_.clear();
        p.rect(0, 0, w, h, &col::white, nullptr);
        p.text(8, 4, "Dynamic sensitivity of " + var.name + " - click a bar for the regression", col::text, true);
        if (rs.empty()) return;
        double lo = rs[0].lo, hi = rs[0].hi, mean = mc::stats::mean(v);
        for (auto& r : rs) { lo = std::min(lo, r.lo); hi = std::max(hi, r.hi); }
        lo = std::min(lo, mean);
        hi = std::max(hi, mean);
        int labelW = 0;
        for (auto& r : rs) labelW = std::max(labelW, p.textWidth(r.name));
        labelW += 16;
        int top = p.textHeight() + 18, rowH = std::max(14, std::min(28, (h - top - 30) / int(rs.size())));
        Plot pl{labelW, top, w - labelW - 20, rowH * int(rs.size()), lo - (hi - lo) * 0.05, hi + (hi - lo) * 0.05, 0, 1};
        Color bar = col::input.mix(col::white, 0.25);
        for (size_t i = 0; i < rs.size(); ++i) {
            int yy = top + int(i) * rowH;
            p.rect(pl.px(rs[i].lo), yy + 2, std::max(1, pl.px(rs[i].hi) - pl.px(rs[i].lo)), rowH - 4, &bar, nullptr);
            p.text(8, yy + (rowH - p.textHeight()) / 2, rs[i].name, col::text);
            tornadoNames_.push_back(rs[i].name);
            tornadoRows_.push_back({yy, yy + rowH});
        }
        p.line(pl.px(mean), top - 6, pl.px(mean), top + pl.h + 2, col::output, 2);
        for (double t : ticks(pl.x0, pl.x1, 5)) {
            std::string s = niceNum(t);
            p.text(pl.px(t) - p.textWidth(s) / 2, top + pl.h + 6, s, col::muted);
        }
        p.text(pl.px(mean) + 4, top - 14, "mean " + niceNum(mean), col::output);
    }

    // Box-Whisker per variant (MC FLO: box 25%-75%, whiskers 15%-85%, median line, mean point)
    void paintBoxWhisker(Painter& p, int w, int h, const mc::Variable& var) {
        auto& sim = mc::Simulation::instance();
        auto labels = sim.resultSetLabels();
        std::vector<std::pair<std::string, Vec>> series;
        if (labels.size() < 2) series.push_back({var.name, data(var.name)});
        else
            for (size_t i = 0; i < labels.size(); ++i) {
                Vec d;
                sim.samplesIn(int(i), var.name, d);
                series.push_back({labels[i], d});
            }
        double lo = 1e300, hi = -1e300;
        for (auto& [n, d] : series) if (!d.empty()) { lo = std::min(lo, mc::stats::min(d)); hi = std::max(hi, mc::stats::max(d)); }
        if (!(hi > lo)) { p.text(10, 10, "No data.", col::text); return; }
        Plot pl = axes(p, 0, 0, w, h, -0.5, double(series.size()) - 0.5, lo, hi, "Box-Whisker of " + var.name + " (box 25%-75%, whiskers 15%-85%)");
        p.rect(pl.x - 20, pl.y + pl.h + 2, pl.w + 40, p.textHeight() + 6, &col::white, nullptr);   // no numeric x axis
        int bw = std::max(10, pl.w / int(series.size()) / 3);
        for (size_t i = 0; i < series.size(); ++i) {
            const Vec& d = series[i].second;
            if (d.empty()) continue;
            int cx = pl.px(double(i));
            double q15 = mc::stats::percentile(d, 0.15), q25 = mc::stats::percentile(d, 0.25), q50 = mc::stats::percentile(d, 0.5),
                   q75 = mc::stats::percentile(d, 0.75), q85 = mc::stats::percentile(d, 0.85), m = mc::stats::mean(d);
            Color c = col::series[i % 8], fill = c.mix(col::white, 0.6);
            p.line(cx, pl.py(q15), cx, pl.py(q25), c, 2);
            p.line(cx, pl.py(q75), cx, pl.py(q85), c, 2);
            p.line(cx - bw / 3, pl.py(q15), cx + bw / 3, pl.py(q15), c, 2);
            p.line(cx - bw / 3, pl.py(q85), cx + bw / 3, pl.py(q85), c, 2);
            p.rect(cx - bw / 2, pl.py(q75), bw, std::max(1, pl.py(q25) - pl.py(q75)), &fill, &c);
            p.line(cx - bw / 2, pl.py(q50), cx + bw / 2, pl.py(q50), c, 3);
            p.ellipse(cx - 4, pl.py(m) - 4, 8, 8, &col::output, nullptr);
            p.ellipse(cx - 2, pl.py(mc::stats::min(d)) - 2, 4, 4, &col::muted, nullptr);
            p.ellipse(cx - 2, pl.py(mc::stats::max(d)) - 2, 4, 4, &col::muted, nullptr);
            p.text(cx - p.textWidth(series[i].first) / 2, pl.y + pl.h + 4, series[i].first, col::text, true);
        }
    }

    // several variables at a glance (cells selected before opening the window)
    void paintOverview(Painter& p, int w, int h) {
        std::vector<std::string> names = overview_;
        if (names.empty())
            for (auto& v : vars_) if (names.size() < 9) names.push_back(v.name);
        int n = int(names.size()), cols = n <= 2 ? n : n <= 4 ? 2 : 3, rows = (n + cols - 1) / std::max(1, cols);
        if (n == 0) return;
        int cw = w / cols, ch = h / rows;
        for (int i = 0; i < n; ++i) {
            Vec d = data(names[i]);
            HistogramOptions o;
            o.title = names[i] + "  (mean " + niceNum(mc::stats::mean(d)) + ", P5 " + niceNum(mc::stats::percentile(d, 0.05)) + ", P95 " +
                      niceNum(mc::stats::percentile(d, 0.95)) + ")";
            o.bins = bins_;
            bool isOut = false;
            for (auto& v : vars_) if (v.name == names[i]) isOut = v.kind == mc::VarKind::Output;
            o.color = isOut ? col::output : col::input;
            drawHistogram(p, (i % cols) * cw, (i / cols) * ch, cw - 4, ch - 4, d, o);
        }
    }

    // Decision matrix (MC FLO 7.8.8): for a target value, the conditional probability that a driver
    // breaches its ambition level when the target is missed, against the conditional damage.
    struct DecisionPoint { std::string name; double prob, damage, rho, compatLo, compatHi; int x, y; };
    void paintDecision(Painter& p, int w, int h, const mc::Variable& var, const Vec& v, const std::vector<std::pair<std::string, Vec>>& inputs) {
        bool above = selectedItemSafe("targetDir") != "<";   // ">": the result should exceed the target
        double n = 0, ok = 0;
        for (double x : v) if (std::isfinite(x)) { n += 1; ok += above ? x > target_ : x < target_; }
        double pSuccess = n > 0 ? ok / n : 0, ambition = 1 - pSuccess;
        decision_.clear();
        double maxDamage = 0;
        for (auto& [name, x] : inputs) {
            double r = mc::stats::spearman(x, v);
            if (!std::isfinite(r) || std::fabs(r) <= 0.05) continue;   // as in MC FLO
            bool up = (r > 0) == above;   // a higher driver helps
            double q = mc::stats::percentile(x, up ? ambition : 1 - ambition);
            double miss = 0, both = 0, dsum = 0;
            Vec good;
            for (size_t i = 0; i < x.size() && i < v.size(); ++i) {
                if (!std::isfinite(x[i]) || !std::isfinite(v[i])) continue;
                bool success = above ? v[i] > target_ : v[i] < target_;
                if (success) { good.push_back(x[i]); continue; }
                miss += 1;
                // strictly beyond the driver's ambition quantile (discrete drivers: yes/no counts correctly)
                if (up ? x[i] < q : x[i] > q) { both += 1; dsum += v[i] - target_; }
            }
            if (miss == 0) continue;
            DecisionPoint d{name, both / miss, both > 0 ? std::fabs(dsum / both) : 0, r, kNaNd(), kNaNd(), 0, 0};
            if (good.size() > 10) { d.compatLo = mc::stats::percentile(good, 0.025); d.compatHi = mc::stats::percentile(good, 0.975); }
            maxDamage = std::max(maxDamage, d.damage);
            decision_.push_back(d);
        }
        p.rect(0, 0, w, h, &col::white, nullptr);
        std::string title = "Decision matrix " + var.name + ":  expected " + niceNum(mc::stats::mean(v)) + "   target " + std::string(above ? "> " : "< ") +
                            niceNum(target_) + "   ambition " + pctText(ambition) + "   P(target reached) " + pctText(pSuccess);
        p.text(8, 4, title, col::text, true);
        if (decision_.empty()) { p.text(8, 26, "No driver with |rank correlation| > 0.05, or the target is never missed.", col::muted); return; }
        int left = 60, top = p.textHeight() + 20, pw = w - left - 20, ph = h - top - 40;
        // risk zones: a smooth gradient green - yellow - orange - red from the lower left (low
        // probability, low damage) to the upper right, drawn as thin diagonal bands
        riskGradient(p, left, top, pw, ph);
        for (int k = 1; k < 3; ++k) {   // thirds as a faint guide
            Color g{255, 255, 255};
            p.line(left + k * pw / 3, top, left + k * pw / 3, top + ph, g, 1, true);
            p.line(left, top + k * ph / 3, left + pw, top + k * ph / 3, g, 1, true);
        }
        p.rect(left, top, pw, ph, nullptr, &col::axis);
        double ymax = maxDamage > 0 ? maxDamage * 1.1 : 1;
        std::vector<std::array<int, 4>> placed;   // label boxes, to avoid overlaps
        for (auto& d : decision_) {
            d.x = left + int(d.prob * pw);
            d.y = top + ph - int(d.damage / ymax * ph);
            Color c = d.rho >= 0 ? col::input : col::red;
            p.ellipse(d.x - 6, d.y - 6, 12, 12, &c, &col::black);
            int tw = p.textWidth(d.name), th = p.textHeight();
            int lx = d.x + 8 + tw > left + pw ? d.x - 8 - tw : d.x + 8, ly = d.y - th / 2;
            for (int tries = 0; tries < 12; ++tries) {
                bool hit = false;
                for (auto& b : placed)
                    if (lx < b[0] + b[2] && lx + tw > b[0] && ly < b[1] + b[3] && ly + th > b[1]) { hit = true; break; }
                if (!hit) break;
                ly += th;
            }
            placed.push_back({lx, ly, tw, th});
            p.text(lx, ly, d.name, col::text);
        }
        p.text(left, top + ph + 6, "conditional probability (driver breaches its ambition level | target missed)", col::muted);
        p.text(4, top - p.textHeight() - 2, "cond. damage", col::muted);
        for (double t : {0.0, 0.5, 1.0}) {
            std::string lb = pctText(t);
            int lw = p.textWidth(lb);
            p.text(left + int(t * pw) - (t == 0 ? 0 : t == 1 ? lw : lw / 2), top + ph + 6 + p.textHeight(), lb, col::muted);
        }
        p.text(4, top, niceNum(ymax), col::muted);
        // tooltip of the point under the mouse
        for (auto& d : decision_)
            if (std::abs(mouseX_ - d.x) <= 8 && std::abs(mouseY_ - d.y) <= 8) {
                std::vector<std::string> lines{d.name, "conditional probability " + pctText(d.prob), "conditional damage " + niceNum(d.damage),
                                               "rank correlation " + niceNum(d.rho)};
                if (std::isfinite(d.compatLo))
                    lines.push_back("values compatible with the target (P95): " + niceNum(d.compatLo) + " to " + niceNum(d.compatHi));
                int tw = 0, th = p.textHeight();
                for (auto& l : lines) tw = std::max(tw, p.textWidth(l));
                int bx = std::min(w - tw - 16, d.x + 12), by = std::min(h - th * int(lines.size()) - 12, d.y + 12);
                Color bg{255, 252, 235};
                p.roundRect(bx, by, tw + 12, th * int(lines.size()) + 8, 6, &bg, &col::axis);
                for (size_t i = 0; i < lines.size(); ++i) p.text(bx + 6, by + 4 + int(i) * th, lines[i], col::text, i == 0);
                break;
            }
    }
    static double kNaNd() { return std::nan(""); }

    // colour of the risk score s (0 = lower left, 1 = upper right)
    static Color riskColor(double s) {
        static const std::pair<double, Color> stops[] = {
            {0.0, {196, 234, 196}}, {0.35, {226, 242, 196}}, {0.5, {252, 244, 190}}, {0.68, {252, 218, 182}}, {1.0, {242, 176, 176}}};
        s = std::min(1.0, std::max(0.0, s));
        for (size_t i = 1; i < std::size(stops); ++i)
            if (s <= stops[i].first) {
                double t = (s - stops[i - 1].first) / (stops[i].first - stops[i - 1].first);
                return stops[i - 1].second.mix(stops[i].second, t);
            }
        return stops[std::size(stops) - 1].second;
    }
    // fills the plot with bands of equal score: the band a <= xn + yn <= b clipped to the unit square
    static void riskGradient(Painter& p, int left, int top, int pw, int ph) {
        const int bands = 60;
        for (int k = 0; k < bands; ++k) {
            double a = 2.0 * k / bands, b = 2.0 * (k + 1) / bands + 0.01;   // slight overlap: no seams
            std::vector<std::pair<double, double>> poly{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            auto clip = [&poly](double c, bool keepBelow) {   // half-plane x + y <= c (or >= c)
                std::vector<std::pair<double, double>> out;
                auto inside = [&](const std::pair<double, double>& q) { return keepBelow ? q.first + q.second <= c : q.first + q.second >= c; };
                for (size_t i = 0; i < poly.size(); ++i) {
                    auto P = poly[i], Q = poly[(i + 1) % poly.size()];
                    bool ip = inside(P), iq = inside(Q);
                    if (ip) out.push_back(P);
                    if (ip != iq) {
                        double sp = P.first + P.second, sq = Q.first + Q.second, t = (c - sp) / (sq - sp);
                        out.push_back({P.first + t * (Q.first - P.first), P.second + t * (Q.second - P.second)});
                    }
                }
                poly = out;
            };
            clip(a, false);
            clip(b, true);
            if (poly.size() < 3) continue;
            std::vector<int> xs, ys;
            for (auto& q : poly) {
                xs.push_back(left + int(q.first * pw + 0.5));
                ys.push_back(top + ph - int(q.second * ph + 0.5));
            }
            Color c = riskColor((k + 0.5) / bands);
            p.polygon(xs, ys, &c, nullptr);
        }
    }

    std::string selectedItemSafe(const std::string& n) {
        try { return selectedItem(n); } catch (...) { return {}; }
    }

    void mouseDown(const MouseInfo& m) override {
        if (view_ == Histogram || view_ == Cumulative) {
            if (m.x >= plot_.x && m.x <= plot_.x + plot_.w) { stripeX_ = plot_.vx(m.x); repaint(); }
        } else if (view_ == Tornado || view_ == DynSens) {
            for (size_t i = 0; i < tornadoRows_.size(); ++i)
                if (m.y >= tornadoRows_[i].first && m.y < tornadoRows_[i].second) {
                    auto inputs = itemsOf("input");
                    for (size_t k = 0; k < inputs.size(); ++k)
                        if (inputs[k] == tornadoNames_[i]) select("input", int(k));
                    scatterX_ = tornadoNames_[i];
                    setView(Scatter);
                }
        } else if (view_ == Swing) {   // click a bar: scatter of that input against the output
            for (auto& [name, r] : swingRows_)
                if (m.y >= r.first && m.y < r.second) {
                    auto inputs = itemsOf("input");
                    for (size_t k = 0; k < inputs.size(); ++k)
                        if (inputs[k] == name) select("input", int(k));
                    scatterX_ = name;
                    setView(Scatter);
                    break;
                }
        }
    }
    void mouseMove(const MouseInfo& m, bool dragging) override {
        if (dragging && (view_ == Histogram || view_ == Cumulative) && m.x >= plot_.x && m.x <= plot_.x + plot_.w) {
            stripeX_ = plot_.vx(m.x);
            repaint();
        }
        if (view_ == Variants) { probe_ = m.x; probeOn_ = true; repaint(); }
        if (view_ == Decision) { mouseX_ = m.x; mouseY_ = m.y; repaint(); }
    }
    // x variables of the scatter: the inputs, then the outputs
    std::vector<std::string> itemsOf(const std::string&) {
        std::vector<std::string> r;
        for (auto& v : vars_) if (v.kind == mc::VarKind::Input) r.push_back(v.name);
        for (auto& v : vars_) if (v.kind != mc::VarKind::Input) r.push_back(v.name);
        return r;
    }
    int index(const std::string& name) const {
        for (size_t i = 0; i < vars_.size(); ++i) if (mc::lower(vars_[i].name) == mc::lower(name)) return int(i);
        return -1;
    }
    // the scatter of x (horizontal) against y (vertical, the window's variable)
    // the input with the largest |rank correlation| with name (the default x of the scatter)
    std::string strongestDriver(const std::string& name) {
        Vec v = data(name);
        std::string best;
        double bestR = -1;
        for (auto& x : vars_) {
            if (mc::lower(x.name) == mc::lower(name) || x.kind != mc::VarKind::Input) continue;
            double r = std::fabs(mc::stats::spearman(data(x.name), v));
            if (std::isfinite(r) && r > bestR) { bestR = r; best = x.name; }
        }
        return best;
    }
    void updateXVisibility() {
        try {
            setVisible("linput", corrSelected_);
            setVisible("input", corrSelected_);
        } catch (...) {
        }
    }
    // a correlation: its entry in the Variable list, the X-variable list and the scatter of a against b
    void showCorrelation(const std::string& a, const std::string& b) {
        for (size_t i = 0; i < corrs_.size(); ++i)
            if ((mc::lower(corrs_[i].a) == mc::lower(a) && mc::lower(corrs_[i].b) == mc::lower(b)) ||
                (mc::lower(corrs_[i].a) == mc::lower(b) && mc::lower(corrs_[i].b) == mc::lower(a)))
                select("var", int(vars_.size() + i));
        corrSelected_ = true;
        updateXVisibility();
        showScatter(a, b);
    }
    void showScatter(const std::string& x, const std::string& y) {
        int iy = index(y);
        if (iy >= 0 && iy != cur_) { cur_ = iy; resetVariable(); }
        scatterX_ = x;
        auto xs = itemsOf("input");
        for (size_t k = 0; k < xs.size(); ++k)
            if (mc::lower(xs[k]) == mc::lower(x)) select("input", int(k));
        setView(Scatter);
    }
    void setView(View v) {
        view_ = v;
        auto vl = viewList();
        for (size_t i = 0; i < vl.size(); ++i) if (vl[i].first == v) select("view", int(i));
        repaint();
    }
    bool key(int code, int, bool, bool) override {
        if (code == awt::Key::LEFT || code == awt::Key::RIGHT) {
            cur_ = (cur_ + (code == awt::Key::RIGHT ? 1 : int(vars_.size()) - 1)) % int(vars_.size());
            select("var", cur_);
            scatterX_.clear();
            corrSelected_ = false;
            updateXVisibility();
            resetVariable();
            repaint();
            return true;
        }
        return false;
    }
    void itemChanged(const std::string& name) override {
        if (name == "var") {
            int s = std::max(0, selected("var"));
            if (s >= int(vars_.size()) && s - vars_.size() < corrs_.size()) {   // a correlation: its two variables
                const auto& c = corrs_[s - vars_.size()];
                corrSelected_ = true;
                updateXVisibility();
                showScatter(c.a, c.b);
                return;
            }
            cur_ = s;
            scatterX_.clear();
            corrSelected_ = false;
            updateXVisibility();
            resetVariable();
        } else if (name == "input") {   // choosing the x variable shows the scatter
            scatterX_ = selectedItemSafe("input");
            if (view_ != Scatter) { setView(Scatter); return; }
        } else if (name == "view") {
            int s = selected("view");
            auto vl = viewList();
            if (s >= 0 && s < int(vl.size())) view_ = vl[s].first;
        } else if (name == "variant") {
            int s = selected("variant");
            if (s >= 0) {
                mc::Simulation::instance().selectResultSet(s);
                docOf(frame_).recalc();  // refresh the fMC_ functions of the model
            }
        }
        repaint();
    }
    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "zoomIn" || b == "zoomOut") { setZoom(b == "zoomIn" ? zoom() * 1.15 : zoom() / 1.15); return; }
        if (b == "setCertainty") {
            double c = parseOr(text("certainty"), -1);
            if (c > 1) c /= 100;
            if (!(c > 0 && c < 1)) { message("Enter a certainty level between 0 and 100%.", true); return; }
            certainty_ = c;
            Vec v = data(vars_[cur_].name);
            lower_ = mc::stats::percentile(v, (1 - c) / 2);
            upper_ = mc::stats::percentile(v, 1 - (1 - c) / 2);
            setText("lower", niceNum(lower_));
            setText("upper", niceNum(upper_));
            if (view_ != Histogram && view_ != Cumulative) setView(Histogram);
            repaint();
            return;
        }
        if (b == "prev" || b == "next") { key(b == "next" ? awt::Key::RIGHT : awt::Key::LEFT, 0, false, false); return; }
        if (b == "applyBins") {
            int nb = int(parseOr(text("bins"), bins_));
            if (nb < 2 || nb > 1000) { message("Bins must be between 2 and 1000.", true); return; }
            bins_ = nb;
        } else if (b == "thresholds") {
            bool ok1, ok2;
            double lo = parseOr(text("lower"), lower_, &ok1), hi = parseOr(text("upper"), upper_, &ok2);
            if (!ok1 && !ok2) { message("Enter a lower and/or an upper limit.", true); return; }
            if (hi < lo) std::swap(lo, hi);
            lower_ = lo;
            upper_ = hi;
            if (view_ != Histogram && view_ != Cumulative) setView(Histogram);
        } else if (b == "setStripe") {
            std::string t = text("stripe");
            Vec v = data(vars_[cur_].name);
            if (!t.empty() && t[0] == ';') {
                double q = parseOr(t.substr(1), -1);
                if (!(q >= 0 && q <= 1)) { message("Percentile must be between 0 and 1, e.g. ;0.35", true); return; }
                stripeX_ = mc::stats::percentile(v, q);
            } else {
                bool ok;
                double x = parseOr(t, 0, &ok);
                if (!ok) { message("Enter a value, or ; followed by a percentile.", true); return; }
                stripeX_ = x;
            }
            if (view_ != Histogram && view_ != Cumulative) setView(Histogram);
        } else if (b == "applyTarget") {
            bool ok;
            double t = parseOr(text("target"), 0, &ok);
            if (!ok) { message("Enter a number.", true); return; }
            target_ = t;
            setView(Decision);
        } else if (b == "store") {
            createReport(ctx_, false, nullptr, frame_->getController()->getModel());
        } else if (b == "export") {
            exportDialog();
        } else if (b == "comment") {
            ModelDoc d = docOf(frame_);
            table::CellAddress a;
            const auto& var = vars_[cur_];
            bool found = false;
            for (auto& vc : d.scan())
                if (mc::lower(vc.name) == mc::lower(var.name)) { a = vc.addr; found = true; }
            if (!found && !d.currentCell(a)) { message("Select a cell for the comment.", true); return; }
            Vec v = data(var.name);
            storeAsComment(d.model(), a.Sheet, a.Column, a.Row,
                           var.name + ": mean " + niceNum(mc::stats::mean(v)) + ", P5 " + niceNum(mc::stats::percentile(v, 0.05)) + ", P95 " +
                               niceNum(mc::stats::percentile(v, 0.95)));
            message("The diagram was stored as a comment of cell " + d.cellName(a) + ".");
        } else if (b == "rerun") {
            RunOptions o = loadOptions(docOf(frame_));
            if (o.s.seed == -1) o.s.seed = 0;
            runSimulation(ctx_, frame_, o);
            vars_ = mc::Simulation::instance().resultVariables();
            std::stable_partition(vars_.begin(), vars_.end(), [](const mc::Variable& v) { return v.kind == mc::VarKind::Output; });
            cur_ = std::min(cur_, int(vars_.size()) - 1);
            resetVariable();
        }
        repaint();
    }
    void exportDialog() {
        std::vector<FormField> f = {{"File (URL or path)", homeDir() + "/montecalc_chart.png"},
                                    {"Format", "PNG"}};
        f[1].options = {"PNG", "JPEG", "BMP"};
        if (!showForm(ctx_, "Export image", f)) return;
        std::string url = fileUrl(f[0].value);
        std::string mime = f[1].value == "JPEG" ? "image/jpeg" : f[1].value == "BMP" ? "image/bmp" : "image/png";
        int w, h;
        canvasSize(w, h);
        if (exportImage(url, mime, w, h, 2.0)) message("Saved " + url);
    }

private:
    bool preview_;
    bool pair_ = false;
    bool corrSelected_ = false;   // a correlation is chosen: the X-variable list is shown
    std::string scatterX_;        // x of the scatter ("" = the strongest driver)
    Vec previewData_, previewDataB_;
    std::vector<mc::CorrelationRef> corrs_;
    std::vector<mc::Variable> vars_;
    int cur_ = 0;
    View view_ = Histogram;
    int bins_ = 20;
    double stripeX_ = 0, lower_ = 0, upper_ = 0, target_ = 0;
    Plot plot_{0, 0, 1, 1, 0, 1, 0, 1};
    std::vector<std::pair<int, int>> tornadoRows_;
    std::vector<std::pair<std::string, std::pair<int, int>>> swingRows_;
    std::vector<std::string> tornadoNames_;
    int probe_ = 0;
    bool probeOn_ = false;
    double certainty_ = 0.95;
    int mouseX_ = -100, mouseY_ = -100;
    std::vector<DecisionPoint> decision_;
    std::vector<std::string> overview_;
};

// A correlation as a scatter of its two variables: the simulated values when the last simulation has
// both, otherwise a sample of the copula (ranks 0..1) with the cell's coefficient and type.
std::shared_ptr<ResultWindow> correlationScatter(const Ctx& ctx, const Frame& frame, const VarCell& v) {
    if (hasResults(v.corrA) && hasResults(v.corrB)) {
        auto w = std::make_shared<ResultWindow>(ctx, frame, v.corrB);
        w->show();
        w->afterShow();
        w->showCorrelation(v.corrA, v.corrB);
        return w;
    }
    double rho = docOf(frame).cell(v.addr).value();
    int type = 0;
    std::string func;
    std::vector<std::string> args;
    if (mc::formula::findCall(v.formula, "FLOSIMULA_", func, args) && args.size() > 3) type = std::atoi(args[3].c_str());
    std::mt19937_64 rng(1);
    auto cr = mc::generateUniforms(2, 1000, {{0, 1, rho, type}}, rng);
    auto w = std::make_shared<ResultWindow>(ctx, frame, v.corrB, v.corrA + " (rank)", cr.u[0], v.corrB + " (rank)", cr.u[1]);
    w->show();
    w->afterShow();
    w->showCorrelation(v.corrA + " (rank)", v.corrB + " (rank)");
    return w;
}

void openCorrelationScatter(const Ctx& ctx, const Frame& frame, const VarCell& v) { CanvasWindow::keep(correlationScatter(ctx, frame, v)); }

void openResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& view, const std::string& exportTo,
                      const std::string& scatterX) {
    // a correlation (by name, or the selected cell when no variable is given) opens as the scatter
    // of its two variables instead of a histogram
    {
        ModelDoc d = docOf(frame);
        table::CellAddress cur;
        bool haveCur = variable.empty() && d.currentCell(cur);
        for (auto& vc : d.scan()) {
            if (vc.kind != mc::VarKind::Correlation) continue;
            bool byName = !variable.empty() && mc::lower(vc.name) == mc::lower(variable);
            bool byCell = haveCur && vc.addr.Sheet == cur.Sheet && vc.addr.Column == cur.Column && vc.addr.Row == cur.Row;
            if (!byName && !byCell) continue;
            auto w = correlationScatter(ctx, frame, vc);
            if (!exportTo.empty()) { w->exportImage(exportTo, "image/png", 900, 560); w->close(); return; }
            CanvasWindow::keep(w);
            return;
        }
    }
    auto w = std::make_shared<ResultWindow>(ctx, frame, variable);
    w->show();
    w->afterShow();
    static const std::map<std::string, ResultWindow::View> views = {
        {"Histogram", ResultWindow::Histogram}, {"Cumulative", ResultWindow::Cumulative}, {"Tornado", ResultWindow::Swing},
        {"Correlation", ResultWindow::Tornado}, {"DynamicSensitivity", ResultWindow::DynSens}, {"Spider", ResultWindow::Spider},
        {"SpiderCum", ResultWindow::SpiderCum}, {"Scatter", ResultWindow::Scatter}, {"Variants", ResultWindow::Variants},
        {"BoxWhisker", ResultWindow::BoxWhisker}, {"Decision", ResultWindow::Decision}, {"Statistics", ResultWindow::Statistics},
        {"Overview", ResultWindow::Overview}};
    // several variable cells selected: show them at a glance (MC FLO "analyse multiple variables")
    if (variable.empty() && view.empty()) {
        ModelDoc d = docOf(frame);
        table::CellRangeAddress r;
        if (d.selectionRange(r) && (r.EndRow > r.StartRow || r.EndColumn > r.StartColumn)) {
            std::vector<std::string> names;
            for (auto& vc : d.scan())
                if (vc.addr.Sheet == r.Sheet && vc.addr.Row >= r.StartRow && vc.addr.Row <= r.EndRow && vc.addr.Column >= r.StartColumn &&
                    vc.addr.Column <= r.EndColumn && vc.kind != mc::VarKind::Correlation && hasResults(vc.name))
                    names.push_back(vc.name);
            if (names.size() > 1) { w->setOverview(names); w->setView(ResultWindow::Overview); }
        }
    }
    if (views.count(view)) w->setView(views.at(view));
    if (!scatterX.empty()) w->showScatter(scatterX, variable);
    if (!exportTo.empty()) {
        w->exportImage(exportTo, "image/png", 900, 560);
        w->close();
        return;
    }
    CanvasWindow::keep(w);
}

// ============================================================ Choose Wizard

class WizardWindow : public CanvasWindow {
public:
    WizardWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - Choose Wizard", 440, 280, 130) {
        for (const auto& f : mc::floFunctions())
            if (f.kind == mc::FloFunction::Dist || f.kind == mc::FloFunction::TimeSeries) funcs_.push_back(&f);
        std::vector<std::string> names;
        for (auto* f : funcs_) names.push_back(f->label + (f->discrete ? " (discrete)" : f->kind == mc::FloFunction::TimeSeries ? " (time series)" : ""));
        int x = panelX(), w = panelW(), y = 6;
        addLabel("lfunc", "Function", x, y, w); y += 10;
        addList("func", names, x, y, w, 12, true); y += 16;
        addLabel("lparams", "Parameters", x, y, w); y += 10;
        for (int i = 0; i < 5; ++i) {
            addLabel("p" + std::to_string(i), "", x, y + 2, w / 2 + 6);
            addEdit("e" + std::to_string(i), "", x + w / 2 + 8, y, w / 2 - 8);
            y += 14;
        }
        addLabel("lopt", "Options: Number / Min / Max / Round", x, y, w); y += 10;
        addEdit("number", "", x, y, w / 4 - 2); addEdit("min", "", x + w / 4, y, w / 4 - 2);
        addEdit("max", "", x + w / 2, y, w / 4 - 2); addEdit("round", "", x + 3 * w / 4, y, w / 4); y += 16;
        addLabel("liter", "Number of iterations / bins", x, y, w); y += 10;
        addEdit("iter", "10000", x, y, w / 2 - 2); addEdit("bins", "30", x + w / 2, y, w / 2); y += 16;
        addButton("show", "Show", x, y, w / 2 - 1); addButton("real", "Load real data", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("custom", "Custom defined (from selection)", x, y, w); y += 16;
        addLabel("lstripe", "Magic stripe (x, or ;p)", x, y, w); y += 10;
        addEdit("stripe", "", x, y, w - 26); addButton("setStripe", "Set", x + w - 24, y, 24, 12); y += 16;
        addLabel("lname", "Name of distribution", x, y, w); y += 10;
        addEdit("name", "", x, y, w); y += 16;
        addButton("save", "Save into the selected cell", x, y, w); y += 16;
        addButton("close", "Close", x, y, w);
    }
    void afterShow(const std::string& function = "") {
        int sel = 1;
        for (size_t i = 0; i < funcs_.size(); ++i)
            if (!function.empty() && (mc::lower(funcs_[i]->label) == mc::lower(function) || mc::lower(funcs_[i]->name) == mc::lower(function) ||
                                      mc::lower(funcs_[i]->name) == "flosimula_" + mc::lower(function)))
                sel = int(i);
        select("func", sel);
        updateParams();
    }

    void updateParams() {
        int s = std::max(0, selected("func"));
        const auto* f = funcs_[s];
        static const std::map<std::string, std::vector<std::string>> defaults = {
            {"Uniform", {"0", "1"}}, {"Triangular", {"80", "140", "100"}}, {"PERT", {"80", "140", "100"}}, {"Normal", {"100", "15"}},
            {"LogNormal", {"0", "4.6", "0.2"}}, {"Beta", {"2", "5", "0", "1"}}, {"Gumbel", {"0", "1"}}, {"Gamma", {"0", "3", "2"}},
            {"Exponential", {"0", "2"}}, {"TruncNormal", {"0", "1", "-1", "2"}}, {"Weibull", {"0", "3", "2"}}, {"chi2", {"4"}},
            {"studentt", {"5"}}, {"Pareto", {"1", "3"}}, {"Fisher", {"5", "10"}}, {"ArcSin", {"0", "1"}}, {"Cauchy", {"0", "1"}},
            {"Frechet", {"0", "1", "3"}}, {"Logistic", {"0", "1"}}, {"DiscreteUniform", {"1", "6"}}, {"Bernoulli", {"0.3"}},
            {"Binomial", {"0.3", "20"}}, {"Poisson", {"4"}}, {"Pascal", {"0.4", "3"}}, {"Geometric", {"0.25"}},
            {"Hypergeometric", {"10", "45", "20"}}, {"AR(1)", {"10", "0.7", "1", "0"}}, {"MA(1)", {"10", "0.5", "1"}},
            {"ARMA(1,1)", {"10", "0.6", "1", "0.3"}}, {"ARCH(1)", {"0.1", "0.5"}}, {"Wiener", {"100", "0.2", "0.05", "1", "0"}}};
        auto d = defaults.count(f->label) ? defaults.at(f->label) : std::vector<std::string>{};
        for (int i = 0; i < 5; ++i) {
            bool used = i < int(f->params.size());
            setLabel("p" + std::to_string(i), used ? f->params[i].name : "");
            setEnabled("e" + std::to_string(i), used);
            setText("e" + std::to_string(i), used && i < int(d.size()) ? d[i] : "");
        }
        custom_ = false;
        sample();
    }

    std::vector<mc::Value> args() {
        std::vector<mc::Value> a;
        if (custom_) {
            std::vector<std::vector<mc::Value>> rows;
            for (auto& r : customData_) {
                std::vector<mc::Value> line;
                for (double v : r) line.push_back(std::isfinite(v) ? mc::Value::number(v) : mc::Value());
                rows.push_back(line);
            }
            a.push_back(mc::Value::array(rows));
            a.push_back(mc::Value::number(parseOr(text("bins"), 0)));
            a.push_back(mc::Value::text(kPreviewName));
        } else {
            const auto* f = funcs_[std::max(0, selected("func"))];
            for (size_t i = 0; i < f->params.size(); ++i) {
                std::string t = text("e" + std::to_string(i));
                if (t.empty() && f->minParams && i >= f->minParams) break;   // optional parameter
                bool ok;
                double v = parseOr(t, 0, &ok);
                if (!ok) throw std::runtime_error(std::string("enter a number for ") + f->params[i].name);
                a.push_back(mc::Value::number(v));
            }
            // a (temporary) name so that a time series path continues over the preview iterations
            a.push_back(mc::Value::text(kPreviewName));
        }
        // MC FLO order after the name: Number; Min; Max; Round
        for (const char* o : {"number", "min", "max", "round"}) {
            std::string t = text(o);
            bool ok;
            double v = parseOr(t, 0, &ok);
            a.push_back(t.empty() || !ok ? mc::Value() : mc::Value::number(v));
        }
        while (!a.empty() && a.back().kind == mc::Value::Empty) a.pop_back();
        return a;
    }
    static constexpr const char* kPreviewName = "__MonteCalc_preview__";

    void sample() {
        auto& sim = mc::Simulation::instance();
        if (sim.busy()) return;
        error_.clear();
        data_.clear();
        int n = std::min(1000000, std::max(10, int(parseOr(text("iter"), 10000))));
        const mc::FloFunction* f = custom_ ? mc::findFlo("FLOsimula_UserDefined") : funcs_[std::max(0, selected("func"))];
        mc::Mode prev = sim.mode();
        try {
            auto a = args();
            sim.setMode(mc::Mode::Random);
            sim.clearSeriesState();
            for (int i = 0; i < n; ++i) data_.push_back(mc::evalFlo(*f, a));
            sim.clearSeriesState();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        sim.setMode(prev);
        if (!data_.empty()) stripe_ = mc::stats::percentile(data_, 0.5);
        repaint();
    }

    void paint(Painter& p, int w, int h) override {
        p.rect(0, 0, w, h, &col::white, nullptr);
        if (!error_.empty()) { p.text(10, 10, "Parameters: " + error_, col::red); return; }
        if (data_.empty()) { p.text(10, 10, "Choose a function and press Show.", col::text); return; }
        HistogramOptions o;
        const mc::FloFunction* f = custom_ ? mc::findFlo("FLOsimula_UserDefined") : funcs_[std::max(0, selected("func"))];
        o.title = (custom_ ? "Custom defined" : f->label) + " - " + std::to_string(data_.size()) + " iterations";
        o.bins = std::max(2, int(parseOr(text("bins"), 30)));
        o.stripe = true;
        o.stripeX = stripe_;
        o.realData = real_.empty() ? nullptr : &real_;
        int statsW = 180;
        plot_ = drawHistogram(p, 0, 0, w - statsW, h, data_, o);
        std::vector<std::vector<std::string>> rows{{"Statistic", "Value"}};
        rows.push_back({"Mean", niceNum(mc::stats::mean(data_))});
        rows.push_back({"Minimum", niceNum(mc::stats::min(data_))});
        rows.push_back({"Maximum", niceNum(mc::stats::max(data_))});
        rows.push_back({"Variance", niceNum(mc::stats::variance(data_))});
        rows.push_back({"Std deviation", niceNum(mc::stats::stdev(data_))});
        for (double q : {0.01, 0.05, 0.10, 0.25, 0.5, 0.75, 0.90, 0.95, 0.99})
            rows.push_back({"P" + std::to_string(int(std::round(q * 100))), niceNum(mc::stats::percentile(data_, q))});
        drawTable(p, w - statsW + 4, 6, statsW - 8, rows, {95, 77});
    }

    void mouseDown(const MouseInfo& m) override {
        if (m.right) { loadReal(); return; }  // right click loads the selected real data (as in MC FLO)
        if (m.x >= plot_.x && m.x <= plot_.x + plot_.w) { stripe_ = plot_.vx(m.x); repaint(); }
    }
    void mouseMove(const MouseInfo& m, bool dragging) override {
        if (dragging && m.x >= plot_.x && m.x <= plot_.x + plot_.w) { stripe_ = plot_.vx(m.x); repaint(); }
    }

    void loadReal() {
        std::vector<std::string> label;
        real_.clear();
        for (auto& r : docOf(frame_).selectionValues(&label))
            for (double v : r) if (std::isfinite(v)) real_.push_back(v);
        if (real_.empty()) message("Select the historical data in the sheet first.", true);
        repaint();
    }

    void itemChanged(const std::string& n) override {
        if (n == "func") updateParams();
    }

    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "show") { sample(); return; }
        if (b == "real") { loadReal(); return; }
        if (b == "setStripe") {
            std::string t = text("stripe");
            if (!t.empty() && t[0] == ';') stripe_ = mc::stats::percentile(data_, std::min(1.0, std::max(0.0, parseOr(t.substr(1), 0.5))));
            else stripe_ = parseOr(t, stripe_);
            repaint();
            return;
        }
        if (b == "custom") {
            ModelDoc d = docOf(frame_);
            table::CellRangeAddress r;
            std::vector<std::string> label;
            customData_ = d.selectionValues(&label);
            if (!d.selectionRange(r) || customData_.empty()) { message("Select the data (or a histogram / quantile table) first.", true); return; }
            // skip a leading text cell (series name) as described in the manual
            if (!label.empty()) {
                setText("name", mc::formula::sanitizeName(label[0]));
                if (!customData_.empty() && !customData_[0].empty() && !std::isfinite(customData_[0][0])) {
                    if (customData_.size() > 1) { customData_.erase(customData_.begin()); r.StartRow += 1; }
                    else { customData_[0].erase(customData_[0].begin()); r.StartColumn += 1; }
                }
            }
            customRange_ = d.cellName(table::CellAddress(r.Sheet, r.StartColumn, r.StartRow), false) + ":" +
                           d.cellName(table::CellAddress(r.Sheet, r.EndColumn, r.EndRow), false);
            customSheet_ = r.Sheet;
            custom_ = true;
            if (customData_[0].size() == 1 && customData_.size() > 1 && text("bins") == "30") setText("bins", "0");
            sample();
            return;
        }
        if (b == "save") {
            std::string name = mc::formula::sanitizeName(text("name"));
            if (text("name").empty()) { message("Enter a name of the distribution.", true); return; }
            std::string f;
            if (custom_) {
                ModelDoc d = docOf(frame_);
                table::CellAddress a;
                d.currentCell(a);
                std::string range = customRange_;
                if (a.Sheet != customSheet_) range = "$'" + d.sheetNames()[customSheet_] + "'." + range;
                f = "=FLOsimula_UserDefined(" + range + ";" + text("bins") + ";" + quote(name) + ")";   // (Data; NumberOfBins; Name)
            } else {
                const auto* fn = funcs_[std::max(0, selected("func"))];
                // MC FLO order: parameters; Variable_name; Number; Min; Max; Round
                std::vector<std::string> parts;
                for (size_t i = 0; i < fn->params.size(); ++i) {
                    std::string t = text("e" + std::to_string(i));
                    if (t.empty() && fn->minParams && i >= fn->minParams) break;
                    parts.push_back(t);
                }
                parts.push_back(quote(name));
                std::vector<std::string> opts = {text("number"), text("min"), text("max"), text("round")};
                while (!opts.empty() && opts.back().empty()) opts.pop_back();
                for (auto& o : opts) parts.push_back(o);
                f = "=" + fn->name + "(";
                for (size_t i = 0; i < parts.size(); ++i) f += (i ? ";" : "") + parts[i];
                f += ")";
            }
            insertIntoCurrentCell(frame_, f, name, mc::VarKind::Input);
            return;
        }
    }

private:
    std::vector<const mc::FloFunction*> funcs_;
    Vec data_, real_;
    std::string error_;
    double stripe_ = 0;
    Plot plot_{0, 0, 1, 1, 0, 1, 0, 1};
    bool custom_ = false;
    std::vector<std::vector<double>> customData_;
    std::string customRange_;
    int customSheet_ = 0;
};

void openWizard(const Ctx& ctx, const Frame& frame, const std::string& function) {
    auto w = std::make_shared<WizardWindow>(ctx, frame);
    w->show();
    w->afterShow(function);
    CanvasWindow::keep(w);
}

// ============================================================ Fit distribution / time series

class FitWindow : public CanvasWindow {
public:
    FitWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - Fit distribution", 460, 280, 150) {
        std::vector<std::string> label;
        ModelDoc d = docOf(frame);
        for (auto& r : d.selectionValues(&label))
            for (double v : r) if (std::isfinite(v)) data_.push_back(v);
        if (data_.size() < 5) throw std::runtime_error("select at least 5 historical values in the sheet before calling Fit distribution");
        name_ = label.empty() ? "" : mc::formula::sanitizeName(label[0]);
        int x = panelX(), w = panelW(), y = 6;
        addLabel("lmode", "Fit", x, y, w); y += 10;
        addList("mode", {"Distributions", "Time series"}, x, y, w, 12, true); y += 16;
        addCheck("preferHigh", "Prefer higher values than lower ones", x, y, w, false); y += 13;
        addLabel("lthr", "... from value", x, y + 2, 50); addEdit("thr", niceNum(mc::stats::percentile(data_, 0.75)), x + 52, y, w - 52); y += 16;
        addLabel("lfc", "Number of forecast data", x, y + 2, 90); addEdit("fc", "12", x + 92, y, w - 92); y += 16;
        addButton("fit", "Start fitting data", x, y, w); y += 16;
        addLabel("lres", "Results (AIC window): select a formula", x, y, w); y += 10;
        addList("results", {}, x, y, w, 96, false); y += 100;
        addButton("simulate", "Simulate", x, y, w / 2 - 1); addButton("copy", "Copy results to sheet", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addCheck("qq", "Q-Q plot of the selected fit", x, y, w, false); y += 13;
        addLabel("lname", "Name", x, y + 2, 24); addEdit("name", name_, x + 26, y, w - 26); y += 16;
        addButton("insert", "Insert formula into current cell", x, y, w); y += 16;
        addButton("close", "Close", x, y, w);

        setAnchor("results", AnchorRight | StretchH);
        setAnchor("simulate", AnchorRight | AnchorBottom);
        setAnchor("copy", AnchorRight | AnchorBottom);
        setAnchor("qq", AnchorRight | AnchorBottom);
        setAnchor("lname", AnchorRight | AnchorBottom);
        setAnchor("name", AnchorRight | AnchorBottom);
        setAnchor("insert", AnchorRight | AnchorBottom);
        setAnchor("close", AnchorRight | AnchorBottom);
    }

    void fit() {
        tsMode_ = selected("mode") == 1;
        std::vector<std::string> items;
        try {
            if (!tsMode_) {
                mc::FitOptions o;
                o.preferHigh = checked("preferHigh");
                o.highThreshold = parseOr(text("thr"), 0);
                dfits_ = mc::fitDistributions(data_, o);
                for (auto& f : dfits_) {
                    char b[300];
                    if (f.ok) std::snprintf(b, sizeof b, "%s  RSS %.4g  KS p %.3f  AIC %.1f  %s", f.name.c_str(), f.rss, f.ksPValue, f.aic, f.formula.c_str());
                    else std::snprintf(b, sizeof b, "%s  - %s", f.name.c_str(), f.error.c_str());
                    items.push_back(b);
                }
            } else {
                tfits_ = mc::fitTimeSeries(data_);
                for (auto& f : tfits_) {
                    char b[300];
                    std::snprintf(b, sizeof b, "%s  AIC %.4g  RSS %.4g  Theil's U %.3f  %s", f.name.c_str(), f.aic, f.rss, f.theilU, f.formula.c_str());
                    items.push_back(b);
                }
                hw_ = mc::bestHoltWinters(data_, data_.size() >= 36 ? 12 : data_.size() >= 16 ? 4 : 1, 0);
                if (hw_.ok) {
                    char b[300];
                    std::snprintf(b, sizeof b, "Holt-Winters %s (season %d)  SSE %.4g  Theil's U %.3f  (forecasting only)",
                                  hw_.multiplicative ? "multiplicative" : "additive", hw_.season, hw_.sse, hw_.theilU);
                    items.push_back(b);
                }
            }
        } catch (const std::exception& e) {
            message(e.what(), true);
        }
        setItems("results", items, 0);
        sel_ = 0;
        simulate();
    }

    void simulate() {
        path_.clear();
        if (!tsMode_ || tfits_.empty()) { repaint(); return; }
        int h = std::max(1, int(parseOr(text("fc"), 12)));
        std::mt19937_64 rng(std::random_device{}());
        lo_.clear(); mid_.clear(); hi_.clear();
        if (sel_ < int(tfits_.size())) {
            auto ts = mc::anchorAt(tfits_[sel_].ts, data_.back());
            path_ = ts.path(h, rng);
            lo_ = mc::forecastTimeSeries(ts, data_, h, 0.025, 1000, rng);
            mid_ = mc::forecastTimeSeries(ts, data_, h, 0.5, 1000, rng);
            hi_ = mc::forecastTimeSeries(ts, data_, h, 0.975, 1000, rng);
        } else if (hw_.ok) {
            mid_ = hw_.forecast(h);
            lo_ = hw_.forecastPercentile(h, 0.025, 1000, rng);
            hi_ = hw_.forecastPercentile(h, 0.975, 1000, rng);
        }
        repaint();
    }

    void paint(Painter& p, int w, int h) override {
        p.rect(0, 0, w, h, &col::white, nullptr);
        int statsW = 180;
        if (!tsMode_) {
            HistogramOptions o;
            o.title = "Historical data (" + std::to_string(data_.size()) + " values)";
            o.bins = std::max(5, int(std::sqrt(double(data_.size()))));
            o.color = col::input.mix(col::white, 0.35);
            bool qq = false;
            try { qq = checked("qq"); } catch (...) {}
            if (qq && sel_ >= 0 && sel_ < int(dfits_.size()) && dfits_[sel_].ok) {
                // Q-Q plot: sorted data against the quantiles of the fitted distribution
                auto d = mc::makeFitted(dfits_[sel_]);
                Vec srt = mc::stats::sorted(data_), th;
                for (size_t i = 0; i < srt.size(); ++i) th.push_back(d->quantile((i + 0.5) / srt.size()));
                double lo = std::min(srt.front(), th.front()), hi = std::max(srt.back(), th.back());
                Plot pl = axes(p, 0, 0, w - statsW, h, lo, hi, lo, hi, "Q-Q plot: " + dfits_[sel_].formula + " (x: fitted, y: data)");
                p.line(pl.px(lo), pl.py(lo), pl.px(hi), pl.py(hi), col::output, 2);
                for (size_t i = 0; i < srt.size(); ++i)
                    if (std::isfinite(th[i])) p.ellipse(pl.px(th[i]) - 3, pl.py(srt[i]) - 3, 6, 6, &col::input, nullptr);
                return;
            }
            Plot pl = drawHistogram(p, 0, 0, w - statsW, h, data_, o);
            if (sel_ >= 0 && sel_ < int(dfits_.size()) && dfits_[sel_].ok) {
                auto d = mc::makeFitted(dfits_[sel_]);
                double lo = pl.x0, hi = pl.x1, bw = (hi - lo) / o.bins;
                std::vector<int> xs, ys;
                for (int i = 0; i <= 120; ++i) {
                    double xv = lo + (hi - lo) * i / 120.0;
                    double share = d->cdf(xv + bw / 2) - d->cdf(xv - bw / 2);
                    xs.push_back(pl.px(xv));
                    ys.push_back(pl.py(std::min(pl.y1, share)));
                }
                p.polyline(xs, ys, col::output, 2);
                p.text(pl.x + 6, pl.y + 4, dfits_[sel_].formula, col::output, true);
            }
            std::vector<std::vector<std::string>> rows{{"Data", "Value"}};
            rows.push_back({"Mean", niceNum(mc::stats::mean(data_))});
            rows.push_back({"Minimum", niceNum(mc::stats::min(data_))});
            rows.push_back({"Maximum", niceNum(mc::stats::max(data_))});
            rows.push_back({"Variance", niceNum(mc::stats::variance(data_))});
            rows.push_back({"Range", niceNum(mc::stats::max(data_) - mc::stats::min(data_))});
            rows.push_back({"Mode", niceNum(mc::stats::mode(data_, o.bins))});
            rows.push_back({"SDeviation", niceNum(mc::stats::stdev(data_))});
            for (double q : {0.05, 0.25, 0.5, 0.75, 0.95})
                rows.push_back({"P" + std::to_string(int(q * 100)), niceNum(mc::stats::percentile(data_, q))});
            rows.push_back({"Number of iterations", std::to_string(data_.size())});
            drawTable(p, w - statsW + 4, 6, statsW - 8, rows, {105, 67});
        } else {
            size_t n = data_.size(), h2 = mid_.size();
            Vec hist = data_, fitted, fc(n + h2, std::nan("")), lo(n + h2, std::nan("")), hi(n + h2, std::nan("")), sim(n + h2, std::nan(""));
            hist.resize(n + h2, std::nan(""));
            if (sel_ < int(tfits_.size())) fitted = tfits_[sel_].fitted;
            else if (hw_.ok) fitted = hw_.fitted;
            fitted.resize(n + h2, std::nan(""));
            for (size_t i = 0; i < h2; ++i) {
                fc[n + i] = mid_[i];
                lo[n + i] = i < lo_.size() ? lo_[i] : std::nan("");
                hi[n + i] = i < hi_.size() ? hi_[i] : std::nan("");
                if (i < path_.size()) sim[n + i] = path_[i];
            }
            fc[n - 1] = lo[n - 1] = hi[n - 1] = sim[n - 1] = data_.back();
            std::string title = sel_ < int(tfits_.size()) ? tfits_[sel_].formula : "Holt-Winters";
            drawLineChart(p, 0, 0, w, h, title, {{"Real data", hist}, {"Estimated", fitted}, {"Forecast P50", fc}, {"P2.5", lo}, {"P97.5", hi}, {"Simulated path", sim}},
                          {col::black, col::input, col::output, col::grid.mix(col::output, 0.5), col::grid.mix(col::output, 0.5), col::stripe}, int(n));
        }
    }

    void itemChanged(const std::string& n) override {
        if (n == "results") { sel_ = std::max(0, selected("results")); simulate(); }
        if (n == "mode") { dfits_.clear(); tfits_.clear(); setItems("results", {}); tsMode_ = selected("mode") == 1; repaint(); }
        if (n == "qq") repaint();
    }

    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "fit") { fit(); return; }
        if (b == "simulate") { simulate(); return; }
        if (b == "insert") {
            std::string f;
            if (!tsMode_ && sel_ < int(dfits_.size()) && dfits_[sel_].ok) f = dfits_[sel_].formula;
            else if (tsMode_ && sel_ < int(tfits_.size())) f = tfits_[sel_].formula;
            if (f.empty()) { message("Fit the data and select a formula first (Holt-Winters models cannot be used as variables).", true); return; }
            std::string name = mc::formula::sanitizeName(text("name"));
            // MC FLO order: parameters; Variable_name; Number (= forecast periods for time series)
            std::string tail = ";" + (text("name").empty() ? std::string() : quote(name));
            if (tsMode_ && !text("fc").empty()) tail += ";" + text("fc");
            if (tail != ";") f.insert(f.size() - 1, tail);
            insertIntoCurrentCell(frame_, f, text("name").empty() ? "" : name, mc::VarKind::Input);
            return;
        }
        if (b == "copy") {
            ModelDoc d = docOf(frame_);
            int sh = d.ensureSheet("FLO_Fit");
            std::vector<std::vector<Any>> rows;
            if (!tsMode_) {
                rows.push_back({Any(OUString("Distribution")), Any(OUString("RSS")), Any(OUString("KS statistic")), Any(OUString("KS p-value")),
                                Any(OUString("AIC")), Any(OUString("Formula"))});
                for (auto& f : dfits_)
                    if (f.ok) rows.push_back({Any(ou(f.name)), Any(f.rss), Any(f.ks), Any(f.ksPValue), Any(f.aic), Any(ou(f.formula.substr(1)))});
            } else {
                rows.push_back({Any(OUString("Process")), Any(OUString("AIC")), Any(OUString("RSS")), Any(OUString("Theil's U")), Any(OUString("")),
                                Any(OUString("Formula"))});
                for (auto& f : tfits_)
                    rows.push_back({Any(ou(f.name)), Any(f.aic), Any(f.rss), Any(f.theilU), Any(OUString("")), Any(ou(f.formula.substr(1)))});
            }
            d.writeTable(sh, 0, 0, rows);
            message("The results were written to the sheet FLO_Fit (copy any formula into the model).");
        }
    }

private:
    Vec data_;
    std::string name_;
    bool tsMode_ = false;
    std::vector<mc::DistFit> dfits_;
    std::vector<mc::TsFit> tfits_;
    mc::HoltWinters hw_;
    int sel_ = 0;
    Vec path_, lo_, mid_, hi_;
};

void openFitWindow(const Ctx& ctx, const Frame& frame) {
    try {
        auto w = std::make_shared<FitWindow>(ctx, frame);
        w->show();
        CanvasWindow::keep(w);
    } catch (const std::exception& e) {
        showMessage(ctx, frame, "MonteCalc - Fit distribution", e.what(), true);
    }
}

// ============================================================ correlogram

class CorrelogramWindow : public CanvasWindow {
public:
    CorrelogramWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - Correlogram", 420, 300, 70) {
        std::vector<std::string> labels;
        auto rows = docOf(frame).selectionValues(&labels);
        if (rows.empty()) throw std::runtime_error("select the data (one column per variable) first");
        size_t nc = rows[0].size();
        cols_.assign(nc, Vec());
        for (auto& r : rows)
            for (size_t j = 0; j < nc && j < r.size(); ++j) cols_[j].push_back(r[j]);
        for (size_t j = 0; j < nc; ++j) names_.push_back(j < labels.size() ? labels[j] : "Column " + std::to_string(j + 1));
        if (nc < 2 || nc > 12) throw std::runtime_error("select 2 to 12 columns");
        int x = panelX(), w = panelW();
        addButton("export", "Export image", x, 6, w);
        addButton("close", "Close", x, 22, w);
    }
    void paint(Painter& p, int w, int h) override {
        p.rect(0, 0, w, h, &col::white, nullptr);
        size_t n = cols_.size();
        int cw = (w - 10) / int(n), ch = (h - 10) / int(n);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j) {
                int x = 5 + int(j) * cw, y = 5 + int(i) * ch;
                if (i == j) {
                    HistogramOptions o;
                    o.title = names_[i];
                    o.bins = 12;
                    drawHistogram(p, x, y, cw - 4, ch - 4, cols_[i], o);
                } else if (j > i) {
                    drawScatter(p, x, y, cw - 4, ch - 4, cols_[j], cols_[i], names_[j], names_[i], false);
                } else {
                    double r = mc::stats::spearman(cols_[i], cols_[j]);
                    Color c = col::white.mix(r >= 0 ? col::green : col::red, std::min(1.0, std::fabs(std::isfinite(r) ? r : 0)) * 0.7);
                    p.rect(x, y, cw - 4, ch - 4, &c, &col::grid);
                    char b[32];
                    std::snprintf(b, sizeof b, "r = %.2f", std::isfinite(r) ? r : 0.0);
                    p.text(x + (cw - 4 - p.textWidth(b)) / 2, y + (ch - 4) / 2 - 6, b, col::text, true);
                }
            }
    }
    void button(const std::string& b) override {
        if (b == "close") close();
        if (b == "export") {
            std::string path = homeDir() + "/montecalc_correlogram.png";
            int w, h;
            canvasSize(w, h);
            if (exportImage(fileUrl(path), "image/png", w, h, 2.0)) message("Saved " + path);
        }
    }

private:
    std::vector<Vec> cols_;
    std::vector<std::string> names_;
};

void openCorrelogram(const Ctx& ctx, const Frame& frame) {
    try {
        auto w = std::make_shared<CorrelogramWindow>(ctx, frame);
        w->show();
        CanvasWindow::keep(w);
    } catch (const std::exception& e) {
        showMessage(ctx, frame, "MonteCalc - Correlogram", e.what(), true);
    }
}

// ============================================================ variable list ("Show")

class VariableListWindow : public CanvasWindow {
public:
    VariableListWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - Show variables", 440, 240, 90) {
        int x = panelX(), w = panelW(), y = 6;
        addLabel("lcat", "Category", x, y, w); y += 10;
        addList("cat", {"All", "Inputs", "Correlations", "Outputs"}, x, y, w, 12, true); y += 16;
        addButton("goto", "Go to cell", x, y, w); y += 16;
        addButton("details", "Details / preview", x, y, w); y += 16;
        addButton("reload", "Reload", x, y, w); y += 16;
        addButton("close", "Close", x, y, w);
        reload();
    }
    void reload() {
        ModelDoc d = docOf(frame_);
        all_ = d.scan();
        repaint();
    }
    std::vector<VarCell> shown() {
        int c = 0;
        try { c = selected("cat"); } catch (...) {}
        std::vector<VarCell> r;
        for (auto& v : all_)
            if (c <= 0 || (c == 1 && v.kind == mc::VarKind::Input) || (c == 2 && v.kind == mc::VarKind::Correlation) || (c == 3 && v.kind == mc::VarKind::Output))
                r.push_back(v);
        return r;
    }
    void paint(Painter& p, int w, int h) override {
        p.rect(0, 0, w, h, &col::white, nullptr);
        ModelDoc d = docOf(frame_);
        std::vector<std::vector<std::string>> rows{{"Name", "Kind", "Cell", "Definition", "Value"}};
        std::vector<Color> colors{col::white};
        for (auto& v : shown()) {
            std::string kind = v.kind == mc::VarKind::Input ? "input" : v.kind == mc::VarKind::Output ? "output" : "correlation";
            std::string def = v.description.size() > 48 ? v.description.substr(0, 48) + "..." : v.description;
            bool locked = v.kind == mc::VarKind::Input && mc::Simulation::instance().isLocked(v.name);
            rows.push_back({v.name + (v.named ? "" : " (unnamed)") + (locked ? " [locked]" : ""), kind, d.cellName(v.addr, true), def,
                            niceNum(d.cell(v.addr).value())});
            colors.push_back(v.kind == mc::VarKind::Input ? Color{221, 235, 247} : v.kind == mc::VarKind::Output ? Color{252, 228, 214} : Color{255, 242, 204});
        }
        const int font = 11;   // larger than the other tables: this list is read, not glanced at
        rowH_ = tableRowHeight(p, font);
        drawTable(p, 6, 6, w - 12, rows, {(w - 12) * 22 / 100, (w - 12) * 12 / 100, (w - 12) * 16 / 100, (w - 12) * 38 / 100, (w - 12) * 12 / 100},
                  &colors, sel_ + 1, font);
        p.text(6, h - p.textHeight(10) - 8, "Click a row to select the cell; double-click for details (result window or preview).", col::muted, false, 10);
    }
    void mouseDown(const MouseInfo& m) override {
        int row = (m.y - 6) / std::max(1, rowH_) - 1;
        auto s = shown();
        if (row < 0 || row >= int(s.size())) return;
        sel_ = row;
        docOf(frame_).select(s[row].addr);
        repaint();
        if (m.clicks >= 2) button("details");
    }
    void itemChanged(const std::string&) override { sel_ = -1; repaint(); }
    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "reload") { reload(); return; }
        auto s = shown();
        if (sel_ < 0 || sel_ >= int(s.size())) { message("Select a variable in the list.", true); return; }
        const auto& v = s[sel_];
        if (b == "goto") { docOf(frame_).select(v.addr); return; }
        if (b == "details") {
            if (v.kind != mc::VarKind::Correlation && hasResults(v.name)) { openResultWindow(ctx_, frame_, v.name); return; }
            if (v.kind == mc::VarKind::Correlation) { openCorrelationScatter(ctx_, frame_, v); return; }
            Vec data = previewSamples(docOf(frame_), v.addr, 5000);
            if (data.empty()) { message("The variable could not be previewed.", true); return; }
            auto w = std::make_shared<ResultWindow>(ctx_, frame_, v.name, v.name, data);
            w->show();
            w->afterShow();
            CanvasWindow::keep(w);
        }
    }

private:
    std::vector<VarCell> all_;
    int sel_ = -1, rowH_ = 18;
};

void openVariableList(const Ctx& ctx, const Frame& frame) {
    auto w = std::make_shared<VariableListWindow>(ctx, frame);
    w->show();
    CanvasWindow::keep(w);
}

// ============================================================ context sheet / text

void writeContextSheet(const Ctx& ctx, const Frame& frame) {
    (void)ctx;
    ModelDoc d = docOf(frame);
    ModelGraph g = buildGraph(d);
    auto& sim = mc::Simulation::instance();
    d.removeSheet("FLO_Context");
    int sh = d.ensureSheet("FLO_Context");
    std::vector<std::vector<Any>> rows;
    auto t = [](const std::string& s) { return Any(ou(s)); };
    auto n = [](double v) { return std::isfinite(v) ? Any(v) : Any(OUString()); };
    rows.push_back({t("MonteCalc model context. Naming: FLO_i_ = input (uncertain variable), FLO_c_ = relationship (correlation), FLO_o_ = output.")});
    rows.push_back({t("Statistics come from the last simulation run (" + std::to_string(sim.iterations()) + " iterations).")});
    rows.push_back({});
    rows.push_back({t("Variable"), t("Kind"), t("Cell"), t("Value"), t("Mean"), t("StdDev"), t("P5"), t("P95"), t("Min"), t("Max"),
                    t("Correlation r"), t("Correlated With"), t("Depends On")});
    for (size_t i = 0; i < g.vars.size(); ++i) {
        const auto& v = g.vars[i];
        std::string kind = v.kind == mc::VarKind::Input ? "input" : v.kind == mc::VarKind::Output ? "output" : "relationship";
        Vec s;
        bool has = v.kind != mc::VarKind::Correlation && sim.samples(v.name, s);
        double r = std::nan("");
        std::string with;
        if (v.kind == mc::VarKind::Correlation) {
            r = g.numValues[i];
            with = v.corrA + ", " + v.corrB;
        } else {
            for (size_t k = 0; k < g.vars.size(); ++k)
                if (g.vars[k].kind == mc::VarKind::Correlation && (mc::lower(g.vars[k].corrA) == mc::lower(v.name) || mc::lower(g.vars[k].corrB) == mc::lower(v.name))) {
                    with += (with.empty() ? "" : ", ") + (mc::lower(g.vars[k].corrA) == mc::lower(v.name) ? g.vars[k].corrB : g.vars[k].corrA);
                    r = g.numValues[k];
                }
        }
        std::string deps;
        for (int j : g.dependsOn[i]) deps += (deps.empty() ? "" : ", ") + g.vars[j].name;
        rows.push_back({t(v.name), t(kind), t(d.cellName(v.addr)), n(g.numValues[i]), n(has ? mc::stats::mean(s) : std::nan("")),
                        n(has ? mc::stats::stdev(s) : std::nan("")), n(has ? mc::stats::percentile(s, 0.05) : std::nan("")),
                        n(has ? mc::stats::percentile(s, 0.95) : std::nan("")), n(has ? mc::stats::min(s) : std::nan("")),
                        n(has ? mc::stats::max(s) : std::nan("")), n(r), t(with), t(deps)});
    }
    d.writeTable(sh, 0, 0, rows);
    d.optimalWidth(sh, 0, 13);
}

std::string modelContextText(const Frame& frame) {
    ModelDoc d = docOf(frame);
    ModelGraph g = buildGraph(d);
    auto& sim = mc::Simulation::instance();
    std::ostringstream os;
    os << "MonteCalc spreadsheet model (MC FLO conventions: FLO_i_ inputs, FLO_c_ relationships/correlations, FLO_o_ outputs).\n";
    os << "Last simulation: " << sim.iterations() << " iterations.\n\n";
    for (size_t i = 0; i < g.vars.size(); ++i) {
        const auto& v = g.vars[i];
        std::string kind = v.kind == mc::VarKind::Input ? "input" : v.kind == mc::VarKind::Output ? "output" : "correlation";
        os << "- " << v.name << " [" << kind << ", cell " << d.cellName(v.addr) << "]: " << v.description;
        if (v.kind == mc::VarKind::Correlation) os << " coefficient " << niceNum(g.numValues[i]) << " between " << v.corrA << " and " << v.corrB;
        if (!g.dependsOn[i].empty()) {
            os << "; depends on ";
            bool first = true;
            for (int j : g.dependsOn[i]) { os << (first ? "" : ", ") << g.vars[j].name; first = false; }
        }
        Vec s;
        if (v.kind != mc::VarKind::Correlation && sim.samples(v.name, s)) {
            os << "; simulated mean " << niceNum(mc::stats::mean(s)) << ", sd " << niceNum(mc::stats::stdev(s)) << ", P5 "
               << niceNum(mc::stats::percentile(s, 0.05)) << ", P50 " << niceNum(mc::stats::percentile(s, 0.5)) << ", P95 "
               << niceNum(mc::stats::percentile(s, 0.95)) << ", min " << niceNum(mc::stats::min(s)) << ", max " << niceNum(mc::stats::max(s))
               << ", skewness " << niceNum(mc::stats::skewness(s));
            // compressed distribution (as in MC FLO: histograms instead of the iterations)
            auto hg = mc::stats::histogram(s, 10);
            if (!hg.count.empty()) {
                os << "; histogram " << niceNum(hg.lo.front()) << ".." << niceNum(hg.hi.back()) << " in 10 bins: ";
                for (size_t k = 0; k < hg.count.size(); ++k) os << (k ? " " : "") << int(hg.count[k]);
            }
        }
        os << "\n";
    }
    // drivers of every output
    for (const auto& out : sim.resultNames(mc::VarKind::Output)) {
        Vec o;
        sim.samples(out, o);
        std::vector<std::pair<std::string, double>> dr;
        for (const auto& in : sim.resultNames(mc::VarKind::Input)) {
            Vec x;
            sim.samples(in, x);
            double r = mc::stats::spearman(x, o);
            if (std::isfinite(r)) dr.push_back({in, r});
        }
        std::stable_sort(dr.begin(), dr.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
        os << "\nDrivers of " << out << " (Spearman rank correlation):";
        for (size_t k = 0; k < dr.size() && k < 10; ++k) os << " " << dr[k].first << " " << niceNum(dr[k].second) << ";";
        os << "\n";
    }
    return os.str();
}

} // namespace montecalc

// ============================================================ Insert correlation (graphical)

namespace montecalc {
namespace {

const std::vector<std::string>& copulaTypeNames() {
    static const std::vector<std::string> t = [] {
        std::vector<std::string> v;   // MC FLO type codes (wiki "Korrelationen")
        for (int i = 0; i <= mc::kMaxCopulaType; ++i) v.push_back(std::to_string(i) + " - " + mc::copulaMcfloName(i));
        return v;
    }();
    return t;
}

// parses =FLOsimula_correlation("a";"b";r;type) (also with , separators; type = MC FLO code)
bool parseCorrelationFormula(const std::string& f, std::string& a, std::string& b, double& r, int& type) {
    std::string u = f;
    for (auto& c : u) c = char(std::toupper((unsigned char)c));
    size_t p = u.find("FLOSIMULA_CORRELATION(");
    if (p == std::string::npos) return false;
    size_t s = p + 22, e = f.rfind(')');
    if (e == std::string::npos || e < s) return false;
    std::vector<std::string> args;
    std::string cur;
    bool q = false;
    for (size_t i = s; i < e; ++i) {
        char c = f[i];
        if (c == '"') { q = !q; continue; }
        if (!q && (c == ';' || c == ',')) { args.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    args.push_back(cur);
    if (args.size() < 3) return false;
    a = args[0]; b = args[1];
    bool ok;
    r = parseNumber(args[2], ok);
    if (!ok) return false;
    type = args.size() > 3 ? std::atoi(args[3].c_str()) : 0;
    return true;
}

} // namespace

class CorrelationWindow : public CanvasWindow {
public:
    CorrelationWindow(const Ctx& ctx, const Frame& frame)
        : CanvasWindow(ctx, frame, "MonteCalc - Insert correlation into " + targetName(frame), 430, 250, 132) {
        ModelDoc d = docOf(frame);
        if (!d.currentCell(target_)) throw std::runtime_error("select the cell for the correlation formula first");
        for (auto& v : d.scan()) {
            if (v.kind == mc::VarKind::Input && !v.name.empty()) {
                if (std::find(names_.begin(), names_.end(), v.name) == names_.end()) { names_.push_back(v.name); cells_.push_back(v.addr); }
            } else if (v.kind == mc::VarKind::Correlation && !(v.addr.Sheet == target_.Sheet && v.addr.Column == target_.Column && v.addr.Row == target_.Row)) {
                std::string a, b; double r; int t;
                if (parseCorrelationFormula(v.formula, a, b, r, t)) others_.push_back({a, b, r, t});
            }
        }
        if (names_.size() < 2) throw std::runtime_error("at least two named input variables are needed (define inputs, then use Register variables)");
        // editing an existing correlation: take over its values
        std::string fa, fb; double fr = 0.5; int ft = 0;
        bool existing = parseCorrelationFormula(u8(d.cell(target_).formula()), fa, fb, fr, ft);
        int i1 = existing ? indexOf(fa, 0) : 0, i2 = existing ? indexOf(fb, 1) : 1;
        if (!existing) fr = 0.5;
        int x = panelX(), w = panelW(), y = 6;
        addLabel("lv1", "Variable 1:", x, y + 2, 40); addList("v1", names_, x + 42, y, w - 42, 12, true); y += 15;
        addLabel("lv2", "Variable 2:", x, y + 2, 40); addList("v2", names_, x + 42, y, w - 42, 12, true); y += 20;
        addLabel("lr", "Coefficient of correlation", x, y + 2, 80); addEdit("r", fmtNum(fr), x + 82, y, w - 82); y += 13;
        addLabel("rank", "", x + 82, y, w - 82); y += 16;
        addLabel("lt", "Correlation type:", x, y + 2, 60); addList("type", copulaTypeNames(), x + 62, y, w - 62, 12, true); y += 20;
        addSlider("slider", x, y, w, -99, 99, int(std::lround(fr * 100))); y += 13;
        addLabel("ticks", "-1                      0                      1", x, y, w); y += 14;
        addCheck("ranks", "Show ranks (copula)", x, y, w, false); y += 15;
        addLabel("status", "", x, y, w, 30); y += 34;
        addButton("ok", "OK", x, y, w, 16); y += 20;
        addButton("cancel", "Cancel", x, y, w, 16);
        watchText("r");
        init1_ = i1; init2_ = i2; initType_ = std::max(0, std::min(mc::kMaxCopulaType, ft));
    }
    void afterShow() {
        select("v1", init1_);
        select("v2", init2_ == init1_ ? (init1_ + 1) % int(names_.size()) : init2_);
        select("type", initType_);
        setTextColor("rank", 0x808080);
        ready_ = true;
        update();
    }

    void itemChanged(const std::string& name) override {
        if (!ready_ || syncing_) return;
        if (name == "slider") {
            syncing_ = true;
            setText("r", fmtNum(sliderValue("slider") / 100.0));
            syncing_ = false;
        } else if (name == "r") {
            bool ok;
            double r = parseNumber(text("r"), ok);
            if (ok && r > -1 && r < 1) { syncing_ = true; setSliderValue("slider", int(std::lround(r * 100))); syncing_ = false; }
        }
        update();
    }

    void button(const std::string& name) override {
        if (name == "cancel") { close(); return; }
        if (name == "ok") {
            std::string err = validate();
            if (!err.empty() && err.rfind("Note", 0) != 0) { message(err, true); return; }
            insert();
            close();
        }
    }

    // non-interactive use (dispatch arguments / tests)
    void preset(const std::string& a, const std::string& b, double r, int type) {
        select("v1", indexOf(a, 0));
        select("v2", indexOf(b, 1));
        select("type", std::max(0, std::min(mc::kMaxCopulaType, type)));
        setText("r", fmtNum(r));
        update();
    }
    // "" = fine, "Note: ..." = allowed with a remark, anything else = not allowed
    std::string validate() {
        int i1 = selected("v1"), i2 = selected("v2");
        if (i1 < 0 || i2 < 0) return "Choose two input variables.";
        if (i1 == i2) return "Choose two different variables.";
        bool ok;
        double r = parseNumber(text("r"), ok);
        if (!ok || !(r > -1 && r < 1)) return "The correlation coefficient must be between -1 and 1 (exclusive).";
        int type = std::max(0, selected("type"));
        const std::string &a = names_[i1], &b = names_[i2];
        bool archNew = mc::copulaIsBivariateOnly(type);
        for (auto& o : others_) {
            bool touches = o.a == a || o.b == a || o.a == b || o.b == b;
            if ((o.a == a && o.b == b) || (o.a == b && o.b == a))
                return "A correlation between " + a + " and " + b + " is already defined in another cell.";
            if (archNew && touches) return "Archimedean / asymmetric copulas are bivariate: " + (o.a == a || o.b == a ? a : b) + " is already correlated with another variable.";
            if (mc::copulaIsBivariateOnly(o.type) && touches) return "The variable is already part of an Archimedean / asymmetric correlation (bivariate only).";
        }
        if (archNew)
            for (auto& o : others_)
                if (!mc::copulaIsBivariateOnly(o.type)) return "Note: with an Archimedean copula all correlations of the model must be bivariate.";
        return "";
    }
    void insert() {
        ModelDoc d = docOf(frame_);
        const std::string &a = names_[selected("v1")], &b = names_[selected("v2")];
        bool ok;
        double r = parseNumber(text("r"), ok);
        int type = std::max(0, selected("type"));
        d.cell(target_).setFormula(ou("=FLOsimula_correlation(" + quote(a) + ";" + quote(b) + ";" + fmtNum(r) + ";" + std::to_string(type) + ")"));
        d.removeNamedRangesWithPrefix("FLO_c_" + mc::formula::sanitizeName(a + "_" + b));
        d.setNamedRange("FLO_c_" + mc::formula::sanitizeName(a + "_" + b), target_);
        d.setBackground(target_, kColorCorrelation);
        d.select(target_);
    }

    void paint(Painter& p, int w, int h) override {
        Color bg = col::white;
        p.rect(0, 0, w, h, &bg, nullptr);
        if (xs_.empty()) {
            p.text(12, 12, why_.empty() ? "Choose two input variables." : why_, col::muted);
            return;
        }
        bool ranks = checked("ranks");
        const Vec& X = ranks ? u1_ : xs_;
        const Vec& Y = ranks ? u2_ : ys_;
        double x0, x1, y0, y1;
        auto mm = [](const Vec& v, double& lo, double& hi) {
            lo = *std::min_element(v.begin(), v.end()); hi = *std::max_element(v.begin(), v.end());
            if (hi <= lo) { lo -= 0.5; hi += 0.5; }
            double pad = (hi - lo) * 0.03; lo -= pad; hi += pad;
        };
        mm(X, x0, x1); mm(Y, y0, y1);
        Plot pl = axes(p, 6, 4, w - 12, h - 10, x0, x1, y0, y1, names_[sel1_] + " (x) / " + names_[sel2_] + " (y)");
        // colours as in MC FLO: outside the 90% band of both variables in the direction of the
        // correlation green (upper) / red (lower), one variable outside black, else blue
        bool ok;
        double rr = parseNumber(text("r"), ok);
        bool neg = ok && rr < 0;
        Color blue = Color{52, 120, 200}, red = col::red, green = col::green, black = Color{30, 30, 30};
        for (size_t i = 0; i < X.size(); ++i) {
            bool hx = u1_[i] > 0.95, lx = u1_[i] < 0.05, hy = u2_[i] > 0.95, ly = u2_[i] < 0.05;
            const Color* c = &blue;
            int sz = 3;
            if (hx && (neg ? ly : hy)) { c = &green; sz = 5; }
            else if (lx && (neg ? hy : ly)) { c = &red; sz = 5; }
            else if (hx || lx || hy || ly) c = &black;
            p.rect(pl.px(X[i]) - sz / 2, pl.py(Y[i]) - sz / 2, sz, sz, c, nullptr);
        }
        p.text(pl.x + pl.w - p.textWidth(names_[sel1_]) - 4, pl.y + pl.h - p.textHeight() - 2, names_[sel1_], col::muted);
    }

    std::string exportTo;

private:
    struct Corr { std::string a, b; double r; int type; };
    table::CellAddress target_{};
    std::vector<std::string> names_;
    std::vector<table::CellAddress> cells_;
    std::vector<Corr> others_;
    std::map<std::string, Vec> marg_;   // sorted preview samples per variable
    Vec xs_, ys_, u1_, u2_;
    std::string why_;
    int sel1_ = 0, sel2_ = 1, init1_ = 0, init2_ = 1, initType_ = 0;
    bool ready_ = false, syncing_ = false;

    static std::string targetName(const Frame& frame) {
        ModelDoc d = docOf(frame);
        table::CellAddress a;
        return d.currentCell(a) ? d.cellName(a, false) : std::string();
    }
    int indexOf(const std::string& n, int def) const {
        for (size_t i = 0; i < names_.size(); ++i)
            if (mc::formula::sanitizeName(names_[i]) == mc::formula::sanitizeName(n)) return int(i);
        return def;
    }
    const Vec& marginal(int i) {
        auto it = marg_.find(names_[i]);
        if (it != marg_.end()) return it->second;
        Vec v = previewSamples(docOf(frame_), cells_[i], 2000);
        std::sort(v.begin(), v.end());
        return marg_[names_[i]] = v;
    }
    void update() {
        xs_.clear(); ys_.clear(); u1_.clear(); u2_.clear(); why_.clear();
        sel1_ = selected("v1"); sel2_ = selected("v2");
        std::string err = validate();
        setLabel("status", err);
        setTextColor("status", err.rfind("Note", 0) == 0 ? 0x806000 : 0xC00000);
        bool ok;
        double r = parseNumber(text("r"), ok);
        if (sel1_ < 0 || sel2_ < 0 || sel1_ == sel2_ || !ok || !(r > -1 && r < 1)) {
            setLabel("rank", "");
            why_ = sel1_ == sel2_ ? "Choose two different variables." : "Enter a coefficient between -1 and 1.";
            repaint();
            return;
        }
        try {
            const Vec& m1 = marginal(sel1_);
            const Vec& m2 = marginal(sel2_);
            if (m1.empty() || m2.empty()) throw std::runtime_error("the variables could not be previewed");
            std::mt19937_64 rng(20261103);   // fixed: the picture changes only with the inputs
            auto res = mc::generateUniforms(2, 1000, {{0, 1, r, mc::copulaFromMcflo(std::max(0, selected("type")))}}, rng);
            u1_ = res.u[0]; u2_ = res.u[1];
            auto q = [](const Vec& s, double u) { size_t k = std::min(s.size() - 1, size_t(u * double(s.size()))); return s[k]; };
            for (size_t i = 0; i < u1_.size(); ++i) { xs_.push_back(q(m1, u1_[i])); ys_.push_back(q(m2, u2_[i])); }
            double rs = mc::stats::spearman(xs_, ys_);
            char t[64];
            std::snprintf(t, sizeof t, "%.2f", std::isfinite(rs) ? rs : 0.0);
            setLabel("rank", t);
        } catch (const std::exception& e) {
            why_ = e.what();
            setLabel("rank", "");
        }
        repaint();
    }
};

void openCorrelationWindow(const Ctx& ctx, const Frame& frame, const std::string& var1, const std::string& var2, double r, int type,
                           const std::string& exportTo, bool insertNow) {
    auto w = std::make_shared<CorrelationWindow>(ctx, frame);
    w->show();
    w->afterShow();
    if (!var1.empty()) w->preset(var1, var2, r, type);
    if (!exportTo.empty()) {
        int cw, ch;
        w->canvasSize(cw, ch);
        w->exportImage(exportTo, "image/png", cw, ch, 2.0);
    }
    if (insertNow) {
        std::string err = w->validate();
        if (!err.empty() && err.rfind("Note", 0) != 0) { w->close(); throw std::runtime_error(err); }
        w->insert();
        w->close();
        return;
    }
    if (!exportTo.empty()) { w->close(); return; }
    CanvasWindow::keep(w);
}

} // namespace montecalc
