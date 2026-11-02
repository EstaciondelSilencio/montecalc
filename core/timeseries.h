// MonteCalc - stochastic time-series processes
#pragma once

#include <random>
#include <vector>

namespace mc {

enum class TsType { AR1, MA1, ARMA11, ARCH1, GBM };

struct TimeSeries {
    TsType type;
    // AR1:    mu, phi, sigma, y0
    // MA1:    mu, theta, sigma
    // ARMA11: mu, phi, theta, sigma, y0
    // ARCH1:  mu, a0, a1, y0
    // GBM:    s0, drift, volatility, dt
    std::vector<double> p;

    void validate() const;
    // Value after `periods` steps. The first shock uses the (possibly correlated)
    // uniform u; later shocks are drawn independently from rng.
    double simulate(double u, int periods, std::mt19937_64& rng) const;
    // Expected value after `periods` steps.
    double expected(int periods) const;
    // Full path of `periods` values (used by the forecasting tools / tests).
    std::vector<double> path(int periods, std::mt19937_64& rng) const;
};

} // namespace mc
