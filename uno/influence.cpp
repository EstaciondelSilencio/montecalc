// MonteCalc - influence diagram: the model's FLO_ variables as an interactive diagram,
// kept linked to the worksheet, with a Results tab for the simulated distributions.
#include "canvaswin.hpp"
#include "charts.hpp"
#include "deps.hpp"
#include "model.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "windows.hpp"

#include "../core/formula.h"
#include "../core/stats.h"

#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/table/CellContentType.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Reference;

namespace montecalc {

namespace {

struct Node {
    int var = -1;              // index into graph.vars, -1 for a module
    std::string name, family;
    mc::VarKind kind = mc::VarKind::Input;
    double x = 0.5, y = 0.5;   // normalised centre
    std::vector<int> members;  // modules: member node indices
};

std::string familyOf(const std::string& n) {
    size_t e = n.size();
    while (e > 0 && std::isdigit((unsigned char)n[e - 1])) --e;
    if (e == n.size() || e == 0) return {};
    std::string stem = n.substr(0, e);
    while (!stem.empty() && (stem.back() == '_' || stem.back() == ' ')) stem.pop_back();
    return stem;
}

} // namespace

class InfluenceWindow : public CanvasWindow {
public:
    InfluenceWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - Influence diagram", 620, 380, 118) {
        int x = panelX(), w = panelW(), y = 6;
        addButton("tabDiagram", "Diagram", x, y, w / 2 - 1); addButton("tabResults", "Results", x + w / 2 + 1, y, w / 2 - 1); y += 18;
        addButton("variables", "Variables (load)", x, y, w); y += 16;
        addButton("reload", "Reload (F5)", x, y, w / 2 - 1); addButton("refresh", "Refresh (Ctrl+R)", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("relayout", "Re-layout (Ctrl+L)", x, y, w); y += 16;
        addLabel("lzoom", "Zoom", x, y + 3, 22);
        addButton("zoomOut", "-", x + 24, y, 18); addButton("zoomIn", "+", x + 44, y, 18);
        addButton("zoom100", "100%", x + 64, y, w - 64); y += 16;
        addCheck("edit", "Edit mode (Ctrl+E)", x, y, w, false); y += 12;
        addCheck("follow", "Follow in Calc (Ctrl+F)", x, y, w, true); y += 12;
        addCheck("dist", "Show distributions (Ctrl+D)", x, y, w, false); y += 12;
        addCheck("sens", "Show sensitivity (Ctrl+I)", x, y, w, false); y += 12;
        addCheck("focus", "Focus on selection (Ctrl+H)", x, y, w, false); y += 12;
        addCheck("group", "Group view (Ctrl+K)", x, y, w, false); y += 12;
        addCheck("ortho", "Orthogonal arrows (Ctrl+J)", x, y, w, false); y += 14;
        addButton("goto", "Go to cell (Ctrl+G)", x, y, w); y += 16;
        addButton("export", "Export image (Ctrl+S)", x, y, w); y += 16;
        addButton("context", "Write context for Claude", x, y, w); y += 16;
        addButton("mentor", "AI mentor (Ctrl+M)", x, y, w); y += 18;
        addLabel("lres", "Results: variables / statistics of", x, y, w); y += 10;
        addList("resvars", {}, x, y, w, 40, false, true); y += 42;
        addList("statvar", {}, x, y, w, 12, true); y += 16;
        addButton("close", "Close", x, y, w);
        setAnchor("resvars", AnchorRight | StretchH);   // the results list grows with the window
        setAnchor("statvar", AnchorRight | AnchorBottom);
        setAnchor("close", AnchorRight | AnchorBottom);
    }

    // ---------------------------------------------------------------- data
    void load() {
        ModelDoc d(frame_->getController()->getModel());
        g_ = buildGraph(d);
        base_.clear();
        for (size_t i = 0; i < g_.vars.size(); ++i) {
            Node n;
            n.var = int(i);
            n.name = g_.vars[i].name;
            n.kind = g_.vars[i].kind;
            n.family = familyOf(n.name);
            base_.push_back(n);
        }
        layout();
        selectedVisible_ = -1;
        loaded_ = true;
        status_ = std::to_string(g_.vars.size()) + " variables and " + std::to_string(edgeCount()) + " relationships loaded";
        // results tab lists
        std::vector<std::string> items;
        resNames_.clear();
        auto& sim = mc::Simulation::instance();
        for (auto k : {mc::VarKind::Output, mc::VarKind::Input})
            for (auto& n : sim.resultNames(k)) {
                items.push_back(std::string(k == mc::VarKind::Output ? "Output: " : "Input: ") + n);
                resNames_.push_back(n);
            }
        setItems("resvars", items);
        setItems("statvar", resNames_, 0);
        for (int i = 0; i < std::min<int>(2, int(items.size())); ++i) select("resvars", i);
        repaint();
    }

