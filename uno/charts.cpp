// MonteCalc - chart drawing used by the interactive windows
#include "charts.hpp"

#include "../core/stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace montecalc {

static std::string pctText(double p) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f%%", p * 100);
    return b;
}

static void range(const Vec& v, double& lo, double& hi) {
    lo = mc::stats::min(v);
    hi = mc::stats::max(v);
    if (!std::isfinite(lo)) { lo = 0; hi = 1; }
    if (hi == lo) { lo -= 0.5; hi += 0.5; }
}

Plot drawHistogram(Painter& p, int x, int y, int w, int h, const Vec& data0, const HistogramOptions& o) {
    Vec data = mc::stats::finite(data0);
    double lo, hi;
    range(data, lo, hi);
    if (o.realData && !o.realData->empty()) {
        lo = std::min(lo, mc::stats::min(*o.realData));
        hi = std::max(hi, mc::stats::max(*o.realData));
    }
    int bins = std::max(2, o.bins);
    double bw = (hi - lo) / bins;
    std::vector<double> cnt(bins, 0.0), real(bins, 0.0);
    for (double v : data) cnt[std::min(bins - 1, std::max(0, int((v - lo) / bw)))] += 1;
    if (o.realData)
        for (double v : *o.realData)
            if (std::isfinite(v)) real[std::min(bins - 1, std::max(0, int((v - lo) / bw)))] += 1;
    double n = std::max(1.0, double(data.size())), nr = o.realData ? std::max(1.0, double(o.realData->size())) : 1;
    double top = 0;
    for (int i = 0; i < bins; ++i) top = std::max({top, cnt[i] / n, o.realData ? real[i] / nr : 0.0});
    top = top > 0 ? top * 1.12 : 1;
    Plot pl = axes(p, x, y, w, h, lo, hi, 0, top, o.title, true);
    // confidence region
    double share = 0;
    if (o.thresholds) {
        for (double v : data) share += v >= o.lower && v <= o.upper;
        share /= n;
    }
    for (int i = 0; i < bins; ++i) {
        double a = lo + i * bw, b = a + bw;
        int x0 = pl.px(a) + 1, x1 = pl.px(b) - 1;
        int y0 = pl.py(cnt[i] / n);
        Color c = o.color;
        if (o.thresholds && (b <= o.lower || a >= o.upper)) c = o.color.mix(col::white, 0.65);
        p.rect(x0, y0, std::max(1, x1 - x0), pl.y + pl.h - y0, &c, nullptr);
    }
    if (o.realData && !o.realData->empty()) {
        std::vector<int> xs, ys;
        for (int i = 0; i < bins; ++i) { xs.push_back(pl.px(lo + (i + 0.5) * bw)); ys.push_back(pl.py(real[i] / nr)); }
        p.polyline(xs, ys, col::black, 2);
        p.text(pl.x + pl.w - p.textWidth("Real-Data") - 4, pl.y + 2, "Real-Data", col::black, true);
    }
    if (o.cumulative) {
        Vec s = mc::stats::sorted(data);
        std::vector<int> xs, ys;
        for (int k = 0; k <= 100; ++k) {
            double q = mc::stats::percentile(s, k / 100.0);
            xs.push_back(pl.px(q));
            ys.push_back(pl.y + pl.h - int(k / 100.0 * pl.h));
        }
        p.polyline(xs, ys, col::text, 2);
        p.text(pl.x + pl.w + 2 - p.textWidth("100%"), pl.y - p.textHeight(), "cum.", col::muted);
    }
    for (auto& [label, v] : o.markers) {
        if (!(v >= lo && v <= hi)) continue;
        int xx = pl.px(v);
        p.line(xx, pl.y, xx, pl.y + pl.h, col::text, 1, true);
        p.text(xx + 3, pl.y + 2, label, col::text);
    }
    if (o.thresholds) {
        std::string t = "Confidence " + pctText(share) + " between " + niceNum(o.lower) + " and " + niceNum(o.upper);
        p.text(pl.x + 4, pl.y + pl.h - p.textHeight() - 4, t, col::text, true);
        for (double v : {o.lower, o.upper})
            if (v >= lo && v <= hi) p.line(pl.px(v), pl.y, pl.px(v), pl.y + pl.h, col::green, 2);
    }
    if (o.stripe) {
        double sx = std::min(hi, std::max(lo, o.stripeX));
        int xx = pl.px(sx);
        p.line(xx, pl.y, xx, pl.y + pl.h, col::stripe, 2);
        double q = mc::stats::percentRank(data, sx);
        std::string t = "x = " + niceNum(sx) + "   p = " + pctText(q);
        int tw = p.textWidth(t) + 10, th = p.textHeight() + 6;
        int bx = std::min(pl.x + pl.w - tw, std::max(pl.x, xx - tw / 2));
        p.roundRect(bx, pl.y + 4, tw, th, 6, &col::white, &col::stripe);
        p.text(bx + 5, pl.y + 7, t, col::stripe, true);
    }
    return pl;
}

