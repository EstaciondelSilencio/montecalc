// MonteCalc - statistics over simulated (or user-provided) samples.
// All functions ignore non-finite values (NaN marks "not evaluated" iterations).
#pragma once

#include <random>
#include <string>
#include <vector>

namespace mc::stats {

using Vec = std::vector<double>;

Vec finite(const Vec& v);
Vec sorted(const Vec& v);

double count(const Vec& v);
double sum(const Vec& v);
double mean(const Vec& v);
double min(const Vec& v);
double max(const Vec& v);
double variance(const Vec& v);        // sample variance (n-1)
double stdev(const Vec& v);           // sample standard deviation
double skewness(const Vec& v);        // sample skewness (as spreadsheet SKEW)
double kurtosis(const Vec& v);        // sample excess kurtosis (as spreadsheet KURT)
double cv(const Vec& v);              // coefficient of variation stdev/mean
double percentile(const Vec& v, double p);    // inclusive, linear interpolation
double percentRank(const Vec& v, double x);   // share of values <= x
double probBetween(const Vec& v, double lo, double hi);
double semivariance(const Vec& v);            // below the mean
double semivarianceBelow(const Vec& v, double limit);
double semivarianceAbove(const Vec& v, double limit);
double expectedGain(const Vec& v);            // mean of values > 0
double expectedGainVariance(const Vec& v);    // variance of values > 0
double expectedLoss(const Vec& v);            // mean of values < 0
double expectedShortfall(const Vec& v, double p); // tail mean beyond the p-quantile
double mode(const Vec& v, int bins);
double density(const Vec& v, double x);       // Gaussian kernel density estimate

struct Histogram {
    Vec lo, hi, count;
};
Histogram histogram(const Vec& v, int bins);

// Paired statistics (pairs with a non-finite member are dropped)
double pearson(const Vec& x, const Vec& y);
double spearman(const Vec& x, const Vec& y);
double regressionSlope(const Vec& x, const Vec& y);
double regressionIntercept(const Vec& x, const Vec& y);
Vec ranks(const Vec& v);  // average ranks, 1-based

// Anderson-Darling normality test. Returns A^2* (adjusted); `normal` set to
// true when the hypothesis of normality is not rejected at the 5% level.
double andersonDarling(const Vec& v, bool& normal);

// Bootstrap confidence interval of the mean (or of a percentile if p in (0,1)).
struct Interval { double estimate, lower, upper; };
Interval bootstrap(const Vec& v, int samples, double confidence, double p, std::mt19937_64& rng);

// Comparison helper used by the conditional-probability functions:
// op is one of "<", "<=", ">", ">=", "=", "<>"
bool compare(double a, const std::string& op, double b);
// P(A | B) where A: a op1 va and B: b op2 vb
double conditionalProbability(const Vec& a, const std::string& op1, double va,
                              const Vec& b, const std::string& op2, double vb);

// Dynamic sensitivity: sort by input, split into `bins` groups, average the
// output per group. Returns {range, min, max} of the group means.
Vec dynamicSensitivity(const Vec& input, const Vec& output, int bins);

} // namespace mc::stats
