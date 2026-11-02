// MonteCalc - statistics
#include "stats.h"
#include "specfun.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace mc::stats {

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

Vec finite(const Vec& v) {
    Vec r;
    r.reserve(v.size());
    for (double x : v)
        if (std::isfinite(x)) r.push_back(x);
    return r;
}

Vec sorted(const Vec& v) {
    Vec r = finite(v);
    std::sort(r.begin(), r.end());
    return r;
}

static void pairs(const Vec& x, const Vec& y, Vec& a, Vec& b) {
    size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; ++i)
        if (std::isfinite(x[i]) && std::isfinite(y[i])) { a.push_back(x[i]); b.push_back(y[i]); }
}

double count(const Vec& v) { return double(finite(v).size()); }

double sum(const Vec& v) {
    double s = 0;
    for (double x : v) if (std::isfinite(x)) s += x;
    return s;
}

double mean(const Vec& v) {
    double s = 0, n = 0;
    for (double x : v) if (std::isfinite(x)) { s += x; ++n; }
    return n > 0 ? s / n : kNaN;
}

double min(const Vec& v) {
    Vec f = finite(v);
    return f.empty() ? kNaN : *std::min_element(f.begin(), f.end());
}

double max(const Vec& v) {
    Vec f = finite(v);
    return f.empty() ? kNaN : *std::max_element(f.begin(), f.end());
}

double variance(const Vec& v) {
    Vec f = finite(v);
    if (f.size() < 2) return kNaN;
    double m = mean(f), s = 0;
    for (double x : f) s += (x - m) * (x - m);
    return s / (f.size() - 1);
}

double stdev(const Vec& v) { return std::sqrt(variance(v)); }

double skewness(const Vec& v) {
    Vec f = finite(v);
    double n = f.size();
    if (n < 3) return kNaN;
    double m = mean(f), s = stdev(f), acc = 0;
    if (!(s > 0)) return 0;
    for (double x : f) acc += std::pow((x - m) / s, 3);
    return n / ((n - 1) * (n - 2)) * acc;
}

double kurtosis(const Vec& v) {
    Vec f = finite(v);
    double n = f.size();
    if (n < 4) return kNaN;
    double m = mean(f), s = stdev(f), acc = 0;
    if (!(s > 0)) return 0;
    for (double x : f) acc += std::pow((x - m) / s, 4);
    return n * (n + 1) / ((n - 1) * (n - 2) * (n - 3)) * acc -
           3 * (n - 1) * (n - 1) / ((n - 2) * (n - 3));
}

double cv(const Vec& v) { return stdev(v) / mean(v); }

double percentile(const Vec& v, double p) {
    if (p < 0 || p > 1) throw std::invalid_argument("percentile must be in [0,1]");
    Vec s = sorted(v);
    if (s.empty()) return kNaN;
    double h = (s.size() - 1) * p;
    size_t i = size_t(std::floor(h));
    if (i + 1 >= s.size()) return s.back();
    return s[i] + (h - i) * (s[i + 1] - s[i]);
}

double percentRank(const Vec& v, double x) {
    Vec s = sorted(v);
    if (s.empty()) return kNaN;
    return double(std::upper_bound(s.begin(), s.end(), x) - s.begin()) / s.size();
}

double probBetween(const Vec& v, double lo, double hi) {
    Vec f = finite(v);
    if (f.empty()) return kNaN;
    double c = 0;
    for (double x : f) if (x >= lo && x <= hi) ++c;
    return c / f.size();
}

double semivariance(const Vec& v) { return semivarianceBelow(v, mean(v)); }

double semivarianceBelow(const Vec& v, double limit) {
    Vec f = finite(v);
    if (f.empty()) return kNaN;
    double s = 0;
    for (double x : f) if (x < limit) s += (x - limit) * (x - limit);
    return s / f.size();
}

double semivarianceAbove(const Vec& v, double limit) {
    Vec f = finite(v);
    if (f.empty()) return kNaN;
    double s = 0;
    for (double x : f) if (x > limit) s += (x - limit) * (x - limit);
    return s / f.size();
}

double expectedGain(const Vec& v) {
    Vec g;
    for (double x : v) if (std::isfinite(x) && x > 0) g.push_back(x);
    return g.empty() ? 0.0 : mean(g);
}

double expectedGainVariance(const Vec& v) {
    Vec g;
    for (double x : v) if (std::isfinite(x) && x > 0) g.push_back(x);
    return g.size() < 2 ? 0.0 : variance(g);
}

double expectedLoss(const Vec& v) {
    Vec g;
    for (double x : v) if (std::isfinite(x) && x < 0) g.push_back(x);
    return g.empty() ? 0.0 : mean(g);
}

double expectedShortfall(const Vec& v, double p) {
    double var = percentile(v, p);
    Vec t;
    for (double x : v) {
        if (!std::isfinite(x)) continue;
        if (p <= 0.5 ? x <= var : x >= var) t.push_back(x);
    }
    return t.empty() ? var : mean(t);
}

Histogram histogram(const Vec& v, int bins) {
    if (bins < 1) bins = 1;
    Vec f = finite(v);
    Histogram h;
    if (f.empty()) return h;
    double lo = *std::min_element(f.begin(), f.end());
    double hi = *std::max_element(f.begin(), f.end());
    if (hi == lo) { hi = lo + 0.5; lo -= 0.5; }
    double w = (hi - lo) / bins;
    h.lo.resize(bins); h.hi.resize(bins); h.count.assign(bins, 0);
    for (int i = 0; i < bins; ++i) { h.lo[i] = lo + i * w; h.hi[i] = lo + (i + 1) * w; }
    for (double x : f) h.count[std::min(bins - 1, int((x - lo) / w))] += 1;
    return h;
}