    int edgeCount() {
        int e = 0;
        for (auto& s : g_.dependsOn) e += int(s.size());
        for (auto& c : g_.corrPartners) e += (c.first >= 0) + (c.second >= 0);
        return e;
    }

    // layered layout (inputs left, relationships middle, outputs right) refined by forces
    void layout() {
        size_t n = base_.size();
        if (n == 0) return;
        std::vector<int> layer(n, 0);
        for (int pass = 0; pass < int(n) + 1; ++pass)
            for (size_t i = 0; i < n; ++i)
                for (int j : g_.dependsOn[i]) layer[i] = std::max(layer[i], layer[j] + 1);
        int maxL = 1;
        for (size_t i = 0; i < n; ++i) {
            if (base_[i].kind == mc::VarKind::Correlation) {
                auto [a, b] = g_.corrPartners[i];
                layer[i] = std::max(a >= 0 ? layer[a] : 0, b >= 0 ? layer[b] : 0);
            }
            maxL = std::max(maxL, layer[i]);
        }
        for (size_t i = 0; i < n; ++i)
            if (base_[i].kind == mc::VarKind::Output) layer[i] = std::max(layer[i], maxL);
        std::map<int, std::vector<int>> byLayer;
        for (size_t i = 0; i < n; ++i) byLayer[layer[i] * 2 + (base_[i].kind == mc::VarKind::Correlation)].push_back(int(i));
        int maxKey = byLayer.rbegin()->first;
        for (auto& [k, ids] : byLayer)
            for (size_t j = 0; j < ids.size(); ++j) {
                base_[ids[j]].x = 0.1 + 0.8 * (maxKey ? double(k) / maxKey : 0.5);
                base_[ids[j]].y = (j + 1.0) / (ids.size() + 1.0);
            }
        relax(250, -1);
    }

    void relax(int iterations, int fixed) {
        size_t n = base_.size();
        std::vector<double> lx(n);
        for (size_t i = 0; i < n; ++i) lx[i] = base_[i].x;
        for (int it = 0; it < iterations; ++it) {
            std::vector<double> fx(n, 0), fy(n, 0);
            for (size_t i = 0; i < n; ++i)
                for (size_t j = i + 1; j < n; ++j) {
                    double dx = base_[i].x - base_[j].x, dy = (base_[i].y - base_[j].y) * 1.4, d2 = dx * dx + dy * dy + 1e-4;
                    double f = 0.0009 / d2;
                    fx[i] += f * dx; fy[i] += f * dy; fx[j] -= f * dx; fy[j] -= f * dy;
                }
            auto spring = [&](int a, int b) {
                double dx = base_[b].x - base_[a].x, dy = base_[b].y - base_[a].y;
                fy[a] += 0.05 * dy; fy[b] -= 0.05 * dy;
                fx[a] += 0.01 * dx; fx[b] -= 0.01 * dx;
            };
            for (size_t i = 0; i < n; ++i) for (int j : g_.dependsOn[i]) spring(j, int(i));
            for (size_t i = 0; i < n; ++i) {
                auto [a, b] = g_.corrPartners[i];
                if (a >= 0) spring(a, int(i));
                if (b >= 0) spring(b, int(i));
            }
            for (size_t i = 0; i < n; ++i) {
                if (int(i) == fixed) continue;
                fx[i] += 0.05 * (lx[i] - base_[i].x);  // keep the layer structure
                base_[i].x = std::min(0.95, std::max(0.05, base_[i].x + std::max(-0.02, std::min(0.02, fx[i]))));
                base_[i].y = std::min(0.93, std::max(0.07, base_[i].y + std::max(-0.02, std::min(0.02, fy[i]))));
            }
        }
    }

