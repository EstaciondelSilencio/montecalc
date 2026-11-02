// MonteCalc - drawing layer for the interactive windows. The same chart code paints on
// screen (UNO XGraphics) or into SVG (for "Export image" and comment images).
#pragma once

#include <com/sun/star/awt/XFont.hpp>
#include <com/sun/star/awt/XGraphics.hpp>

#include <string>
#include <vector>

namespace montecalc {

namespace css = com::sun::star;

struct Color {
    int r, g, b;
    sal_Int32 rgb() const { return (r << 16) | (g << 8) | b; }
    std::string hex() const;
    Color mix(const Color& o, double t) const;
};

namespace col {
const Color white{255, 255, 255}, black{0, 0, 0}, grid{225, 228, 232}, axis{110, 115, 125}, text{40, 44, 52},
    muted{120, 125, 135}, input{52, 120, 200}, output{232, 132, 40}, relation{214, 160, 30}, green{46, 160, 90},
    red{210, 60, 60}, highlight{250, 210, 60}, panel{246, 247, 249}, stripe{200, 30, 90};
const Color series[] = {{52, 120, 200}, {232, 132, 40}, {46, 160, 90}, {150, 90, 190}, {210, 60, 60}, {30, 160, 170}, {140, 110, 70}, {200, 90, 160}};
}  // namespace col

class Painter {
public:
    virtual ~Painter() = default;
    virtual void line(int x1, int y1, int x2, int y2, const Color& c, int width = 1, bool dashed = false) = 0;
    virtual void rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) = 0;
    virtual void roundRect(int x, int y, int w, int h, int radius, const Color* fill, const Color* stroke) = 0;
    virtual void ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) = 0;
    virtual void polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) = 0;
    virtual void polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width = 1) = 0;
    // text with its top-left corner at (x, y); bold/size are hints
    virtual void text(int x, int y, const std::string& s, const Color& c, bool bold = false, int size = 0) = 0;
    virtual int textWidth(const std::string& s, int size = 0) = 0;
    virtual int textHeight(int size = 0) = 0;
};

// Screen painter on an XGraphics
class ScreenPainter : public Painter {
public:
    explicit ScreenPainter(const css::uno::Reference<css::awt::XGraphics>& g);
    void line(int x1, int y1, int x2, int y2, const Color& c, int width, bool dashed) override;
    void rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) override;
    void roundRect(int x, int y, int w, int h, int radius, const Color* fill, const Color* stroke) override;
    void ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) override;
    void polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) override;
    void polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width) override;
    void text(int x, int y, const std::string& s, const Color& c, bool bold, int size) override;
    int textWidth(const std::string& s, int size) override;
    int textHeight(int size) override;

private:
    css::uno::Reference<css::awt::XGraphics> g_;
    css::uno::Reference<css::awt::XFont> font_, bold_;
    int charH_ = 12;
};

// SVG painter (text width estimated)
class SvgPainter : public Painter {
public:
    SvgPainter(int w, int h, double scale = 1.0);
    void line(int x1, int y1, int x2, int y2, const Color& c, int width, bool dashed) override;
    void rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) override;
    void roundRect(int x, int y, int w, int h, int radius, const Color* fill, const Color* stroke) override;
    void ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) override;
    void polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) override;
    void polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width) override;
    void text(int x, int y, const std::string& s, const Color& c, bool bold, int size) override;
    int textWidth(const std::string& s, int size) override;
    int textHeight(int size) override;
    std::string svg() const;

private:
    int w_, h_;
    double scale_;
    std::string body_;
};

// ---------------------------------------------------------------- chart helpers
struct Plot {
    int x, y, w, h;          // plot area in pixels
    double x0, x1, y0, y1;   // data range
    int px(double v) const { return x + int((v - x0) / (x1 - x0 == 0 ? 1 : x1 - x0) * w); }
    int py(double v) const { return y + h - int((v - y0) / (y1 - y0 == 0 ? 1 : y1 - y0) * h); }
    double vx(int p) const { return x0 + double(p - x) / (w == 0 ? 1 : w) * (x1 - x0); }
};

std::string niceNum(double v);  // short axis label
std::vector<double> ticks(double lo, double hi, int n);
// draws background, grid and axes with tick labels; returns the inner plot
Plot axes(Painter& p, int x, int y, int w, int h, double x0, double x1, double y0, double y1, const std::string& title,
          bool percentY = false, const std::string& ylabel = "");

} // namespace montecalc
