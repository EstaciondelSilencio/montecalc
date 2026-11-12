// MonteCalc - drawing layer
#include "painter.hpp"
#include "util.hpp"

#ifndef MONTECALC_XLL
#include <com/sun/star/awt/FontDescriptor.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/awt/SimpleFontMetric.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/awt/XFont.hpp>

#endif
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef MONTECALC_XLL
using namespace com::sun::star;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
#endif

namespace montecalc {

std::string Color::hex() const {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", r & 255, g & 255, b & 255);
    return buf;
}

Color Color::mix(const Color& o, double t) const {
    return {int(r + (o.r - r) * t), int(g + (o.g - g) * t), int(b + (o.b - b) * t)};
}

#ifndef MONTECALC_XLL
// ============================================================ screen

ScreenPainter::ScreenPainter(const Reference<awt::XGraphics>& g) : g_(g) {
    dev_ = g_->getDevice();
    font_ = fontFor(0, false);
    bold_ = fontFor(0, true);
    if (font_.is()) {
        g_->setFont(font_);
        charH_ = sizedH_[kTextFont];
    }
}

Reference<awt::XFont> ScreenPainter::fontFor(int size, bool bold) {
    int pt = size > 0 ? size : kTextFont;
    auto& cache = sized_[bold ? 1 : 0];
    auto it = cache.find(pt);
    if (it != cache.end()) return it->second;
    Reference<awt::XFont> f;
    if (dev_.is()) {
        awt::FontDescriptor fd;
        fd.Name = "Liberation Sans";
        fd.Height = sal_Int16(pt);
        if (bold) fd.Weight = awt::FontWeight::BOLD;
        f = dev_->getFont(fd);
        if (f.is() && !sizedH_.count(pt)) {
            auto m = f->getFontMetric();
            sizedH_[pt] = std::max(pt + 1, int(m.Ascent + m.Descent));
        }
    }
    cache[pt] = f;
    return f;
}

void ScreenPainter::line(int x1, int y1, int x2, int y2, const Color& c, int width, bool dashed) {
    g_->setLineColor(c.rgb());
    if (dashed) {
        double len = std::hypot(x2 - x1, y2 - y1);
        int n = std::max(1, int(len / 6));
        for (int i = 0; i < n; i += 2) {
            double a = double(i) / n, b = std::min(1.0, double(i + 1) / n);
            g_->drawLine(x1 + int((x2 - x1) * a), y1 + int((y2 - y1) * a), x1 + int((x2 - x1) * b), y1 + int((y2 - y1) * b));
        }
        return;
    }
    for (int k = 0; k < std::max(1, width); ++k) {
        int o = k - (width - 1) / 2;
        if (std::abs(x2 - x1) > std::abs(y2 - y1)) g_->drawLine(x1, y1 + o, x2, y2 + o);
        else g_->drawLine(x1 + o, y1, x2 + o, y2);
    }
}

void ScreenPainter::rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    g_->setFillColor(fill ? fill->rgb() : sal_Int32(0xFFFFFFFF));
    g_->setLineColor(stroke ? stroke->rgb() : (fill ? fill->rgb() : sal_Int32(0xFFFFFFFF)));
    if (!fill) g_->setFillColor(sal_Int32(0xFFFFFFFF));
    if (fill) g_->drawRect(x, y, w, h);
    else if (stroke) {
        g_->drawLine(x, y, x + w, y);
        g_->drawLine(x + w, y, x + w, y + h);
        g_->drawLine(x + w, y + h, x, y + h);
        g_->drawLine(x, y + h, x, y);
    }
}

void ScreenPainter::roundRect(int x, int y, int w, int h, int radius, const Color* fill, const Color* stroke) {
    g_->setFillColor(fill ? fill->rgb() : sal_Int32(0xFFFFFFFF));
    g_->setLineColor(stroke ? stroke->rgb() : (fill ? fill->rgb() : 0));
    g_->drawRoundedRect(x, y, w, h, radius, radius);
}

void ScreenPainter::ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    g_->setFillColor(fill ? fill->rgb() : sal_Int32(0xFFFFFFFF));
    g_->setLineColor(stroke ? stroke->rgb() : (fill ? fill->rgb() : 0));
    g_->drawEllipse(x, y, w, h);
}