    // nodes and edges as currently shown (group view, focus)
    void visible(std::vector<Node>& nodes, std::vector<std::pair<int, int>>& arrows, std::vector<std::pair<int, int>>& dashed) {
        nodes.clear();
        arrows.clear();
        dashed.clear();
        std::vector<int> map(base_.size(), -1);
        bool group = checkedSafe("group");
        std::map<std::string, int> famCount;
        if (group)
            for (auto& n : base_) if (!n.family.empty()) famCount[n.family]++;
        std::map<std::string, int> moduleIdx;
        for (size_t i = 0; i < base_.size(); ++i) {
            const Node& b = base_[i];
            if (group && !b.family.empty() && famCount[b.family] >= 2 && !expanded_.count(b.family)) {
                auto it = moduleIdx.find(b.family);
                if (it == moduleIdx.end()) {
                    Node m;
                    m.var = -1;
                    m.name = b.family + " (" + std::to_string(famCount[b.family]) + ")";
                    m.family = b.family;
                    m.kind = b.kind;
                    m.x = 0; m.y = 0;
                    moduleIdx[b.family] = int(nodes.size());
                    nodes.push_back(m);
                    it = moduleIdx.find(b.family);
                }
                Node& m = nodes[it->second];
                m.members.push_back(int(i));
                m.x += b.x; m.y += b.y;
                map[i] = it->second;
            } else {
                map[i] = int(nodes.size());
                nodes.push_back(b);
            }
        }
        for (auto& n : nodes)
            if (n.var < 0 && !n.members.empty()) { n.x /= n.members.size(); n.y /= n.members.size(); }
        std::set<std::pair<int, int>> seen;
        for (size_t i = 0; i < base_.size(); ++i)
            for (int j : g_.dependsOn[i]) {
                int a = map[j], b = map[i];
                if (a != b && seen.insert({a, b}).second) arrows.push_back({a, b});
            }
        for (size_t i = 0; i < base_.size(); ++i) {
            auto [a, b] = g_.corrPartners[i];
            if (a >= 0 && map[a] != map[i]) dashed.push_back({map[i], map[a]});
            if (b >= 0 && map[b] != map[i]) dashed.push_back({map[i], map[b]});
        }
        // focus on selection: selected node + direct neighbours
        if (checkedSafe("focus") && selectedVisible_ >= 0 && selectedVisible_ < int(nodes.size())) {
            std::set<int> keep{selectedVisible_};
            for (auto& [a, b] : arrows) { if (a == selectedVisible_) keep.insert(b); if (b == selectedVisible_) keep.insert(a); }
            for (auto& [a, b] : dashed) { if (a == selectedVisible_) keep.insert(b); if (b == selectedVisible_) keep.insert(a); }
            hidden_ = keep;
        } else {
            hidden_.clear();
        }
    }

    bool checkedSafe(const std::string& n) {
        try { return checked(n); } catch (...) { return false; }
    }

    // ---------------------------------------------------------------- painting
    struct Box { int x, y, w, h; };
    Box boxOf(Painter& p, const Node& n, int w, int h) {
        bool dist = checkedSafe("dist");
        int th = p.textHeight(kNameSize);
        int bw = std::max(90, p.textWidth(n.name, kNameSize) + 26), bh = dist ? 80 : th * 2 + 12;
        int x = int(n.x * w) - bw / 2, y = int(n.y * (h - 40)) - bh / 2;
        x = std::max(4, std::min(x, w - bw - 4));   // keep the whole node visible
        y = std::max(4, std::min(y, h - 40 - bh - p.textHeight() - 8));
        return {x, y, bw, bh};
    }

    Vec samplesOf(const Node& n) {
        Vec v;
        if (n.var >= 0) mc::Simulation::instance().samples(n.name, v);
        return v;
    }

    // the diagram is drawn in unscaled units through a zoom painter (Ctrl + / Ctrl - / Ctrl 0)
    bool exporting_ = false;
    bool exportPng(const std::string& url, const std::string& mime, int w, int h, double scale) {
        exporting_ = true;
        bool ok = false;
        try { ok = exportImage(url, mime, w, h, scale); } catch (...) { exporting_ = false; throw; }
        exporting_ = false;
        return ok;
    }

    // the base window applies the zoom (Ctrl + / Ctrl - / Ctrl 0)
    void paint(Painter& p, int w, int h) override { paintScaled(p, w, h); }