Plot drawOverlay(Painter& p, int x, int y, int w, int h, const std::vector<std::pair<std::string, Vec>>& series, int bins, bool withCdf,
                 double probeX, bool probe) {
    double lo = 1e300, hi = -1e300;
    for (auto& [n, v] : series) {
        double a, b;
        range(v, a, b);
        lo = std::min(lo, a);
        hi = std::max(hi, b);
    }
    if (series.empty()) { lo = 0; hi = 1; }
    bins = std::max(2, bins);
    double bw = (hi - lo) / bins, top = 0;
    std::vector<std::vector<double>> shares;
    for (auto& [n, v] : series) {
        std::vector<double> c(bins, 0.0);
        Vec f = mc::stats::finite(v);
        for (double d : f) c[std::min(bins - 1, std::max(0, int((d - lo) / bw)))] += 1.0 / std::max<size_t>(1, f.size());
        for (double s : c) top = std::max(top, s);
        shares.push_back(c);
    }
    Plot pl = axes(p, x, y, w, h - 22, lo, hi, 0, top > 0 ? top * 1.12 : 1, "", true, "frequency");
    for (size_t k = 0; k < series.size(); ++k) {
        const Color& c = col::series[k % 8];
        std::vector<int> xs, ys;
        for (int i = 0; i < bins; ++i) {
            int y0 = pl.py(shares[k][i]);
            xs.push_back(pl.px(lo + i * bw)); ys.push_back(y0);
            xs.push_back(pl.px(lo + (i + 1) * bw)); ys.push_back(y0);
        }
        p.polyline(xs, ys, c, 2);
        if (withCdf) {
            Vec s = mc::stats::sorted(series[k].second);
            std::vector<int> cx, cy;
            for (int q = 0; q <= 100; ++q) {
                cx.push_back(pl.px(mc::stats::percentile(s, q / 100.0)));
                cy.push_back(pl.y + pl.h - int(q / 100.0 * pl.h));
            }
            p.polyline(cx, cy, c.mix(col::white, 0.25), 1);
        }
    }
    if (withCdf) {
        for (int q = 0; q <= 4; ++q) {
            std::string t = std::to_string(q * 25) + "%";
            p.text(pl.x + pl.w + 3, pl.y + pl.h - int(q / 4.0 * pl.h) - p.textHeight() / 2, t, col::muted);
        }
    }
    // legend Name / c_Name
    int lx = x + 8, ly = y + h - 16;
    for (size_t k = 0; k < series.size(); ++k) {
        const Color& c = col::series[k % 8];
        p.rect(lx, ly + 3, 10, 8, &c, nullptr);
        std::string t = series[k].first + (withCdf ? " / c_" + series[k].first : "");
        p.text(lx + 14, ly, t, col::text);
        lx += p.textWidth(t) + 28;
    }
    if (probe && probeX >= pl.x && probeX <= pl.x + pl.w) {
        int px = int(probeX);
        p.line(px, pl.y, px, pl.y + pl.h, col::stripe, 1);
        double xv = pl.vx(px);
        std::vector<std::string> lines{"x = " + niceNum(xv)};
        for (auto& [n, v] : series) lines.push_back(n + ": " + pctText(mc::stats::percentRank(v, xv)));
        int tw = 0;
        for (auto& l : lines) tw = std::max(tw, p.textWidth(l));
        int th = p.textHeight();
        int bx = std::min(pl.x + pl.w - tw - 12, px + 8);
        p.roundRect(bx, pl.y + 4, tw + 12, th * int(lines.size()) + 8, 6, &col::white, &col::stripe);
        for (size_t i = 0; i < lines.size(); ++i) p.text(bx + 6, pl.y + 8 + int(i) * th, lines[i], i ? col::series[(i - 1) % 8] : col::text, i == 0);
    }
    return pl;
}