double mode(const Vec& v, int bins) {
    Histogram h = histogram(v, bins);
    if (h.count.empty()) return kNaN;
    size_t i = std::max_element(h.count.begin(), h.count.end()) - h.count.begin();
    return (h.lo[i] + h.hi[i]) / 2;
}

double density(const Vec& v, double x) {
    Vec f = finite(v);
    if (f.size() < 2) return kNaN;
    double s = stdev(f);
    double iqr = percentile(f, 0.75) - percentile(f, 0.25);
    double sig = std::min(s, iqr / 1.34);
    if (!(sig > 0)) sig = s > 0 ? s : 1;
    double h = 0.9 * sig * std::pow(double(f.size()), -0.2);
    double acc = 0;
    for (double xi : f) { double z = (x - xi) / h; acc += std::exp(-0.5 * z * z); }
    return acc / (f.size() * h * std::sqrt(2 * kPi));
}

double pearson(const Vec& x, const Vec& y) {
    Vec a, b;
    pairs(x, y, a, b);
    if (a.size() < 2) return kNaN;
    double ma = mean(a), mb = mean(b), sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    if (saa <= 0 || sbb <= 0) return 0;
    return sab / std::sqrt(saa * sbb);
}

Vec ranks(const Vec& v) {
    size_t n = v.size();
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
    Vec r(n);
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j + 1 < n && v[idx[j + 1]] == v[idx[i]]) ++j;
        double avg = (i + j) / 2.0 + 1;
        for (size_t k = i; k <= j; ++k) r[idx[k]] = avg;
        i = j + 1;
    }
    return r;
}

double spearman(const Vec& x, const Vec& y) {
    Vec a, b;
    pairs(x, y, a, b);
    if (a.size() < 2) return kNaN;
    return pearson(ranks(a), ranks(b));
}

double regressionSlope(const Vec& x, const Vec& y) {
    Vec a, b;
    pairs(x, y, a, b);
    if (a.size() < 2) return kNaN;
    double ma = mean(a), mb = mean(b), sab = 0, saa = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
    }
    return saa > 0 ? sab / saa : kNaN;
}

double regressionIntercept(const Vec& x, const Vec& y) {
    Vec a, b;
    pairs(x, y, a, b);
    return mean(b) - regressionSlope(a, b) * mean(a);
}

double andersonDarling(const Vec& v, bool& normal) {
    Vec s = sorted(v);
    double n = s.size();
    normal = false;
    if (n < 8) return kNaN;
    double m = mean(s), sd = stdev(s);
    if (!(sd > 0)) return kNaN;
    double acc = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        double fi = std::min(std::max(normCdf((s[i] - m) / sd), 1e-15), 1 - 1e-15);
        double fj = std::min(std::max(normCdf((s[s.size() - 1 - i] - m) / sd), 1e-15), 1 - 1e-15);
        acc += (2.0 * (i + 1) - 1) * (std::log(fi) + std::log(1 - fj));
    }
    double a2 = -n - acc / n;
    double a2s = a2 * (1 + 0.75 / n + 2.25 / (n * n));
    normal = a2s < 0.752;
    return a2s;
}

Interval bootstrap(const Vec& v, int samples, double confidence, double p, std::mt19937_64& rng) {
    Vec f = finite(v);
    if (f.empty() || samples < 2) return {kNaN, kNaN, kNaN};
    bool usePct = p > 0 && p < 1;
    std::uniform_int_distribution<size_t> pick(0, f.size() - 1);
    Vec est(samples);
    Vec draw(f.size());
    for (int s = 0; s < samples; ++s) {
        for (auto& d : draw) d = f[pick(rng)];
        est[s] = usePct ? percentile(draw, p) : mean(draw);
    }
    double a = (1 - confidence) / 2;
    return {usePct ? percentile(f, p) : mean(f), percentile(est, a), percentile(est, 1 - a)};
}

bool compare(double a, const std::string& op0, double b) {
    std::string op;
    for (char c : op0) if (c != ' ') op += c;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    if (op == "=" || op == "==") return a == b;
    if (op == "<>" || op == "!=") return a != b;
    throw std::invalid_argument("comparison operator must be one of < <= > >= = <>");
}

double conditionalProbability(const Vec& a, const std::string& op1, double va, const Vec& b,
                              const std::string& op2, double vb) {
    size_t n = std::min(a.size(), b.size());
    double cb = 0, cab = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i])) continue;
        if (compare(b[i], op2, vb)) {
            ++cb;
            if (compare(a[i], op1, va)) ++cab;
        }
    }
    return cb > 0 ? cab / cb : kNaN;
}

Vec dynamicSensitivity(const Vec& input, const Vec& output, int bins) {
    Vec a, b;
    pairs(input, output, a, b);
    if (a.empty() || bins < 1) return {kNaN, kNaN, kNaN};
    std::vector<size_t> idx(a.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t i, size_t j) { return a[i] < a[j]; });
    Vec means;
    size_t n = a.size();
    for (int g = 0; g < bins; ++g) {
        size_t s = n * g / bins, e = n * (g + 1) / bins;
        if (e <= s) continue;
        double acc = 0;
        for (size_t k = s; k < e; ++k) acc += b[idx[k]];
        means.push_back(acc / (e - s));
    }
    double lo = *std::min_element(means.begin(), means.end());
    double hi = *std::max_element(means.begin(), means.end());
    return {hi - lo, lo, hi};
}

} // namespace mc::stats
