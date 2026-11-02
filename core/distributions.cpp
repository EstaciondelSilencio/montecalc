// MonteCalc - probability distributions
#include "distributions.h"
#include "specfun.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace mc {

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

void require(bool ok, const char* msg) {
    if (!ok) throw std::invalid_argument(msg);
}

double clampU(double u) {
    const double eps = 1e-15;
    return std::min(std::max(u, eps), 1.0 - eps);
}

// ---- generic continuous helper based on lambdas ----
class Continuous : public Distribution {
public:
    using Fn = std::function<double(double)>;
    Continuous(Fn cdf, Fn q, double mean) : cdf_(std::move(cdf)), q_(std::move(q)), mean_(mean) {}
    double cdf(double x) const override { return cdf_(x); }
    double quantile(double u) const override { return q_(clampU(u)); }
    double mean() const override { return mean_; }

private:
    Fn cdf_, q_;
    double mean_;
};

// ---- generic discrete helper: integer support [lo, hi] (hi may be inf) ----
class Discrete : public Distribution {
public:
    using Fn = std::function<double(double)>;
    Discrete(Fn cdfInt, double lo, double hi, double mean)
        : cdf_(std::move(cdfInt)), lo_(lo), hi_(hi), mean_(mean) {}
    double cdf(double x) const override {
        if (x < lo_) return 0.0;
        double k = std::floor(x);
        if (k >= hi_) return 1.0;
        return cdf_(k);
    }
    double quantile(double u) const override {
        u = clampU(u);
        // exponential search for an upper bracket, then binary search on integers
        double lo = lo_, hi;
        if (cdf(lo) >= u) return lo;
        if (std::isfinite(hi_)) {
            hi = hi_;
        } else {
            double step = 1;
            hi = lo + step;
            while (cdf(hi) < u) { lo = hi; step *= 2; hi = lo + step; }
        }
        // invariant: cdf(lo) < u <= cdf(hi)
        while (hi - lo > 1) {
            double mid = std::floor((lo + hi) / 2);
            if (cdf(mid) < u) lo = mid; else hi = mid;
        }
        return hi;
    }
    double mean() const override { return mean_; }
    bool isDiscrete() const override { return true; }

private:
    Fn cdf_;
    double lo_, hi_, mean_;
};

double lchoose(double n, double k) {
    return std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
}

} // namespace

bool Bounds::active() const { return std::isfinite(lo) || std::isfinite(hi); }

// ======================= Continuous ======================================

DistPtr makeUniform(double a, double b) {
    require(b > a, "Uniform: max must be greater than min");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= a ? 0.0 : x >= b ? 1.0 : (x - a) / (b - a); },
        [=](double u) { return a + (b - a) * u; }, (a + b) / 2);
}

DistPtr makeTriangular(double a, double m, double b) {
    require(a <= m && m <= b && a < b, "Triangular: requires min <= mode <= max and min < max");
    double fc = (m - a) / (b - a);
    return std::make_unique<Continuous>(
        [=](double x) {
            if (x <= a) return 0.0;
            if (x >= b) return 1.0;
            if (x <= m) return (x - a) * (x - a) / ((b - a) * (m - a));
            return 1 - (b - x) * (b - x) / ((b - a) * (b - m));
        },
        [=](double u) {
            if (u < fc) return a + std::sqrt(u * (b - a) * (m - a));
            return b - std::sqrt((1 - u) * (b - a) * (b - m));
        },
        (a + m + b) / 3);
}

DistPtr makeBeta(double al, double be, double a, double b) {
    require(al > 0 && be > 0, "Beta: alpha and beta must be positive");
    require(b > a, "Beta: max must be greater than min");
    auto c = [=](double x) {
        if (x <= a) return 0.0;
        if (x >= b) return 1.0;
        return betaI(al, be, (x - a) / (b - a));
    };
    return std::make_unique<Continuous>(
        c,
        [=](double u) {
            double z = invertCdf([=](double t) { return betaI(al, be, t); }, u, 0.0, 1.0);
            return a + (b - a) * z;
        },
        a + (b - a) * al / (al + be));
}

DistPtr makePert(double a, double m, double b) {
    require(a <= m && m <= b && a < b, "PERT: requires min <= mode <= max and min < max");
    double al = 1 + 4 * (m - a) / (b - a);
    double be = 1 + 4 * (b - m) / (b - a);
    return makeBeta(al, be, a, b);
}

DistPtr makeNormal(double mu, double sd) {
    require(sd > 0, "Normal: standard deviation must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return normCdf((x - mu) / sd); },
        [=](double u) { return mu + sd * normInv(u); }, mu);
}