void ScreenPainter::polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) {
    Sequence<sal_Int32> X(sal_Int32(xs.size())), Y(sal_Int32(ys.size()));
    for (size_t i = 0; i < xs.size(); ++i) { X.getArray()[i] = xs[i]; Y.getArray()[i] = ys[i]; }
    g_->setFillColor(fill ? fill->rgb() : sal_Int32(0xFFFFFFFF));
    g_->setLineColor(stroke ? stroke->rgb() : (fill ? fill->rgb() : 0));
    g_->drawPolygon(X, Y);
}

void ScreenPainter::polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width) {
    if (xs.size() < 2) return;
    Sequence<sal_Int32> X(sal_Int32(xs.size())), Y(sal_Int32(ys.size()));
    g_->setLineColor(c.rgb());
    for (int k = 0; k < std::max(1, width); ++k) {
        for (size_t i = 0; i < xs.size(); ++i) { X.getArray()[i] = xs[i]; Y.getArray()[i] = ys[i] + k - (width - 1) / 2; }
        g_->drawPolyLine(X, Y);
    }
}

void ScreenPainter::text(int x, int y, const std::string& s, const Color& c, bool bold, int size) {
    g_->setTextColor(c.rgb());
    g_->setTextFillColor(sal_Int32(0xFFFFFFFF));
    auto f = fontFor(size, bold);
    if (f.is()) g_->setFont(f);
    g_->drawText(x, y, ou(s));  // (x, y) is the top-left corner of the text
    if (f.is() && f != font_ && font_.is()) g_->setFont(font_);
}

int ScreenPainter::textWidth(const std::string& s, int size) {
    auto f = fontFor(size, false);
    if (f.is()) return f->getStringWidth(ou(s));
    return int(s.size() * 7 * (size > 0 ? size : kTextFont) / 9);
}

int ScreenPainter::textHeight(int size) {
    int pt = size > 0 ? size : kTextFont;
    fontFor(size, false);
    auto it = sizedH_.find(pt);
    return it != sizedH_.end() ? it->second : charH_;
}

#endif   // LibreOffice screen painter

// ============================================================ SVG

SvgPainter::SvgPainter(int w, int h, double scale) : w_(w), h_(h), scale_(scale) {}

static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '&') r += "&amp;";
        else if (c == '"') r += "&quot;";
        else r += c;
    }
    return r;
}

static std::string paint(const Color* fill, const Color* stroke) {
    return std::string(" fill=\"") + (fill ? fill->hex() : "none") + "\" stroke=\"" + (stroke ? stroke->hex() : "none") + "\"";
}

void SvgPainter::line(int x1, int y1, int x2, int y2, const Color& c, int width, bool dashed) {
    char b[256];
    std::snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"%s\" stroke-width=\"%d\"%s/>\n", x1, y1, x2, y2,
                  c.hex().c_str(), std::max(1, width), dashed ? " stroke-dasharray=\"4,3\"" : "");
    body_ += b;
}

void SvgPainter::rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    body_ += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) + "\" height=\"" +
             std::to_string(h) + "\"" + paint(fill, stroke) + "/>\n";
}

void SvgPainter::roundRect(int x, int y, int w, int h, int r, const Color* fill, const Color* stroke) {
    body_ += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) + "\" height=\"" +
             std::to_string(h) + "\" rx=\"" + std::to_string(r / 2) + "\"" + paint(fill, stroke) + "/>\n";
}

void SvgPainter::ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    body_ += "<ellipse cx=\"" + std::to_string(x + w / 2) + "\" cy=\"" + std::to_string(y + h / 2) + "\" rx=\"" + std::to_string(w / 2) +
             "\" ry=\"" + std::to_string(h / 2) + "\"" + paint(fill, stroke) + "/>\n";
}

void SvgPainter::polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) {
    std::string pts;
    for (size_t i = 0; i < xs.size(); ++i) pts += std::to_string(xs[i]) + "," + std::to_string(ys[i]) + " ";
    body_ += "<polygon points=\"" + pts + "\"" + paint(fill, stroke) + "/>\n";
}