void drawTornado(Painter& p, int x, int y, int w, int h, const std::string& title, const std::vector<std::pair<std::string, double>>& items,
                 std::vector<std::pair<int, int>>* barRows) {
    p.rect(x, y, w, h, &col::white, nullptr);
    p.text(x + 8, y + 4, title, col::text, true);
    if (items.empty()) return;
    int labelW = 0;
    for (auto& [n, v] : items) labelW = std::max(labelW, p.textWidth(n));
    labelW += 16;
    int top = y + p.textHeight() + 14, rowH = std::max(14, std::min(28, (h - (top - y) - 24) / int(items.size())));
    int cx = x + labelW + (w - labelW - 20) / 2, half = (w - labelW - 20) / 2;
    p.line(cx, top - 4, cx, top + rowH * int(items.size()), col::axis);
    for (size_t i = 0; i < items.size(); ++i) {
        int yy = top + int(i) * rowH;
        double v = items[i].second;
        int len = int(std::fabs(v) * half);
        const Color& c = v >= 0 ? col::green : col::red;
        p.rect(v >= 0 ? cx : cx - len, yy + 2, std::max(1, len), rowH - 4, &c, nullptr);
        p.text(x + 8, yy + (rowH - p.textHeight()) / 2, items[i].first, col::text, std::fabs(v) >= 0.3);
        char b[16];
        std::snprintf(b, sizeof b, "%+.2f", v);
        p.text(v >= 0 ? cx + len + 4 : cx - len - p.textWidth(b) - 4, yy + (rowH - p.textHeight()) / 2, b, col::muted);
        if (barRows) barRows->push_back({yy, yy + rowH});
    }
    for (double t : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
        int xx = cx + int(t * half);
        std::string s = niceNum(t);
        p.text(xx - p.textWidth(s) / 2, top + rowH * int(items.size()) + 4, s, col::muted);
    }
}

Plot drawScatter(Painter& p, int x, int y, int w, int h, const Vec& xs, const Vec& ys, const std::string& xName, const std::string& yName,
                 bool tails) {
    double x0, x1, y0, y1;
    range(xs, x0, x1);
    range(ys, y0, y1);
    double r = mc::stats::spearman(xs, ys);
    char t[160];
    std::snprintf(t, sizeof t, "%s vs %s  (Spearman %.3f)", yName.c_str(), xName.c_str(), std::isfinite(r) ? r : 0.0);
    Plot pl = axes(p, x, y, w, h, x0, x1, y0, y1, t);
    double q05 = mc::stats::percentile(xs, 0.05), q95 = mc::stats::percentile(xs, 0.95);
    size_t n = std::min(xs.size(), ys.size()), step = std::max<size_t>(1, n / 1000);  // at most 1000 points
    for (size_t i = 0; i < n; i += step) {
        if (!std::isfinite(xs[i]) || !std::isfinite(ys[i])) continue;
        Color c = col::input.mix(col::white, 0.2);
        if (tails && xs[i] <= q05) c = col::red;
        if (tails && xs[i] >= q95) c = col::green;
        p.ellipse(pl.px(xs[i]) - 2, pl.py(ys[i]) - 2, 4, 4, &c, nullptr);
    }
    double b = mc::stats::regressionSlope(xs, ys), a = mc::stats::regressionIntercept(xs, ys);
    if (std::isfinite(a) && std::isfinite(b)) p.line(pl.px(x0), pl.py(a + b * x0), pl.px(x1), pl.py(a + b * x1), col::output, 2);
    return pl;
}