DistPtr makeLognormal(double mean, double sd) {
    require(mean > 0 && sd > 0, "Lognormal: mean and standard deviation must be positive");
    double s2 = std::log(1 + sd * sd / (mean * mean));
    double s = std::sqrt(s2), mu = std::log(mean) - s2 / 2;
    return std::make_unique<Continuous>(
        [=](double x) { return x <= 0 ? 0.0 : normCdf((std::log(x) - mu) / s); },
        [=](double u) { return std::exp(mu + s * normInv(u)); }, mean);
}

DistPtr makeGumbel(double loc, double scale) {
    require(scale > 0, "Gumbel: scale must be positive");
    const double euler = 0.5772156649015329;
    return std::make_unique<Continuous>(
        [=](double x) { return std::exp(-std::exp(-(x - loc) / scale)); },
        [=](double u) { return loc - scale * std::log(-std::log(u)); }, loc + euler * scale);
}

DistPtr makeGamma(double k, double theta) {
    require(k > 0 && theta > 0, "Gamma: shape and scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= 0 ? 0.0 : gammaP(k, x / theta); },
        [=](double u) {
            return theta * invertCdf([=](double t) { return gammaP(k, t); }, u, 0.0, kInf);
        },
        k * theta);
}

DistPtr makeExponential(double rate) {
    require(rate > 0, "Exponential: rate must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= 0 ? 0.0 : 1 - std::exp(-rate * x); },
        [=](double u) { return -std::log1p(-u) / rate; }, 1 / rate);
}

DistPtr makeWeibull(double k, double lambda) {
    require(k > 0 && lambda > 0, "Weibull: shape and scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= 0 ? 0.0 : 1 - std::exp(-std::pow(x / lambda, k)); },
        [=](double u) { return lambda * std::pow(-std::log1p(-u), 1 / k); },
        lambda * std::tgamma(1 + 1 / k));
}

DistPtr makeChiSquare(double df) {
    require(df > 0, "Chi-square: degrees of freedom must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= 0 ? 0.0 : chi2Cdf(x, df); },
        [=](double u) {
            return 2 * invertCdf([=](double t) { return gammaP(df / 2, t); }, u, 0.0, kInf);
        },
        df);
}

DistPtr makeStudentT(double df) {
    require(df > 0, "Student-t: degrees of freedom must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return tCdf(x, df); },
        [=](double u) {
            if (u == 0.5) return 0.0;
            // symmetric: invert on the positive half for accuracy
            double p = u > 0.5 ? u : 1 - u;
            double x = invertCdf([=](double t) { return tCdf(t, df); }, p, 0.0, kInf);
            return u > 0.5 ? x : -x;
        },
        df > 1 ? 0.0 : kNaN);
}

DistPtr makePareto(double alpha, double xm) {
    require(alpha > 0 && xm > 0, "Pareto: shape and scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= xm ? 0.0 : 1 - std::pow(xm / x, alpha); },
        [=](double u) { return xm * std::pow(1 - u, -1 / alpha); },
        alpha > 1 ? alpha * xm / (alpha - 1) : kNaN);
}

DistPtr makeF(double d1, double d2) {
    require(d1 > 0 && d2 > 0, "F: degrees of freedom must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return fCdf(x, d1, d2); },
        [=](double u) {
            return invertCdf([=](double t) { return fCdf(t, d1, d2); }, u, 0.0, kInf);
        },
        d2 > 2 ? d2 / (d2 - 2) : kNaN);
}

DistPtr makeArcsine(double a, double b) {
    require(b > a, "Arcsine: max must be greater than min");
    return std::make_unique<Continuous>(
        [=](double x) {
            if (x <= a) return 0.0;
            if (x >= b) return 1.0;
            return 2 / kPi * std::asin(std::sqrt((x - a) / (b - a)));
        },
        [=](double u) { double s = std::sin(kPi * u / 2); return a + (b - a) * s * s; },
        (a + b) / 2);
}

DistPtr makeCauchy(double x0, double g) {
    require(g > 0, "Cauchy: scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return 0.5 + std::atan((x - x0) / g) / kPi; },
        [=](double u) { return x0 + g * std::tan(kPi * (u - 0.5)); }, kNaN);
}

DistPtr makeFrechet(double alpha, double s, double m) {
    require(alpha > 0 && s > 0, "Frechet: shape and scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return x <= m ? 0.0 : std::exp(-std::pow((x - m) / s, -alpha)); },
        [=](double u) { return m + s * std::pow(-std::log(u), -1 / alpha); },
        alpha > 1 ? m + s * std::tgamma(1 - 1 / alpha) : kNaN);
}

DistPtr makeLogistic(double mu, double s) {
    require(s > 0, "Logistic: scale must be positive");
    return std::make_unique<Continuous>(
        [=](double x) { return 1 / (1 + std::exp(-(x - mu) / s)); },
        [=](double u) { return mu + s * std::log(u / (1 - u)); }, mu);
}

