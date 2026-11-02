// MonteCalc - fitting distributions, time series and Holt-Winters models to data
#pragma once

#include "distributions.h"
#include "timeseries.h"

#include <functional>
#include <random>
#include <string>
#include <vector>

namespace mc {

// Nelder-Mead minimisation. Returns the best point; `fbest` receives f(best).
std::vector<double> nelderMead(const std::function<double(const std::vector<double>&)>& f,
                               std::vector<double> x0, std::vector<double> step, int maxIter,
                               double& fbest);

// ---------------------------------------------------------------- distributions
struct DistFit {
    std::string name;      // e.g. "Normal"
    std::string formula;   // e.g. "=FLOsimula_Normal(10.2;1.9)"
    std::vector<double> params;
    double k = 0;          // number of parameters for the AIC (min/max count 1.1 each)
    double rss = 0;        // residual sum of squares between the CDFs
    double ks = 0;         // Kolmogorov-Smirnov statistic
    double ksPValue = 0;
    double aic = 0;        // 2k + n ln(RSS)
    bool ok = false;
    std::string error;
};

struct FitOptions {
    bool preferHigh = false;     // weight deviations above `highThreshold` more strongly (beta only)
    double highThreshold = 0;
    double highWeight = 5;
};

// Fits every continuous distribution; sorted by RSS (best first). Failed fits are last.
std::vector<DistFit> fitDistributions(const std::vector<double>& data, const FitOptions& opt = {});
DistPtr makeFitted(const DistFit& f);

// ---------------------------------------------------------------- time series
struct TsFit {
    std::string name;      // AR(1), MA(1), ARMA(1,1), ARCH(1), Wiener process
    std::string formula;
    TimeSeries ts;
    double aic = 0;        // log(variance) + 2k/n
    double rss = 0;
    double theilU = 0;
    std::vector<double> fitted;   // one-step-ahead estimates of the history
    bool ok = false;
    std::string error;
};
std::vector<TsFit> fitTimeSeries(const std::vector<double>& y);  // sorted by AIC

// ---------------------------------------------------------------- Holt-Winters
struct HoltWinters {
    bool multiplicative = false;
    int season = 12;
    double alpha = 0.3, beta = 0.1, gamma = 0.1;
    double level = 0, trend = 0;
    std::vector<double> seasonal;  // last `season` seasonal factors, oldest first
    std::vector<double> fitted;    // one-step-ahead fitted values of the history
    double sse = 0, aic = 0, sigma = 0, theilU = 0;
    bool ok = false;
    std::vector<double> forecast(int h) const;  // point forecasts 1..h
    // Percentile forecasts by simulation with normal errors (n paths).
    std::vector<double> forecastPercentile(int h, double p, int n, std::mt19937_64& rng) const;
};
HoltWinters fitHoltWinters(const std::vector<double>& y, int season, bool multiplicative);
// Best of additive / multiplicative by AIC (selection 0) or SSE (selection 1).
HoltWinters bestHoltWinters(const std::vector<double>& y, int season, int selection);

// Forecast of a stochastic process: percentile p of `n` simulated paths, `h` steps ahead,
// starting after the last observation.
std::vector<double> forecastTimeSeries(const TimeSeries& ts, const std::vector<double>& history, int h,
                                       double p, int n, std::mt19937_64& rng);
// Re-anchor a fitted process at the last observed value.
TimeSeries anchorAt(const TimeSeries& ts, double last);

// Theil's U2: forecast error relative to the naive no-change forecast.
double theilU(const std::vector<double>& y, const std::vector<double>& fitted);

std::string fmtNum(double v);  // compact number for generated formulas

} // namespace mc