    void paintScaled(Painter& p, int w, int h) {
        p.rect(0, 0, w, h, &col::white, nullptr);
        if (!loaded_) {
            p.text(12, 12, "Press 'Variables' to read the FLO_ variables of the model.", col::text);
            return;
        }
        if (resultsTab_) { paintResults(p, w, h); return; }
        std::vector<Node> nodes;
        std::vector<std::pair<int, int>> arrows, dashed;
        visible(nodes, arrows, dashed);
        // local copies: a screen repaint may run while an image is exported (different size)
        std::vector<Box> boxes;
        for (auto& n : nodes) boxes.push_back(boxOf(p, n, w, h));
        if (!exporting_) { shown_ = nodes; boxes_ = boxes; }
        auto isShown = [&](int i) { return hidden_.empty() || hidden_.count(i); };
        auto related = [&](int a, int b) { return selectedVisible_ < 0 || a == selectedVisible_ || b == selectedVisible_; };
        bool sens = checkedSafe("sens"), ortho = checkedSafe("ortho");
        // edges
        for (auto& [a, b] : dashed) {
            if (!isShown(a) || !isShown(b)) continue;
            Box A = boxes[a], B = boxes[b];
            Color c = related(a, b) ? col::relation : col::grid;
            p.line(A.x + A.w / 2, A.y + A.h / 2, B.x + B.w / 2, B.y + B.h / 2, c, 1, true);
        }
        for (auto& [a, b] : arrows) {
            if (!isShown(a) || !isShown(b)) continue;
            Box A = boxes[a], B = boxes[b];
            Color c = related(a, b) ? col::axis : col::grid;
            int width = 1;
            double imp = 0;
            if (sens) {
                Vec sa = samplesOf(nodes[a]), sb = samplesOf(nodes[b]);
                imp = mc::stats::spearman(sa, sb);
                if (std::isfinite(imp)) {
                    c = imp >= 0 ? col::green : col::red;
                    width = 1 + int(std::fabs(imp) * 5);
                } else {
                    imp = 0;
                }
            }
            int x1 = A.x + A.w, y1 = A.y + A.h / 2, x2 = B.x, y2 = B.y + B.h / 2;
            if (x2 < x1) { x1 = A.x; x2 = B.x + B.w; }
            if (ortho) {
                int mx = (x1 + x2) / 2;
                p.line(x1, y1, mx, y1, c, width);
                p.line(mx, y1, mx, y2, c, width);
                p.line(mx, y2, x2, y2, c, width);
                arrowHead(p, mx, y2, x2, y2, c);
            } else {
                p.line(x1, y1, x2, y2, c, width);
                arrowHead(p, x1, y1, x2, y2, c);
            }
            if (sens && b == selectedVisible_) {
                char t[16];
                std::snprintf(t, sizeof t, "%+.2f", imp);
                p.text((x1 + x2) / 2 + 3, (y1 + y2) / 2 - 14, t, c, true);
            }
        }
        // nodes
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!isShown(int(i))) continue;
            const Node& n = nodes[i];
            Box b = boxes[i];
            bool dim = selectedVisible_ >= 0 && int(i) != selectedVisible_ && !neighbour(arrows, dashed, selectedVisible_, int(i));
            Color base = n.kind == mc::VarKind::Input ? col::input : n.kind == mc::VarKind::Output ? col::green : col::relation;
            Color fill = (dim ? base.mix(col::white, 0.85) : base.mix(col::white, 0.7));
            Color stroke = int(i) == selectedVisible_ ? col::stripe : (dim ? base.mix(col::white, 0.6) : base);
            if (n.var < 0) {
                p.rect(b.x, b.y, b.w, b.h, &fill, &stroke);
                p.rect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, nullptr, &stroke);
            } else if (n.kind == mc::VarKind::Input) {
                p.roundRect(b.x, b.y, b.w, b.h, 14, &fill, &stroke);
            } else if (n.kind == mc::VarKind::Output) {
                int k = 10;
                p.polygon({b.x + k, b.x + b.w - k, b.x + b.w, b.x + b.w - k, b.x + k, b.x},
                          {b.y, b.y, b.y + b.h / 2, b.y + b.h, b.y + b.h, b.y + b.h / 2}, &fill, &stroke);
            } else {
                p.ellipse(b.x, b.y, b.w, b.h, &fill, &stroke);
            }
            p.text(b.x + (b.w - p.textWidth(n.name, kNameSize)) / 2, b.y + 5, n.name, dim ? col::muted : col::text, true, kNameSize);
            if (n.var >= 0 && !checkedSafe("dist")) {
                std::string v = niceNum(g_.numValues[n.var]);
                p.text(b.x + (b.w - p.textWidth(v)) / 2, b.y + 6 + p.textHeight(kNameSize), v, col::muted);
            }
            if (n.var >= 0 && checkedSafe("dist")) miniChart(p, n, b);
        }
        // legend and status bar
        int th = p.textHeight(), bar = th + 8, ly = h - bar - th - 8;
        int lx = 8;
        auto legend = [&](int shape, const Color& c, const std::string& t) {
            if (shape == 0) p.roundRect(lx, ly + 1, 18, th - 2, 6, &c, nullptr);
            else if (shape == 1) p.ellipse(lx, ly + 1, 20, th - 2, &c, nullptr);
            else p.rect(lx, ly + 1, 18, th - 2, &c, nullptr);
            p.text(lx + 24, ly, t, col::muted);
            lx += 24 + p.textWidth(t) + 18;
        };
        legend(0, col::input, "input");
        legend(1, col::relation, "relationship");
        legend(2, col::green, "output");
        p.rect(0, h - bar, w, bar, &col::panel, nullptr);
        char z[24];
        std::snprintf(z, sizeof z, "Zoom %d%%", int(zoom() * 100 + 0.5));
        p.text(w - p.textWidth(z) - 8, h - bar + 4, z, col::muted);
        p.text(8, h - bar + 4, status_, col::text);
        // tooltip
        if (hover_ >= 0 && hover_ < int(nodes.size()) && nodes[hover_].var >= 0) {
            const auto& v = g_.vars[nodes[hover_].var];
            ModelDoc d(frame_->getController()->getModel());
            std::vector<std::string> lines{v.name, "Cell " + d.cellName(v.addr), v.formula.size() > 70 ? v.formula.substr(0, 70) + "..." : v.formula};
            Vec s = samplesOf(nodes[hover_]);
            if (!s.empty())
                lines.push_back("mean " + niceNum(mc::stats::mean(s)) + "  P5 " + niceNum(mc::stats::percentile(s, 0.05)) + "  P95 " +
                                niceNum(mc::stats::percentile(s, 0.95)) + "  range " + niceNum(mc::stats::max(s) - mc::stats::min(s)));
            int tw = 0;
            for (auto& l : lines) tw = std::max(tw, p.textWidth(l));
            int th = p.textHeight(), bx = std::min(w - tw - 14, mouseX_ + 12), by = std::min(h - th * int(lines.size()) - 30, mouseY_ + 12);
            Color bg{255, 252, 235};
            p.roundRect(bx, by, tw + 12, th * int(lines.size()) + 8, 6, &bg, &col::axis);
            for (size_t i = 0; i < lines.size(); ++i) p.text(bx + 6, by + 4 + int(i) * th, lines[i], col::text, i == 0);
        }
    }

    static bool neighbour(const std::vector<std::pair<int, int>>& a, const std::vector<std::pair<int, int>>& d, int s, int i) {
        for (auto& [x, y] : a) if ((x == s && y == i) || (y == s && x == i)) return true;
        for (auto& [x, y] : d) if ((x == s && y == i) || (y == s && x == i)) return true;
        return false;
    }

    static void arrowHead(Painter& p, int x1, int y1, int x2, int y2, const Color& c) {
        double a = std::atan2(y2 - y1, x2 - x1), s = 8;
        p.polygon({x2, x2 - int(s * std::cos(a - 0.4)), x2 - int(s * std::cos(a + 0.4))},
                  {y2, y2 - int(s * std::sin(a - 0.4)), y2 - int(s * std::sin(a + 0.4))}, &c, &c);
    }

    void miniChart(Painter& p, const Node& n, const Box& b) {
        int cx = b.x + 8, cy = b.y + 6 + p.textHeight(kNameSize) + 2, cw = b.w - 16, ch = b.h - p.textHeight(kNameSize) - 16;
        if (n.kind == mc::VarKind::Correlation) {
            auto [ia, ib] = g_.corrPartners[n.var];
            Vec sa, sb;
            if (ia >= 0) mc::Simulation::instance().samples(g_.vars[ia].name, sa);
            if (ib >= 0) mc::Simulation::instance().samples(g_.vars[ib].name, sb);
            if (sa.empty() || sb.empty()) { p.text(cx, cy, "no simulation", col::muted); return; }
            Vec ra = mc::stats::ranks(sa), rb = mc::stats::ranks(sb);
            size_t step = std::max<size_t>(1, ra.size() / 150);
            for (size_t i = 0; i < ra.size() && i < rb.size(); i += step) {
                int x = cx + int(ra[i] / ra.size() * cw), y = cy + ch - int(rb[i] / rb.size() * ch);
                p.rect(x, y, 2, 2, &col::relation, nullptr);
            }
            char t[24];
            std::snprintf(t, sizeof t, "r = %.2f", mc::stats::spearman(sa, sb));
            p.text(cx + 2, cy, t, col::text);
            return;
        }
        Vec v = samplesOf(n);
        if (v.empty()) { p.text(cx, cy, "no simulation", col::muted); return; }
        auto h = mc::stats::histogram(v, 16);
        double top = *std::max_element(h.count.begin(), h.count.end());
        Color c = n.kind == mc::VarKind::Output ? col::green : col::input;
        for (size_t i = 0; i < h.count.size(); ++i) {
            int bh = int(h.count[i] / top * (ch - 2));
            p.rect(cx + int(i * cw / 16.0), cy + ch - bh, std::max(1, cw / 16 - 1), bh, &c, nullptr);
        }
        double m = mc::stats::mean(v), lo = h.lo.front(), hi = h.hi.back();
        int mx = cx + int((m - lo) / (hi - lo == 0 ? 1 : hi - lo) * cw);
        p.line(mx, cy, mx, cy + ch, col::stripe, 2);
    }

    void paintResults(Painter& p, int w, int h) {
        std::vector<std::pair<std::string, Vec>> series;
        for (int i : selectedAll("resvars")) {
            if (i < 0 || i >= int(resNames_.size())) continue;
            Vec v;
            mc::Simulation::instance().samples(resNames_[i], v);
            series.push_back({resNames_[i], v});
        }
        if (series.empty()) {
            p.text(12, 12, resNames_.empty() ? "No simulation results - run Start first." : "Tick variables in the Results list.", col::text);
            return;
        }
        int gridW = std::min(260, w / 3);
        drawOverlay(p, 0, 0, w - gridW, h, series, mc::Simulation::instance().settings().bins, true, mouseX_, probe_);
        std::string sv;
        try { sv = selectedItem("statvar"); } catch (...) {}
        Vec v;
        mc::Variable info;
        for (auto& x : mc::Simulation::instance().resultVariables()) if (x.name == sv) info = x;
        if (mc::Simulation::instance().samples(sv, v))
            drawStatsGrid(p, w - gridW + 4, 4, gridW - 8, sv, info.description, info.cell, v, mc::Simulation::instance().settings().bins,
                          kTableFont, h - 8);
    }

    // ---------------------------------------------------------------- interaction
    int hit(int x, int y) {
        for (int i = int(boxes_.size()) - 1; i >= 0; --i) {
            if (!hidden_.empty() && !hidden_.count(i)) continue;
            const Box& b = boxes_[i];
            if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return i;
        }
        return -1;
    }

    void selectNode(int i) {
        selectedVisible_ = i;
        if (i >= 0 && i < int(shown_.size()) && shown_[i].var >= 0) {
            const auto& v = g_.vars[shown_[i].var];
            ModelDoc d(frame_->getController()->getModel());
            status_ = v.name + "  |  " + d.cellName(v.addr) + "  |  value " + niceNum(g_.numValues[shown_[i].var]);
            if (checkedSafe("follow")) d.select(v.addr);
        } else if (i >= 0) {
            status_ = "Module " + shown_[i].name + " - double-click to expand";
        }
    }

    void mouseDown(const MouseInfo& m) override {
        if (!loaded_) return;
        mouseX_ = m.x;
        mouseY_ = m.y;
        if (resultsTab_) { probe_ = true; repaint(); return; }
        if (m.right) { contextMenu(m.x, m.y); return; }
        int i = hit(m.x, m.y);
        if (m.clicks >= 2 && i >= 0) {
            doubleClick(i);
            return;
        }
        if (i < 0) {
            selectedVisible_ = -1;
            if (checkedSafe("focus")) hidden_.clear();
            status_ = std::to_string(g_.vars.size()) + " variables and " + std::to_string(edgeCount()) + " relationships";
        } else {
            selectNode(i);
            dragNode_ = shown_[i].var;
        }
        repaint();
    }

    void doubleClick(int i) {
        const Node& n = shown_[i];
        if (n.var < 0) { expanded_.insert(n.family); repaint(); return; }
        if (!n.family.empty() && checkedSafe("group") && expanded_.count(n.family)) { expanded_.erase(n.family); repaint(); return; }
        if (checkedSafe("edit")) { editFormula(n.var); return; }
        ModelDoc(frame_->getController()->getModel()).select(g_.vars[n.var].addr);
    }

    void mouseUp(const MouseInfo&) override { dragNode_ = -1; }

    void mouseMove(const MouseInfo& m, bool dragging) override {
        mouseX_ = m.x;
        mouseY_ = m.y;
        if (resultsTab_) { probe_ = true; repaint(); return; }
        int w, h;
        canvasSize(w, h);
        if (dragging && dragNode_ >= 0) {
            base_[dragNode_].x = std::min(0.97, std::max(0.03, double(m.x) / w));
            base_[dragNode_].y = std::min(0.95, std::max(0.05, double(m.y) / (h - 40)));
            relax(15, dragNode_);  // the rest of the diagram reflows around it
            repaint();
            return;
        }
        int hv = hit(m.x, m.y);
        if (hv != hover_) { hover_ = hv; repaint(); }
    }

    void mouseExit() override {
        if (hover_ >= 0 || probe_) { hover_ = -1; probe_ = false; repaint(); }
    }

    void contextMenu(int x, int y) {
        std::vector<std::pair<int, std::string>> items = {
            {1, "Reload\tF5"}, {2, "Refresh values\tCtrl+R"}, {3, "Re-layout\tCtrl+L"}, {4, "Export image...\tCtrl+S"}, {0, ""},
            {5, "Edit mode\tCtrl+E"}, {6, "Follow in Calc\tCtrl+F"}, {7, "Go to cell\tCtrl+G"}, {0, ""},
            {8, "Show distributions\tCtrl+D"}, {9, "Show sensitivity\tCtrl+I"}, {10, "Focus on selection\tCtrl+H"}, {11, "Group view\tCtrl+K"},
            {12, "Orthogonal arrows\tCtrl+J"}, {0, ""}, {15, "Zoom in\tCtrl +"}, {16, "Zoom out\tCtrl -"}, {17, "Zoom 100%\tCtrl 0"}, {0, ""}, {13, "Write context for Claude\tCtrl+Shift+C"}, {14, "AI mentor\tCtrl+M"}};
        int id = popup(x, y, items);
        static const char* actions[] = {"",      "reload", "refresh", "relayout", "export",  "edit",    "follow",  "goto",   "dist",
                                        "sens",  "focus",  "group",   "ortho",    "context", "mentor",  "zoomIn",  "zoomOut", "zoom100"};
        if (id > 0 && id < 18) act(actions[id]);
    }

    void option(const std::string& o) {
        if (o.rfind("zoom=", 0) == 0) { setZoom(std::atof(o.c_str() + 5)); return; }
        if (o == "results") { resultsTab_ = true; return; }
        if (o == "select" && !base_.empty()) { selectedVisible_ = int(base_.size()) - 1; return; }
        setChecked(o, true);
    }

    void toggle(const std::string& n) { setChecked(n, !checkedSafe(n)); repaint(); }

    void act(const std::string& a) {
        if (a == "reload" || a == "variables") { load(); return; }
        if (a == "refresh") {
            ModelDoc d(frame_->getController()->getModel());
            for (size_t i = 0; i < g_.vars.size(); ++i) g_.numValues[i] = d.cell(g_.vars[i].addr)->getValue();
            repaint();
            return;
        }
        if (a == "relayout") { layout(); repaint(); return; }
        if (a == "zoomIn" || a == "zoomOut" || a == "zoom100") {
            setZoom(a == "zoom100" ? 1.0 : a == "zoomIn" ? zoom() * 1.15 : zoom() / 1.15);
            return;
        }
        if (a == "export") { exportDialog(); return; }
        if (a == "goto") {
            if (selectedVisible_ >= 0 && shown_[selectedVisible_].var >= 0)
                ModelDoc(frame_->getController()->getModel()).select(g_.vars[shown_[selectedVisible_].var].addr);
            return;
        }
        if (a == "context") {
            writeContextSheet(ctx_, frame_);
            status_ = "FLO_Context sheet written";
            repaint();
            return;
        }
        if (a == "mentor") { openMentor(ctx_, frame_); return; }
        if (a == "edit" || a == "follow" || a == "dist" || a == "sens" || a == "focus" || a == "group" || a == "ortho") {
            toggle(a);
            if (a == "sens" && mc::Simulation::instance().iterations() == 0) status_ = "Sensitivity needs simulation results - run Start first";
            if (a == "dist" && mc::Simulation::instance().iterations() == 0) status_ = "Distributions need simulation results - run Start first";
            repaint();
        }
    }

    bool key(int code, int ch, bool ctrl, bool shift) override {
        if (code == awt::Key::F5) { act("reload"); return true; }
        if (!ctrl) return false;
        switch (code) {
        case awt::Key::R: act("refresh"); return true;
        case awt::Key::L: act("relayout"); return true;
        case awt::Key::S: act("export"); return true;
        case awt::Key::E: act("edit"); return true;
        case awt::Key::F: act("follow"); return true;
        case awt::Key::T: status_ = "The diagram window always stays in front of its Calc window"; repaint(); return true;
        case awt::Key::G: act("goto"); return true;
        case awt::Key::D: act("dist"); return true;
        case awt::Key::I: act("sens"); return true;
        case awt::Key::H: act("focus"); return true;
        case awt::Key::K: act("group"); return true;
        case awt::Key::J: act("ortho"); return true;
        case awt::Key::C: if (shift) { act("context"); return true; } return false;
        case awt::Key::M: act("mentor"); return true;
        case awt::Key::ADD: act("zoomIn"); return true;
        case awt::Key::SUBTRACT: act("zoomOut"); return true;
        case awt::Key::NUM0: act("zoom100"); return true;
        default: (void)ch; return false;
        }
    }

    void itemChanged(const std::string& n) override {
        if (n == "focus" && !checkedSafe("focus")) hidden_.clear();
        repaint();
    }

    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "tabDiagram") { resultsTab_ = false; repaint(); return; }
        if (b == "tabResults") { resultsTab_ = true; repaint(); return; }
        act(b);
    }

    void exportDialog() {
        std::vector<FormField> f = {{"File", homeDir() + "/influence_diagram.png"}, {"Format", "PNG"}};
        f[1].options = {"PNG", "JPEG", "BMP"};
        if (!showForm(ctx_, "Export image", f)) return;
        std::string url = fileUrl(f[0].value);
        std::string mime = f[1].value == "JPEG" ? "image/jpeg" : f[1].value == "BMP" ? "image/bmp" : "image/png";
        int w, h;
        canvasSize(w, h);
        if (exportPng(url, mime, w, h, 2.0)) { status_ = "Saved " + url; repaint(); }  // higher resolution than the screen
    }

    // formula editor (Edit mode): shift a reference by columns/rows or type a new formula
    void editFormula(int var) {
        ModelDoc d(frame_->getController()->getModel());
        auto cell = d.cell(g_.vars[var].addr);
        std::string f = u8(cell->getFormula());
        std::vector<mc::formula::RangeAddr> refs;
        auto src = d.formulaSource();
        std::string list;
        try {
            auto tree = mc::formula::parse(f, g_.vars[var].addr.Sheet, src.get());
            mc::formula::collectRanges(tree, refs);
        } catch (...) {
        }
        auto names = d.sheetNames();
        for (size_t i = 0; i < refs.size(); ++i)
            list += std::to_string(i + 1) + ": " + mc::formula::cellName(refs[i].a, true, [&](int s) { return names[s]; }) + "\n";
        std::vector<FormField> fields = {{"Formula", f}, {"References (Table references are not listed)", list.empty() ? "(none)" : list},
                                         {"Reference number to move", refs.empty() ? "" : "1"}, {"Columns + / -", "0"}, {"Rows + / -", "0"}};
        fields[1].lines = 4;
        fields[1].readOnly = true;
        if (!showForm(ctx_, "Formula editor - " + g_.vars[var].name, fields, 300)) return;
        std::string nf = fields[0].value;
        int k = std::atoi(fields[2].value.c_str()), dc = std::atoi(fields[3].value.c_str()), dr = std::atoi(fields[4].value.c_str());
        if (k >= 1 && k <= int(refs.size()) && (dc || dr)) {
            // replace the k-th occurrence of the reference text in the formula
            const auto& r = refs[k - 1].a;
            std::string old = mc::formula::columnName(r.col) + std::to_string(r.row + 1);
            std::string rep = mc::formula::columnName(std::max(0, r.col + dc)) + std::to_string(std::max(1, r.row + 1 + dr));
            size_t pos = 0;
            int seen = 0;
            std::string upper = nf;
            for (auto& c : upper) c = char(std::toupper((unsigned char)c));
            while ((pos = upper.find(old, pos)) != std::string::npos) {
                bool boundary = (pos == 0 || !std::isalnum((unsigned char)upper[pos - 1]) || upper[pos - 1] == '$') &&
                                (pos + old.size() >= upper.size() || !std::isdigit((unsigned char)upper[pos + old.size()]));
                if (boundary && ++seen) { nf.replace(pos, old.size(), rep); break; }
                pos += old.size();
            }
        }
        cell->setFormula(ou(nf));  // writing replaces the cell content
        d.recalc();
        load();  // relationships may have changed
    }

private:
    ModelGraph g_;
    std::vector<Node> base_, shown_;
    std::vector<Box> boxes_;
    std::set<int> hidden_;
    std::set<std::string> expanded_;
    std::vector<std::string> resNames_;
    static constexpr int kNameSize = 10;   // node names (points, before zoom)
    int selectedVisible_ = -1, hover_ = -1, dragNode_ = -1, mouseX_ = 0, mouseY_ = 0;
    bool loaded_ = false, resultsTab_ = false, probe_ = false;
    std::string status_ = "Press 'Variables'";
};

void openInfluenceDiagram(const Ctx& ctx, const Frame& frame, const std::string& options, const std::string& exportTo) {
    auto w = std::make_shared<InfluenceWindow>(ctx, frame);
    w->show();
    w->load();
    // options: comma separated toggles (dist, sens, group, ortho, focus, results)
    std::stringstream ss(options);
    std::string o;
    while (std::getline(ss, o, ','))
        if (!o.empty()) w->option(o);
    if (!exportTo.empty()) {
        w->exportPng(exportTo, "image/png", 1000, 620, 1.0);
        w->close();
        return;
    }
    CanvasWindow::keep(w);
}

} // namespace montecalc
