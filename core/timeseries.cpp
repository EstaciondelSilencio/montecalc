// MonteCalc - stochastic time-series processes
#include "timeseries.h"
#include "specfun.h"

#include <cmath>
#include <stdexcept>

namespace mc {

static void need(bool ok, const char* m) {
    if (!ok) throw std::invalid_argument(m);
}

void TimeSeries::validate() const {
    size_t n[] = {4, 3, 5, 4, 4};
    need(p.size() == n[int(type)], "time series: wrong number of parameters");
    switch (type) {
    case TsType::AR1: need(p[2] > 0 && std::fabs(p[1]) < 1, "AR(1): requires |phi| < 1 and sigma > 0"); break;
    case TsType::MA1: need(p[2] > 0, "MA(1): sigma must be positive"); break;
    case TsType::ARMA11: need(p[3] > 0 && std::fabs(p[1]) < 1, "ARMA(1,1): requires |phi| < 1 and sigma > 0"); break;
    case TsType::ARCH1: need(p[1] > 0 && p[2] >= 0 && p[2] < 1, "ARCH(1): requires a0 > 0 and 0 <= a1 < 1"); break;
    case TsType::GBM: need(p[0] > 0 && p[2] >= 0 && p[3] > 0, "Wiener process: requires S0 > 0, volatility >= 0, dt > 0"); break;
    }
}

namespace {
struct Stepper {
    const TimeSeries& ts;
    double y, epsPrev = 0;
    explicit Stepper(const TimeSeries& t) : ts(t) {
        switch (t.type) {
        case TsType::AR1: y = t.p[3]; break;
        case TsType::MA1: y = t.p[0]; break;
        case TsType::ARMA11: y = t.p[4]; break;
        case TsType::ARCH1: y = t.p[3]; break;
        case TsType::GBM: y = t.p[0]; break;
        }
    }
    double step(double z, bool first, std::mt19937_64& rng) {
        const auto& p = ts.p;
        switch (ts.type) {
        case TsType::AR1:
            y = p[0] + p[1] * (y - p[0]) + p[2] * z;
            break;
        case TsType::MA1: {
            if (first) epsPrev = p[2] * std::normal_distribution<double>()(rng);
            double e = p[2] * z;
            y = p[0] + e + p[1] * epsPrev;
            epsPrev = e;
            break;
        }
        case TsType::ARMA11: {
            double e = p[3] * z;
            y = p[0] + p[1] * (y - p[0]) + e + p[2] * epsPrev;
            epsPrev = e;
            break;
        }
        case TsType::ARCH1: {
            double d = y - p[0];
            double s = std::sqrt(p[1] + p[2] * d * d);
            y = p[0] + s * z;
            break;
        }
        case TsType::GBM:
            y = y * std::exp((p[1] - 0.5 * p[2] * p[2]) * p[3] + p[2] * std::sqrt(p[3]) * z);
            break;
        }
        return y;
    }
};
} // namespace

double TimeSeries::simulate(double u, int periods, std::mt19937_64& rng) const {
    if (periods < 1) periods = 1;
    Stepper s(*this);
    std::normal_distribution<double> nd;
    double z0 = normInv(std::min(std::max(u, 1e-15), 1 - 1e-15));
    double y = s.step(z0, true, rng);
    for (int t = 1; t < periods; ++t) y = s.step(nd(rng), false, rng);
    return y;
}

std::vector<double> TimeSeries::path(int periods, std::mt19937_64& rng) const {
    Stepper s(*this);
    std::normal_distribution<double> nd;
    std::vector<double> out;
    for (int t = 0; t < periods; ++t) out.push_back(s.step(nd(rng), t == 0, rng));
    return out;
}

double TimeSeries::expected(int periods) const {
    if (periods < 1) periods = 1;
    switch (type) {
    case TsType::AR1: return p[0] + std::pow(p[1], periods) * (p[3] - p[0]);
    case TsType::MA1: return p[0];
    case TsType::ARMA11: return p[0] + std::pow(p[1], periods) * (p[4] - p[0]);
    case TsType::ARCH1: return p[0];
    case TsType::GBM: return p[0] * std::exp(p[1] * p[3] * periods);
    }
    return p[0];
}

} // namespace mc