void SvgPainter::polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width) {
    std::string pts;
    for (size_t i = 0; i < xs.size(); ++i) pts += std::to_string(xs[i]) + "," + std::to_string(ys[i]) + " ";
    body_ += "<polyline points=\"" + pts + "\" fill=\"none\" stroke=\"" + c.hex() + "\" stroke-width=\"" + std::to_string(std::max(1, width)) + "\"/>\n";
}

void SvgPainter::text(int x, int y, const std::string& s, const Color& c, bool bold, int size) {
    int fs = size > 0 ? size : 11;
    body_ += "<text x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y + fs) + "\" font-family=\"Liberation Sans, Arial, sans-serif\" font-size=\"" +
             std::to_string(fs) + "\" fill=\"" + c.hex() + "\"" + (bold ? " font-weight=\"bold\"" : "") + ">" + esc(s) + "</text>\n";
}

int SvgPainter::textWidth(const std::string& s, int size) { return int(s.size() * (size > 0 ? size : 11) * 0.55); }
int SvgPainter::textHeight(int size) { return (size > 0 ? size : 11) + 3; }

std::string SvgPainter::svg() const {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(int(w_ * scale_)) + "\" height=\"" +
           std::to_string(int(h_ * scale_)) + "\" viewBox=\"0 0 " + std::to_string(w_) + " " + std::to_string(h_) + "\">\n<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n" +
           body_ + "</svg>\n";
}

// ============================================================ chart helpers

std::string niceNum(double v) {
    char b[64];
    double a = std::fabs(v);
    if (a >= 1e9) std::snprintf(b, sizeof b, "%.3gG", v / 1e9);
    else if (a >= 1e6) std::snprintf(b, sizeof b, "%.3gM", v / 1e6);
    else if (a >= 1e4) std::snprintf(b, sizeof b, "%.3gk", v / 1e3);
    else std::snprintf(b, sizeof b, "%.4g", v);
    return b;
}

std::vector<double> ticks(double lo, double hi, int n) {
    std::vector<double> t;
    if (!(hi > lo)) { t.push_back(lo); return t; }
    double raw = (hi - lo) / std::max(1, n), mag = std::pow(10.0, std::floor(std::log10(raw))), step = mag;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * mag >= raw) { step = m * mag; break; }
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) t.push_back(std::fabs(v) < step * 1e-9 ? 0 : v);
    return t;
}

Plot axes(Painter& p, int x, int y, int w, int h, double x0, double x1, double y0, double y1, const std::string& title, bool percentY,
          const std::string& ylabel) {
    const int th = p.textHeight(), tth = p.textHeight(kBoldFont);
    p.rect(x, y, w, h, &col::white, nullptr);
    int top = y + (title.empty() ? th / 2 + 4 : tth + 12);
    if (!title.empty()) p.text(x + 8, y + 4, title, col::text, true);
    std::string widest = percentY ? "100%" : niceNum(y1);
    int left = x + std::max(36, p.textWidth(widest) + 12), bottom = y + h - th - 10, right = x + w - 12;
    Plot pl{left, top, std::max(10, right - left), std::max(10, bottom - top), x0, x1, y0, y1};
    for (double t : ticks(y0, y1, 5)) {
        int yy = pl.py(t);
        p.line(pl.x, yy, pl.x + pl.w, yy, col::grid);
        char b[32];
        std::snprintf(b, sizeof b, "%.0f%%", t * 100);
        std::string s = percentY ? b : niceNum(t);
        p.text(pl.x - p.textWidth(s) - 5, yy - th / 2, s, col::muted);
    }
    for (double t : ticks(x0, x1, std::max(2, pl.w / std::max(80, p.textWidth("-000.0k") + 16)))) {
        int xx = pl.px(t);
        p.line(xx, pl.y + pl.h, xx, pl.y + pl.h + 4, col::axis);
        std::string s = niceNum(t);
        p.text(xx - p.textWidth(s) / 2, pl.y + pl.h + 6, s, col::muted);
    }
    p.line(pl.x, pl.y + pl.h, pl.x + pl.w, pl.y + pl.h, col::axis);
    p.line(pl.x, pl.y, pl.x, pl.y + pl.h, col::axis);
    if (!ylabel.empty()) p.text(x + 4, top - th - 2 < y ? y : top - th - 2, ylabel, col::muted);
    return pl;
}

} // namespace montecalc
