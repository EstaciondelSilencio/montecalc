// MonteCalc - chart drawing used by the interactive windows
#pragma once

#include "painter.hpp"

#include <string>
#include <utility>
#include <vector>

namespace montecalc {

using Vec = std::vector<double>;

struct HistogramOptions {
    std::string title;
    int bins = 30;
    Color color = col::input;
    bool cumulative = false;          // draw the cumulative curve on a 0..1 right axis
    bool stripe = false;              // magic stripe
    double stripeX = 0;
    bool thresholds = false;          // highlighted confidence region
    double lower = 0, upper = 0;
    const Vec* realData = nullptr;    // "Real-Data" line
    std::vector<std::pair<std::string, double>> markers;  // vertical marker lines (worst, best, ...)
};

// returns the plot used (for mouse mapping)
Plot drawHistogram(Painter& p, int x, int y, int w, int h, const Vec& data, const HistogramOptions& o);
// several distributions: histograms (outline) + cumulative curves, legend Name / c_Name
Plot drawOverlay(Painter& p, int x, int y, int w, int h, const std::vector<std::pair<std::string, Vec>>& series, int bins,
                 bool withCdf, double probeX, bool probe);
void drawTornado(Painter& p, int x, int y, int w, int h, const std::string& title, const std::vector<std::pair<std::string, double>>& items,
                 std::vector<std::pair<int, int>>* barRows = nullptr);
Plot drawScatter(Painter& p, int x, int y, int w, int h, const Vec& xs, const Vec& ys, const std::string& xName, const std::string& yName,
                 bool highlightTails);
void drawDynamicSensitivity(Painter& p, int x, int y, int w, int h, const Vec& in, const Vec& out, int bins, const std::string& inName,
                            const std::string& outName);
void drawSpider(Painter& p, int x, int y, int w, int h, const std::string& output, const Vec& out,
                const std::vector<std::pair<std::string, Vec>>& inputs, bool cumulative);
// fontSize in points (before the window zoom); tables default to 10 pt
constexpr int kTableFont = 10;
void drawTable(Painter& p, int x, int y, int w, const std::vector<std::vector<std::string>>& rows, const std::vector<int>& colWidths,
               const std::vector<Color>* rowColors = nullptr, int highlightRow = -1, int fontSize = kTableFont);
int tableRowHeight(Painter& p, int fontSize = kTableFont);
void drawStatsGrid(Painter& p, int x, int y, int w, const std::string& name, const std::string& definition, const std::string& cell,
                   const Vec& v, int bins, int fontSize = kTableFont, int maxHeight = 0);
void drawLineChart(Painter& p, int x, int y, int w, int h, const std::string& title,
                   const std::vector<std::pair<std::string, Vec>>& lines, const std::vector<Color>& colors, int firstForecastIndex = -1);

} // namespace montecalc