DistPtr makePiecewiseLinear(std::vector<double> x, std::vector<double> p) {
    require(x.size() >= 2 && x.size() == p.size(), "Custom: need at least two points");
    for (size_t i = 1; i < x.size(); ++i)
        require(x[i] >= x[i - 1] && p[i] >= p[i - 1], "Custom: points must be ascending");
    require(p.back() > p.front(), "Custom: probabilities must increase");
    // normalise to [0,1]
    double p0 = p.front(), p1 = p.back();
    for (auto& v : p) v = (v - p0) / (p1 - p0);
    double mean = 0;
    for (size_t i = 1; i < x.size(); ++i) mean += (p[i] - p[i - 1]) * (x[i] + x[i - 1]) / 2;
    auto cdf = [x, p](double v) {
        if (v <= x.front()) return 0.0;
        if (v >= x.back()) return 1.0;
        size_t i = std::upper_bound(x.begin(), x.end(), v) - x.begin();
        double dx = x[i] - x[i - 1];
        if (dx <= 0) return p[i];
        return p[i - 1] + (p[i] - p[i - 1]) * (v - x[i - 1]) / dx;
    };
    auto q = [x, p](double u) {
        size_t i = std::lower_bound(p.begin(), p.end(), u) - p.begin();
        if (i == 0) return x.front();
        if (i >= p.size()) return x.back();
        double dp = p[i] - p[i - 1];
        if (dp <= 0) return x[i];
        return x[i - 1] + (x[i] - x[i - 1]) * (u - p[i - 1]) / dp;
    };
    return std::make_unique<Continuous>(cdf, q, mean);
}

DistPtr makeHistogram(const std::vector<double>& mins, const std::vector<double>& maxs,
                      const std::vector<double>& counts) {
    require(!mins.empty() && mins.size() == maxs.size() && mins.size() == counts.size(),
            "Histogram: Min, Max and Number columns must have equal length");
    double total = std::accumulate(counts.begin(), counts.end(), 0.0);
    require(total > 0, "Histogram: total count must be positive");
    std::vector<double> x{mins.front()}, p{0.0};
    double cum = 0;
    for (size_t i = 0; i < mins.size(); ++i) {
        require(maxs[i] >= mins[i] && counts[i] >= 0, "Histogram: invalid bin");
        if (i > 0 && mins[i] > x.back()) { x.push_back(mins[i]); p.push_back(cum / total); }
        cum += counts[i];
        x.push_back(maxs[i]);
        p.push_back(cum / total);
    }
    return makePiecewiseLinear(std::move(x), std::move(p));
}

DistPtr makeEmpirical(std::vector<double> data, int bins) {
    data.erase(std::remove_if(data.begin(), data.end(), [](double v) { return !std::isfinite(v); }),
               data.end());
    require(data.size() >= 2, "Custom: need at least two data points");
    std::sort(data.begin(), data.end());
    require(data.back() > data.front(), "Custom: data must not be constant");
    if (bins <= 0) {
        std::vector<double> p(data.size());
        for (size_t i = 0; i < data.size(); ++i) p[i] = double(i) / (data.size() - 1);
        return makePiecewiseLinear(std::move(data), std::move(p));
    }
    double lo = data.front(), hi = data.back(), w = (hi - lo) / bins;
    std::vector<double> mins(bins), maxs(bins), counts(bins, 0.0);
    for (int i = 0; i < bins; ++i) { mins[i] = lo + i * w; maxs[i] = lo + (i + 1) * w; }
    for (double v : data) {
        int i = std::min(bins - 1, int((v - lo) / w));
        counts[i] += 1;
    }
    return makeHistogram(mins, maxs, counts);
}

// ======================= Discrete ========================================

DistPtr makeDiscreteUniform(double a, double b) {
    a = std::ceil(a);
    b = std::floor(b);
    require(b >= a, "Discrete uniform: max must be >= min");
    double n = b - a + 1;
    return std::make_unique<Discrete>([=](double k) { return (k - a + 1) / n; }, a, b, (a + b) / 2);
}

DistPtr makeBernoulli(double p) {
    require(p >= 0 && p <= 1, "Bernoulli: p must be in [0,1]");
    return std::make_unique<Discrete>([=](double) { return 1 - p; }, 0, 1, p);
}

DistPtr makeBinomial(double n, double p) {
    n = std::round(n);
    require(n >= 0 && p >= 0 && p <= 1, "Binomial: requires n >= 0 and p in [0,1]");
    return std::make_unique<Discrete>(
        [=](double k) {
            if (p == 0) return 1.0;
            if (p == 1) return k >= n ? 1.0 : 0.0;
            return betaI(n - k, k + 1, 1 - p);
        },
        0, n, n * p);
}