void drawDynamicSensitivity(Painter& p, int x, int y, int w, int h, const Vec& in, const Vec& out, int bins, const std::string& inName,
                            const std::string& outName) {
    // interval means of the output after sorting by the input, with min/max markers
    std::vector<std::pair<double, double>> pr;
    for (size_t i = 0; i < in.size() && i < out.size(); ++i)
        if (std::isfinite(in[i]) && std::isfinite(out[i])) pr.push_back({in[i], out[i]});
    std::sort(pr.begin(), pr.end());
    bins = std::max(2, bins);
    Vec means, mins, maxs, centers;
    for (int g = 0; g < bins; ++g) {
        size_t s = pr.size() * g / bins, e = pr.size() * (g + 1) / bins;
        if (e <= s) continue;
        double acc = 0, mn = 1e300, mx = -1e300, c = 0;
        for (size_t k = s; k < e; ++k) { acc += pr[k].second; mn = std::min(mn, pr[k].second); mx = std::max(mx, pr[k].second); c += pr[k].first; }
        means.push_back(acc / (e - s)); mins.push_back(mn); maxs.push_back(mx); centers.push_back(c / (e - s));
    }
    if (means.empty()) return;
    double lo = *std::min_element(mins.begin(), mins.end()), hi = *std::max_element(maxs.begin(), maxs.end());
    auto ext = std::minmax_element(means.begin(), means.end());
    Plot pl = axes(p, x, y, w, h, 0, double(means.size()), lo, hi,
                   "Dynamic sensitivity: mean of " + outName + " by interval of " + inName + "  (range " + niceNum(*ext.second - *ext.first) + ")");
    for (size_t i = 0; i < means.size(); ++i) {
        int x0 = pl.px(double(i)) + 2, x1 = pl.px(double(i + 1)) - 2, mid = (x0 + x1) / 2;
        p.line(mid, pl.py(mins[i]), mid, pl.py(maxs[i]), col::muted);
        Color c = (means[i] == *ext.first) ? col::red : (means[i] == *ext.second) ? col::green : col::input;
        p.rect(x0, pl.py(means[i]) - 2, x1 - x0, 4, &c, nullptr);
    }
}

void drawSpider(Painter& p, int x, int y, int w, int h, const std::string& output, const Vec& out,
                const std::vector<std::pair<std::string, Vec>>& inputs, bool cumulative) {
    const int Q = 10;
    Plot pl = axes(p, x, y, w, h - 20, 0, 1, -1, 1,
                   std::string("Spider: Pearson correlation with ") + output + " by 10% quantile" + (cumulative ? " (cumulative)" : ""));
    p.line(pl.x, pl.py(0), pl.x + pl.w, pl.py(0), col::axis);
    double q[Q + 1];
    for (int k = 0; k <= Q; ++k) q[k] = mc::stats::percentile(out, k / double(Q));
    int lx = x + 8, ly = y + h - 16;
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::vector<int> xs, ys;
        bool strong = false;
        for (int k = 1; k <= Q; ++k) {
            double lo = cumulative ? q[0] : q[k - 1], hi = q[k];
            Vec a, b;
            for (size_t j = 0; j < out.size() && j < inputs[i].second.size(); ++j)
                if (out[j] >= lo && out[j] <= hi) { a.push_back(inputs[i].second[j]); b.push_back(out[j]); }
            double r = mc::stats::pearson(a, b);
            if (!std::isfinite(r)) r = 0;
            strong |= std::fabs(r) >= 0.3;
            xs.push_back(pl.px((k - 0.5) / Q));
            ys.push_back(pl.py(r));
        }
        Color c = strong ? col::series[i % 8] : col::grid.mix(col::muted, 0.4);
        p.polyline(xs, ys, c, strong ? 2 : 1);
        if (strong) {
            p.rect(lx, ly + 3, 10, 8, &c, nullptr);
            p.text(lx + 14, ly, inputs[i].first, col::text);
            lx += p.textWidth(inputs[i].first) + 28;
        }
    }
}

int tableRowHeight(Painter& p, int fontSize) { return p.textHeight(fontSize) + 8; }

void drawTable(Painter& p, int x, int y, int w, const std::vector<std::vector<std::string>>& rows, const std::vector<int>& widths,
               const std::vector<Color>* rowColors, int highlightRow, int fontSize) {
    int th = tableRowHeight(p, fontSize);
    for (size_t r = 0; r < rows.size(); ++r) {
        int yy = y + int(r) * th;
        if (r == 0) p.rect(x, yy, w, th, &col::panel, nullptr);
        else if (int(r) == highlightRow) p.rect(x, yy, w, th, &col::highlight, nullptr);
        else if (rowColors && r < rowColors->size()) p.rect(x, yy, w, th, &(*rowColors)[r], nullptr);
        int xx = x + 4;
        for (size_t c = 0; c < rows[r].size(); ++c) {
            int cw = c < widths.size() ? widths[c] : 80;
            if (c + 1 == rows[r].size()) cw = std::max(cw, x + w - xx);   // the last column may use the rest of the row
            // shorten text that does not fit its column (UTF-8 aware) instead of running into the next one
            std::string t = rows[r][c];
            if (p.textWidth(t, fontSize) > cw - 8) {
                while (!t.empty() && p.textWidth(t + "...", fontSize) > cw - 8) {
                    t.pop_back();
                    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
                }
                t += "...";
            }
            p.text(xx, yy + 4, t, col::text, r == 0, fontSize);
            xx += cw;
        }
        p.line(x, yy + th, x + w, yy + th, col::grid);
    }
}

