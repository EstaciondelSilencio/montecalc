// MonteCalc - probability distributions
//
// Every distribution is sampled by inversion (quantile of a uniform number).
// That lets the engine feed correlated uniforms (copulas) into any distribution
// and makes truncation ("lower"/"upper" bounds) exact.
#pragma once

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace mc {

class Distribution {
public:
    virtual ~Distribution() = default;
    // P(X <= x)
    virtual double cdf(double x) const = 0;
    // Smallest x with cdf(x) >= u, u in (0,1)
    virtual double quantile(double u) const = 0;
    // Expected value; NaN if undefined (e.g. Cauchy)
    virtual double mean() const = 0;
    virtual bool isDiscrete() const { return false; }
};

using DistPtr = std::unique_ptr<Distribution>;

struct Bounds {
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    bool active() const;
};

// --- Continuous -----------------------------------------------------------
DistPtr makeUniform(double min, double max);
DistPtr makeTriangular(double min, double mode, double max);
DistPtr makePert(double min, double mode, double max);
DistPtr makeNormal(double mean, double sd);
DistPtr makeLognormal(double mean, double sd);          // mean/sd of the variable itself
DistPtr makeBeta(double alpha, double beta, double min, double max);
DistPtr makeGumbel(double location, double scale);     // maximum-extreme-value
DistPtr makeGamma(double shape, double scale);
DistPtr makeExponential(double rate);
DistPtr makeWeibull(double shape, double scale);
DistPtr makeChiSquare(double df);
DistPtr makeStudentT(double df);
DistPtr makePareto(double shape, double scale);
DistPtr makeF(double d1, double d2);
DistPtr makeArcsine(double min, double max);
DistPtr makeCauchy(double location, double scale);
DistPtr makeFrechet(double shape, double scale, double location);
DistPtr makeLogistic(double location, double scale);
// X + shift (MC FLO "Shift" parameter of LogNormal, Gamma, Weibull, Exponential, ...)
DistPtr makeShifted(DistPtr d, double shift);
// Piecewise-linear CDF through (x[i], p[i]); x ascending, p ascending from 0 to 1.
DistPtr makePiecewiseLinear(std::vector<double> x, std::vector<double> p);
// Histogram from raw data (bins >= 1) or the empirical CDF of the data (bins == 0).
DistPtr makeEmpirical(std::vector<double> data, int bins);
// Histogram given as bin lower edges, upper edges and counts.
DistPtr makeHistogram(const std::vector<double>& mins, const std::vector<double>& maxs,
                      const std::vector<double>& counts);

// --- Discrete -------------------------------------------------------------
DistPtr makeDiscreteUniform(double min, double max);
DistPtr makeBernoulli(double p);
DistPtr makeBinomial(double n, double p);
DistPtr makePoisson(double lambda);
DistPtr makeNegBinomial(double r, double p);   // failures before the r-th success
DistPtr makeGeometric(double p);               // failures before the first success
DistPtr makeHypergeometric(double population, double successes, double draws);
DistPtr makeDiscreteCustom(std::vector<double> values, std::vector<double> probs);

// Sample with optional truncation. u in (0,1).
double sampleBounded(const Distribution& d, double u, const Bounds& b);
// Expected value under truncation (numerical when bounds are active or the
// mean is undefined; falls back to the median).
double expectedValue(const Distribution& d, const Bounds& b);

} // namespace mc