DistPtr makePoisson(double lambda) {
    require(lambda > 0, "Poisson: lambda must be positive");
    return std::make_unique<Discrete>([=](double k) { return 1 - gammaP(k + 1, lambda); }, 0, kInf,
                                      lambda);
}

DistPtr makeNegBinomial(double r, double p) {
    require(r > 0 && p > 0 && p <= 1, "Negative binomial: requires r > 0 and p in (0,1]");
    return std::make_unique<Discrete>(
        [=](double k) { return p == 1 ? 1.0 : betaI(r, k + 1, p); }, 0, kInf, r * (1 - p) / p);
}

DistPtr makeGeometric(double p) {
    require(p > 0 && p <= 1, "Geometric: p must be in (0,1]");
    return std::make_unique<Discrete>(
        [=](double k) { return 1 - std::pow(1 - p, k + 1); }, 0, kInf, (1 - p) / p);
}

DistPtr makeHypergeometric(double N, double K, double n) {
    N = std::round(N); K = std::round(K); n = std::round(n);
    require(N > 0 && K >= 0 && K <= N && n >= 0 && n <= N,
            "Hypergeometric: requires 0 <= successes <= population and 0 <= draws <= population");
    double lo = std::max(0.0, n + K - N), hi = std::min(n, K);
    // precompute cumulative table
    std::vector<double> cum;
    double c = 0;
    for (double k = lo; k <= hi; ++k) {
        c += std::exp(lchoose(K, k) + lchoose(N - K, n - k) - lchoose(N, n));
        cum.push_back(c);
    }
    for (auto& v : cum) v /= c;
    return std::make_unique<Discrete>(
        [=](double k) { return cum[size_t(k - lo)]; }, lo, hi, n * K / N);
}

DistPtr makeDiscreteCustom(std::vector<double> values, std::vector<double> probs) {
    require(!values.empty() && values.size() == probs.size(),
            "Discrete custom: values and probabilities must have equal length");
    std::vector<size_t> idx(values.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return values[a] < values[b]; });
    std::vector<double> x, cum;
    double total = 0, mean = 0;
    for (size_t i : idx) {
        require(probs[i] >= 0, "Discrete custom: probabilities must be >= 0");
        total += probs[i];
    }
    require(total > 0, "Discrete custom: probabilities must sum to a positive number");
    double c = 0;
    for (size_t i : idx) {
        c += probs[i] / total;
        mean += values[i] * probs[i] / total;
        x.push_back(values[i]);
        cum.push_back(c);
    }
    cum.back() = 1.0;
    class DiscreteTable : public Distribution {
    public:
        DiscreteTable(std::vector<double> x, std::vector<double> c, double m)
            : x_(std::move(x)), c_(std::move(c)), m_(m) {}
        double cdf(double v) const override {
            size_t i = std::upper_bound(x_.begin(), x_.end(), v) - x_.begin();
            return i == 0 ? 0.0 : c_[i - 1];
        }
        double quantile(double u) const override {
            size_t i = std::lower_bound(c_.begin(), c_.end(), clampU(u)) - c_.begin();
            return x_[std::min(i, x_.size() - 1)];
        }
        double mean() const override { return m_; }
        bool isDiscrete() const override { return true; }

    private:
        std::vector<double> x_, c_;
        double m_;
    };
    return std::make_unique<DiscreteTable>(std::move(x), std::move(cum), mean);
}

// ======================= Truncation ======================================

static void boundedRange(const Distribution& d, const Bounds& b, double& flo, double& fhi) {
    flo = 0.0;
    fhi = 1.0;
    if (std::isfinite(b.lo)) {
        // P(X < lo)
        flo = d.isDiscrete() ? d.cdf(std::ceil(b.lo) - 1) : d.cdf(b.lo);
    }
    if (std::isfinite(b.hi)) fhi = d.cdf(b.hi);
    if (!(fhi > flo))
        throw std::invalid_argument("Bounds exclude the whole distribution (lower >= upper?)");
}

double sampleBounded(const Distribution& d, double u, const Bounds& b) {
    if (!b.active()) return d.quantile(u);
    double flo, fhi;
    boundedRange(d, b, flo, fhi);
    double v = d.quantile(flo + clampU(u) * (fhi - flo));
    return std::min(std::max(v, b.lo), b.hi);
}

double expectedValue(const Distribution& d, const Bounds& b) {
    if (!b.active()) {
        double m = d.mean();
        if (std::isfinite(m)) return m;
        return d.quantile(0.5);
    }
    // midpoint rule on the quantile function
    const int n = 2000;
    double s = 0;
    for (int i = 0; i < n; ++i) s += sampleBounded(d, (i + 0.5) / n, b);
    return s / n;
}

} // namespace mc