void drawStatsGrid(Painter& p, int x, int y, int w, const std::string& name, const std::string& def, const std::string& cell, const Vec& v, int bins,
                   int fontSize, int maxHeight) {
    std::vector<std::vector<std::string>> rows{{"Statistic", "Value"}};
    auto add = [&](const std::string& a, const std::string& b) { rows.push_back({a, b}); };
    add("Variable", name);
    if (!def.empty()) add("Definition", def.size() > 40 ? def.substr(0, 40) + "..." : def);
    if (!cell.empty()) add("Cell", cell);
    add("Observations", niceNum(mc::stats::count(v)));
    add("Bins", std::to_string(bins));
    add("Mean", niceNum(mc::stats::mean(v)));
    add("Variance", niceNum(mc::stats::variance(v)));
    add("Skewness", niceNum(mc::stats::skewness(v)));
    add("Kurtosis", niceNum(mc::stats::kurtosis(v)));
    add("Minimum", niceNum(mc::stats::min(v)));
    add("Maximum", niceNum(mc::stats::max(v)));
    add("Mode", niceNum(mc::stats::mode(v, bins)));
    add("Range", niceNum(mc::stats::max(v) - mc::stats::min(v)));
    add("Std. deviation", niceNum(mc::stats::stdev(v)));
    add("Coeff. of variation", niceNum(mc::stats::cv(v)));
    for (double q : {0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99}) add("P" + std::to_string(int(std::round(q * 100))), niceNum(mc::stats::percentile(v, q)));
    int th = tableRowHeight(p, fontSize);
    if (maxHeight > 0 && int(rows.size()) * th > maxHeight && w >= 360) {
        // not enough height: two tables side by side, each with the heading
        size_t half = (rows.size() + 1) / 2;
        std::vector<std::vector<std::string>> a(rows.begin(), rows.begin() + half), b{rows[0]};
        b.insert(b.end(), rows.begin() + half, rows.end());
        int cw = (w - 12) / 2;
        drawTable(p, x, y, cw, a, {cw * 50 / 100, cw * 50 / 100}, nullptr, -1, fontSize);
        drawTable(p, x + cw + 12, y, cw, b, {cw * 50 / 100, cw * 50 / 100}, nullptr, -1, fontSize);
        return;
    }
    drawTable(p, x, y, w, rows, {w * 50 / 100, w * 50 / 100}, nullptr, -1, fontSize);
}

void drawLineChart(Painter& p, int x, int y, int w, int h, const std::string& title, const std::vector<std::pair<std::string, Vec>>& lines,
                   const std::vector<Color>& colors, int firstForecast) {
    double lo = 1e300, hi = -1e300;
    size_t n = 0;
    for (auto& [nm, v] : lines) {
        for (double d : v) if (std::isfinite(d)) { lo = std::min(lo, d); hi = std::max(hi, d); }
        n = std::max(n, v.size());
    }
    if (n < 2) return;
    if (hi == lo) { lo -= 1; hi += 1; }
    Plot pl = axes(p, x, y, w, h - 20, 0, double(n - 1), lo, hi, title);
    if (firstForecast > 0) {
        int fx = pl.px(firstForecast - 0.5);
        Color shade = col::panel;
        p.rect(fx, pl.y, pl.x + pl.w - fx, pl.h, &shade, nullptr);
        p.text(fx + 4, pl.y + 2, "forecast", col::muted);
    }
    int lx = x + 8, ly = y + h - 16;
    for (size_t k = 0; k < lines.size(); ++k) {
        std::vector<int> xs, ys;
        for (size_t i = 0; i < lines[k].second.size(); ++i) {
            double v = lines[k].second[i];
            if (!std::isfinite(v)) continue;
            xs.push_back(pl.px(double(i)));
            ys.push_back(pl.py(v));
        }
        const Color& c = k < colors.size() ? colors[k] : col::series[k % 8];
        p.polyline(xs, ys, c, 2);
        p.rect(lx, ly + 3, 10, 8, &c, nullptr);
        p.text(lx + 14, ly, lines[k].first, col::text);
        lx += p.textWidth(lines[k].first) + 28;
    }
}

} // namespace montecalc
