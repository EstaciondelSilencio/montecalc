// MonteCalc - Monte Carlo simulation for LibreOffice Calc
// Special mathematical functions used by the distributions and statistics.
#pragma once

#include <functional>

namespace mc {

constexpr double kPi = 3.14159265358979323846;

double normCdf(double x);                 // standard normal CDF
double normInv(double p);                 // standard normal quantile
double gammaP(double a, double x);        // regularized lower incomplete gamma P(a,x)
double betaI(double a, double b, double x); // regularized incomplete beta I_x(a,b)
double tCdf(double x, double df);         // Student-t CDF
double chi2Cdf(double x, double df);
double fCdf(double x, double d1, double d2);

// Find x in [lo,hi] with cdf(x) == p for a monotone non-decreasing cdf.
// The bracket is expanded automatically when needed (lo/hi may be +-inf hints).
double invertCdf(const std::function<double(double)>& cdf, double p,
                 double lo, double hi);

} // namespace mc
