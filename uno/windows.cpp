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

#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/table/XColumnRowRange.hpp>
#include <com/sun/star/text/XText.hpp>

#include <algorithm>
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
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

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
                if (c->getError() == 0) out.push_back(c->getValue());
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
    d.cell(a)->setFormula(ou(formula));
    if (!name.empty()) {
        d.setNamedRange((kind == mc::VarKind::Input ? "FLO_i_" : kind == mc::VarKind::Output ? "FLO_o_" : "FLO_c_") + mc::formula::sanitizeName(name), a);
    }
    d.setBackground(a, kind == mc::VarKind::Input ? kColorInput : kind == mc::VarKind::Output ? kColorOutput : kColorCorrelation);
}

} // namespace

// ============================================================ result window

class ResultWindow : public CanvasWindow {
public:
    enum View { Histogram, Cumulative, Tornado, Spider, SpiderCum, Scatter, Variants, Decision, Statistics };

    ResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& previewName = "", Vec previewData = {})
        : CanvasWindow(ctx, frame, previewName.empty() ? "MonteCalc - Results" : "MonteCalc - Preview " + previewName, 440, 270, 120),
          preview_(!previewName.empty()) {
        auto& sim = mc::Simulation::instance();
        if (preview_) {
            mc::Variable v;
            v.name = previewName;
            v.kind = mc::VarKind::Input;
            vars_.push_back(v);
            previewData_ = std::move(previewData);
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
        addLabel("lvar", "Variable", x, y, w); y += 10;
        addList("var", items, x, y, w - 30, 12, true);
        addButton("prev", "<", x + w - 28, y, 13, 12);
        addButton("next", ">", x + w - 14, y, 13, 12); y += 16;
        addLabel("lview", "View", x, y, w); y += 10;
        std::vector<std::string> views = {"Histogram", "Cumulative", "Tornado", "Spider", "Spider (cumulative)", "Scatter / dynamic sensitivity",
                                          "Variants", "Decision matrix", "Statistics"};
        if (preview_) views = {"Histogram", "Cumulative", "Statistics"};
        addList("view", views, x, y, w, 12, true); y += 16;
        addLabel("lbins", "Number of bins", x, y + 2, 60);
        addEdit("bins", std::to_string(bins_), x + 62, y, 26);
        addButton("applyBins", "Apply", x + 90, y, w - 90, 12); y += 16;
        addLabel("lthr", "Thresholds: lower / upper", x, y, w); y += 10;
        addEdit("lower", "", x, y, w / 2 - 14);
        addEdit("upper", "", x + w / 2 - 12, y, w / 2 - 14);
        addButton("thresholds", "Set", x + w - 24, y, 24, 12); y += 16;
        addLabel("lstripe", "Magic stripe (x, or ;p for a percentile)", x, y, w); y += 10;
        addEdit("stripe", "", x, y, w - 26);
        addButton("setStripe", "Set", x + w - 24, y, 24, 12); y += 16;
        std::vector<std::string> inputs;
        for (auto& v : vars_) if (v.kind == mc::VarKind::Input) inputs.push_back(v.name);
        addLabel("linput", "Input for scatter", x, y, w); y += 10;
        addList("input", inputs.empty() ? std::vector<std::string>{"-"} : inputs, x, y, w, 12, true); y += 16;
        addLabel("ltarget", "Target value (decision matrix)", x, y, w); y += 10;
        addEdit("target", "", x, y, w - 26);
        addButton("applyTarget", "Set", x + w - 24, y, 24, 12); y += 16;
        auto labels = sim.resultSetLabels();
        if (!preview_ && labels.size() > 1) {
            addLabel("lvariant", "Variant", x, y, w); y += 10;
            addList("variant", labels, x, y, w, 12, true); y += 16;
        }
        addButton("store", "Store results", x, y, w / 2 - 1); addButton("export", "Export image", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("comment", "Store as comment", x, y, w / 2 - 1); addButton("rerun", "Start", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("close", "Close", x, y, w);
        resetVariable();
    }

    void afterShow() {
        select("var", cur_);
        if (has("variant")) select("variant", std::max(0, mc::Simulation::instance().activeResultSet()));
    }

    Vec data(const std::string& name) {
        if (preview_) return previewData_;
        Vec v;
        mc::Simulation::instance().samples(name, v);
        return v;
    }

    void resetVariable() {
        Vec v = data(vars_[cur_].name);
        stripeX_ = mc::stats::percentile(v, 0.5);
        lower_ = mc::stats::percentile(v, 0.025);
        upper_ = mc::stats::percentile(v, 0.975);
        target_ = mc::stats::percentile(v, 0.5);
    }

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
            plot_ = drawHistogram(p, 0, 0, w, h - 46, v, o);
            // statistics strip
            std::string s1 = "Mean " + niceNum(mc::stats::mean(v)) + "   Min " + niceNum(mc::stats::min(v)) + "   Max " + niceNum(mc::stats::max(v)) +
                             "   Std dev " + niceNum(mc::stats::stdev(v)) + "   Variance " + niceNum(mc::stats::variance(v));
            std::string s2;
            for (double q : {0.05, 0.10, 0.25, 0.5, 0.75, 0.90, 0.95}) s2 += "P" + std::to_string(int(q * 100)) + " " + niceNum(mc::stats::percentile(v, q)) + "   ";
            p.text(8, h - 42, s1, col::text);
            p.text(8, h - 24, s2, col::muted);
            break;
        }
        case Tornado: {
            std::vector<std::pair<std::string, double>> t;
            for (auto& [n, x] : inputs) {
                double r = mc::stats::spearman(x, v);
                t.push_back({n, std::isfinite(r) ? r : 0});
            }
            std::stable_sort(t.begin(), t.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
            tornadoRows_.clear();
            tornadoNames_.clear();
            for (auto& x : t) tornadoNames_.push_back(x.first);
            drawTornado(p, 0, 0, w, h, "Tornado (Spearman rank correlation with " + var.name + ") - click a bar for details", t, &tornadoRows_);
            break;
        }
        case Spider:
        case SpiderCum:
            drawSpider(p, 0, 0, w, h, var.name, v, inputs, view_ == SpiderCum);
            break;
        case Scatter: {
            std::string in = selectedItemSafe("input");
            Vec x = data(in);
            if (x.empty() || in == var.name) { p.text(10, 10, "Choose an input variable for the scatter plot.", col::text); break; }
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
            drawOverlay(p, 0, 18, w, h - 18, series, bins_, true, probe_, probeOn_);
            break;
        }
        case Decision:
            paintDecision(p, w, h, var, v, inputs);
            break;
        case Statistics:
            drawStatsGrid(p, 10, 10, std::min(w - 20, 420), var.name, var.description, var.cell, v, bins_);
            break;
        }
    }

    void paintDecision(Painter& p, int w, int, const mc::Variable& var, const Vec& v, const std::vector<std::pair<std::string, Vec>>& inputs) {
        // decision matrix: target value, ambition level, probability, and the control effect of every input
        double prob = 0, n = 0;
        for (double x : v) if (std::isfinite(x)) { n += 1; prob += x >= target_; }
        prob = n > 0 ? prob / n : 0;
        p.text(10, 8, "Decision matrix for " + var.name, col::text, true);
        p.text(10, 28, "Target value " + niceNum(target_) + "   ambition level " + pctText(1 - prob) + "   probability to reach the target " + pctText(prob),
               col::text);
        std::vector<std::vector<std::string>> rows{{"Input", "Rank corr.", "P(target) favourable", "P(target) unfavourable", "Control effect"}};
        struct R { std::string name; double r, fav, unf, eff; };
        std::vector<R> rs;
        for (auto& [name, x] : inputs) {
            double r = mc::stats::spearman(x, v);
            if (!std::isfinite(r)) r = 0;
            double med = mc::stats::percentile(x, 0.5);
            double hf = 0, nf = 0, hu = 0, nu = 0;
            for (size_t i = 0; i < x.size() && i < v.size(); ++i) {
                if (!std::isfinite(x[i]) || !std::isfinite(v[i])) continue;
                bool favourable = r >= 0 ? x[i] >= med : x[i] <= med;
                (favourable ? nf : nu) += 1;
                (favourable ? hf : hu) += v[i] >= target_;
            }
            double fav = nf ? hf / nf : 0, unf = nu ? hu / nu : 0;
            rs.push_back({name, r, fav, unf, fav - unf});
        }
        std::stable_sort(rs.begin(), rs.end(), [](const R& a, const R& b) { return a.eff > b.eff; });
        std::vector<Color> colors{col::white};
        for (auto& r : rs) {
            char b[16];
            std::snprintf(b, sizeof b, "%+.2f", r.r);
            rows.push_back({r.name, b, pctText(r.fav), pctText(r.unf), pctText(r.eff)});
            colors.push_back(col::white.mix(col::green, std::min(1.0, std::max(0.0, r.eff)) * 0.8));
        }
        int tw = std::min(w - 20, 620);
        drawTable(p, 10, 50, tw, rows, {tw * 30 / 100, tw * 14 / 100, tw * 20 / 100, tw * 20 / 100, tw * 16 / 100}, &colors);
        if (!rs.empty())
            p.text(10, 56 + (p.textHeight() + 6) * int(rows.size()), "Most urgent control effect: " + rs[0].name, col::text, true);
    }

    std::string selectedItemSafe(const std::string& n) {
        try { return selectedItem(n); } catch (...) { return {}; }
    }

    void mouseDown(const MouseInfo& m) override {
        if (view_ == Histogram || view_ == Cumulative) {
            if (m.x >= plot_.x && m.x <= plot_.x + plot_.w) { stripeX_ = plot_.vx(m.x); repaint(); }
        } else if (view_ == Tornado) {
            for (size_t i = 0; i < tornadoRows_.size(); ++i)
                if (m.y >= tornadoRows_[i].first && m.y < tornadoRows_[i].second) {
                    auto inputs = itemsOf("input");
                    for (size_t k = 0; k < inputs.size(); ++k)
                        if (inputs[k] == tornadoNames_[i]) select("input", int(k));
                    setView(Scatter);
                }
        }
    }
    void mouseMove(const MouseInfo& m, bool dragging) override {
        if (dragging && (view_ == Histogram || view_ == Cumulative) && m.x >= plot_.x && m.x <= plot_.x + plot_.w) {
            stripeX_ = plot_.vx(m.x);
            repaint();
        }
        if (view_ == Variants) { probe_ = m.x; probeOn_ = true; repaint(); }
    }
    std::vector<std::string> itemsOf(const std::string&) {
        std::vector<std::string> r;
        for (auto& v : vars_) if (v.kind == mc::VarKind::Input) r.push_back(v.name);
        return r;
    }
    void setView(View v) {
        view_ = v;
        std::vector<View> map = preview_ ? std::vector<View>{Histogram, Cumulative, Statistics}
                                         : std::vector<View>{Histogram, Cumulative, Tornado, Spider, SpiderCum, Scatter, Variants, Decision, Statistics};
        for (size_t i = 0; i < map.size(); ++i) if (map[i] == v) select("view", int(i));
        repaint();
    }
    bool key(int code, int, bool, bool) override {
        if (code == awt::Key::LEFT || code == awt::Key::RIGHT) {
            cur_ = (cur_ + (code == awt::Key::RIGHT ? 1 : int(vars_.size()) - 1)) % int(vars_.size());
            select("var", cur_);
            resetVariable();
            repaint();
            return true;
        }
        return false;
    }
    void itemChanged(const std::string& name) override {
        if (name == "var") { cur_ = std::max(0, selected("var")); resetVariable(); }
        else if (name == "view") {
            int s = selected("view");
            std::vector<View> map = preview_ ? std::vector<View>{Histogram, Cumulative, Statistics}
                                             : std::vector<View>{Histogram, Cumulative, Tornado, Spider, SpiderCum, Scatter, Variants, Decision, Statistics};
            if (s >= 0 && s < int(map.size())) view_ = map[s];
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
            RunOptions o = loadOptions(frame_->getController()->getModel());
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
    Vec previewData_;
    std::vector<mc::Variable> vars_;
    int cur_ = 0;
    View view_ = Histogram;
    int bins_ = 20;
    double stripeX_ = 0, lower_ = 0, upper_ = 0, target_ = 0;
    Plot plot_{0, 0, 1, 1, 0, 1, 0, 1};
    std::vector<std::pair<int, int>> tornadoRows_;
    std::vector<std::string> tornadoNames_;
    int probe_ = 0;
    bool probeOn_ = false;
};

void openResultWindow(const Ctx& ctx, const Frame& frame, const std::string& variable, const std::string& view, const std::string& exportTo) {
    auto w = std::make_shared<ResultWindow>(ctx, frame, variable);
    w->show();
    w->afterShow();
    static const std::map<std::string, ResultWindow::View> views = {
        {"Histogram", ResultWindow::Histogram}, {"Cumulative", ResultWindow::Cumulative}, {"Tornado", ResultWindow::Tornado},
        {"Spider", ResultWindow::Spider}, {"SpiderCum", ResultWindow::SpiderCum}, {"Scatter", ResultWindow::Scatter},
        {"Variants", ResultWindow::Variants}, {"Decision", ResultWindow::Decision}, {"Statistics", ResultWindow::Statistics}};
    if (views.count(view)) w->setView(views.at(view));
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
        addLabel("lopt", "Options: Min / Max / Round / Number", x, y, w); y += 10;
        addEdit("min", "", x, y, w / 4 - 2); addEdit("max", "", x + w / 4, y, w / 4 - 2);
        addEdit("round", "", x + w / 2, y, w / 4 - 2); addEdit("number", "", x + 3 * w / 4, y, w / 4); y += 16;
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
    void afterShow() { select("func", 1); updateParams(); }

    void updateParams() {
        int s = std::max(0, selected("func"));
        const auto* f = funcs_[s];
        static const std::map<std::string, std::vector<std::string>> defaults = {
            {"Uniform", {"0", "1"}}, {"Triangle", {"80", "100", "140"}}, {"PERT", {"80", "100", "140"}}, {"Normal", {"100", "15"}},
            {"Lognormal", {"100", "20"}}, {"Beta", {"2", "5", "0", "1"}}, {"Gumbel", {"0", "1"}}, {"Gamma", {"2", "3"}}, {"Exponential", {"0.5"}},
            {"TruncatedNormal", {"0", "1", "-1", "2"}}, {"Weibull", {"2", "3"}}, {"Chi2", {"4"}}, {"StudentT", {"5"}}, {"Pareto", {"3", "1"}},
            {"F", {"5", "10"}}, {"ArcSin", {"0", "1"}}, {"Cauchy", {"0", "1"}}, {"Frechet", {"3", "1", "0"}}, {"Logistic", {"0", "1"}},
            {"DiscreteUniform", {"1", "6"}}, {"Bernoulli", {"0.3"}}, {"Binomial", {"20", "0.3"}}, {"Poisson", {"4"}}, {"Pascal", {"3", "0.4"}},
            {"Geometric", {"0.25"}}, {"HyperGeometric", {"50", "10", "12"}}, {"AR1", {"10", "0.7", "1", "12"}}, {"MA1", {"10", "0.5", "1"}},
            {"ARMA11", {"10", "0.6", "0.3", "1", "12"}}, {"ARCH1", {"0", "1", "0.5", "0"}}, {"WienerProcess", {"100", "0.05", "0.2", "1"}}};
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
            a.push_back(mc::Value());
        } else {
            const auto* f = funcs_[std::max(0, selected("func"))];
            for (size_t i = 0; i < f->params.size(); ++i) {
                bool ok;
                double v = parseOr(text("e" + std::to_string(i)), 0, &ok);
                if (!ok) throw std::runtime_error(std::string("enter a number for ") + f->params[i].name);
                a.push_back(mc::Value::number(v));
            }
        }
        for (const char* o : {"min", "max", "round", "number"}) {
            std::string t = text(o);
            bool ok;
            double v = parseOr(t, 0, &ok);
            a.push_back(t.empty() || !ok ? mc::Value() : mc::Value::number(v));
        }
        if (funcs_[std::max(0, selected("func"))]->kind == mc::FloFunction::TimeSeries && !custom_) {
            // time series take only Number as option
            mc::Value number = a.back();
            a.resize(a.size() - 4);
            a.push_back(number);
        }
        while (!a.empty() && a.back().kind == mc::Value::Empty) a.pop_back();
        return a;
    }

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
            for (int i = 0; i < n; ++i) data_.push_back(mc::evalFlo(*f, a));
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
        int statsW = 150;
        plot_ = drawHistogram(p, 0, 0, w - statsW, h, data_, o);
        std::vector<std::vector<std::string>> rows{{"Statistic", "Value"}};
        rows.push_back({"Mean", niceNum(mc::stats::mean(data_))});
        rows.push_back({"Minimum", niceNum(mc::stats::min(data_))});
        rows.push_back({"Maximum", niceNum(mc::stats::max(data_))});
        rows.push_back({"Variance", niceNum(mc::stats::variance(data_))});
        rows.push_back({"Std deviation", niceNum(mc::stats::stdev(data_))});
        for (double q : {0.01, 0.05, 0.10, 0.25, 0.5, 0.75, 0.90, 0.95, 0.99})
            rows.push_back({"P" + std::to_string(int(std::round(q * 100))), niceNum(mc::stats::percentile(data_, q))});
        drawTable(p, w - statsW + 4, 6, statsW - 8, rows, {80, 70});
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
                f = "=FLOsimula_UserDefined(" + range + ";" + text("bins") + ";" + quote(name) + ")";
            } else {
                const auto* fn = funcs_[std::max(0, selected("func"))];
                std::vector<std::string> parts{quote(name)};
                for (size_t i = 0; i < fn->params.size(); ++i) parts.push_back(text("e" + std::to_string(i)));
                std::vector<std::string> opts;
                if (fn->kind == mc::FloFunction::TimeSeries) opts = {text("number")};
                else opts = {text("min"), text("max"), text("round"), text("number")};
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

void openWizard(const Ctx& ctx, const Frame& frame) {
    auto w = std::make_shared<WizardWindow>(ctx, frame);
    w->show();
    w->afterShow();
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
        addLabel("lname", "Name", x, y + 2, 24); addEdit("name", name_, x + 26, y, w - 26); y += 16;
        addButton("insert", "Insert formula into current cell", x, y, w); y += 16;
        addButton("close", "Close", x, y, w);
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
        int statsW = 150;
        if (!tsMode_) {
            HistogramOptions o;
            o.title = "Historical data (" + std::to_string(data_.size()) + " values)";
            o.bins = std::max(5, int(std::sqrt(double(data_.size()))));
            o.color = col::input.mix(col::white, 0.35);
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
            drawTable(p, w - statsW + 4, 6, statsW - 8, rows, {90, 60});
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
            if (!text("name").empty()) f.insert(f.find('(') + 1, quote(name) + ";");
            if (tsMode_) {  // Number = forecast periods
                f.insert(f.size() - 1, ";" + text("fc"));
            }
            insertIntoCurrentCell(frame_, f, text("name").empty() ? "" : name, mc::VarKind::Input);
            return;
        }
        if (b == "copy") {
            ModelDoc d = docOf(frame_);
            Reference<sheet::XSpreadsheets> sheets = d.doc()->getSheets();
            if (!sheets->hasByName("FLO_Fit")) sheets->insertNewByName("FLO_Fit", sal_Int16(d.sheetNames().size()));
            Reference<sheet::XSpreadsheet> sh(sheets->getByName("FLO_Fit"), UNO_QUERY_THROW);
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
            Sequence<Sequence<Any>> s(sal_Int32(rows.size()));
            for (size_t i = 0; i < rows.size(); ++i) {
                Sequence<Any> r(6);
                for (int j = 0; j < 6; ++j) r.getArray()[j] = rows[i][j];
                s.getArray()[i] = r;
            }
            Reference<sheet::XCellRangeData>(sh->getCellRangeByPosition(0, 0, 5, sal_Int32(rows.size()) - 1), UNO_QUERY_THROW)->setDataArray(s);
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
                            niceNum(d.cell(v.addr)->getValue())});
            colors.push_back(v.kind == mc::VarKind::Input ? Color{221, 235, 247} : v.kind == mc::VarKind::Output ? Color{252, 228, 214} : Color{255, 242, 204});
        }
        rowH_ = p.textHeight() + 6;
        drawTable(p, 6, 6, w - 12, rows, {(w - 12) * 22 / 100, (w - 12) * 12 / 100, (w - 12) * 16 / 100, (w - 12) * 38 / 100, (w - 12) * 12 / 100},
                  &colors, sel_ + 1);
        p.text(6, h - 20, "Click a row to select the cell; double-click for details (result window or preview).", col::muted);
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
            if (v.kind == mc::VarKind::Correlation) {
                // shape of the correlation: copula sample of the two variables' ranks
                double rho = docOf(frame_).cell(v.addr)->getValue();
                int type = 0;
                std::string func;
                std::vector<std::string> args;
                if (mc::formula::findCall(v.formula, "FLOSIMULA_", func, args) && args.size() > 3) type = std::atoi(args[3].c_str());
                std::mt19937_64 rng(1);
                auto cr = mc::generateUniforms(2, 1000, {{0, 1, rho, type}}, rng);
                auto w = std::make_shared<ResultWindow>(ctx_, frame_, v.corrA, v.corrA + " ~ " + v.corrB + " (ranks)", cr.u[0]);
                w->show();
                w->afterShow();
                CanvasWindow::keep(w);
                return;
            }
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
    Reference<sheet::XSpreadsheets> sheets = d.doc()->getSheets();
    if (sheets->hasByName("FLO_Context")) sheets->removeByName("FLO_Context");
    sheets->insertNewByName("FLO_Context", sal_Int16(d.sheetNames().size()));
    Reference<sheet::XSpreadsheet> sh(sheets->getByName("FLO_Context"), UNO_QUERY_THROW);
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
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = 0; j < rows[i].size(); ++j) {
            auto c = sh->getCellByPosition(sal_Int32(j), sal_Int32(i));
            double dv;
            OUString sv;
            if (rows[i][j] >>= dv) c->setValue(dv);
            else if ((rows[i][j] >>= sv) && !sv.isEmpty()) Reference<text::XText>(c, UNO_QUERY_THROW)->setString(sv);
        }
    }
    auto cols = Reference<table::XColumnRowRange>(sh, UNO_QUERY_THROW)->getColumns();
    for (sal_Int32 c = 0; c < 13; ++c) Reference<beans::XPropertySet>(cols->getByIndex(c), UNO_QUERY_THROW)->setPropertyValue("OptimalWidth", Any(true));
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
