// MonteCalc - Bayesian statistics, kernel density estimation and regression tools
#include "bayes.h"
#include "specfun.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>

namespace mc::bayes {

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

// ============================================================ kernels

double silverman(const Vec& data) {
    Vec f = stats::finite(data);
    if (f.size() < 2) return 1.0;
    double s = stats::stdev(f);
    double iqr = stats::percentile(f, 0.75) - stats::percentile(f, 0.25);
    double sig = iqr > 0 ? std::min(s, iqr / 1.34) : s;
    if (!(sig > 0)) sig = std::fabs(f[0]) > 0 ? std::fabs(f[0]) * 0.01 : 1.0;
    return 0.9 * sig * std::pow(double(f.size()), -0.2);
}

double kdeDensity(const Vec& data, double bw, double x) {
    if (data.empty()) return 0;
    if (!(bw > 0)) bw = silverman(data);
    double acc = 0;
    for (double xi : data) {
        double z = (x - xi) / bw;
        acc += std::exp(-0.5 * z * z);
    }
    return acc / (data.size() * bw * std::sqrt(2 * kPi));
}

Vec kdeSample(const Vec& data0, int n, double bw, double adapt, std::mt19937_64& rng) {
    Vec data = stats::finite(data0);
    if (data.empty()) throw std::invalid_argument("no data");
    if (!(bw > 0)) bw = silverman(data);
    double lo = *std::min_element(data.begin(), data.end()), hi = *std::max_element(data.begin(), data.end());
    double r = hi - lo;
    lo -= adapt * r;
    hi += adapt * r;
    std::uniform_int_distribution<size_t> pick(0, data.size() - 1);
    std::normal_distribution<double> nd(0, bw);
    Vec out(n);
    for (auto& v : out) {
        for (int tries = 0; tries < 100; ++tries) {
            v = data[pick(rng)] + nd(rng);
            if (v >= lo && v <= hi) break;
        }
        v = std::min(hi, std::max(lo, v));
    }
    return out;
}

Mat kdeHistogram(const Vec& data0, double lo, double hi, double bw, int bins) {
    Vec data = stats::finite(data0);
    if (data.empty() || bins < 1) throw std::invalid_argument("no data");
    if (!(hi > lo)) {
        lo = *std::min_element(data.begin(), data.end());
        hi = *std::max_element(data.begin(), data.end());
        double r = hi - lo;
        lo -= 0.1 * r; hi += 0.1 * r;
    }
    if (!(bw > 0)) bw = silverman(data);
    double w = (hi - lo) / bins;
    Mat out;
    for (int i = 0; i < bins; ++i) {
        double a = lo + i * w, b = a + w;
        out.push_back({a, b, kdeDensity(data, bw, (a + b) / 2)});
    }
    return out;
}

Mat histogram3(const Vec& data, int bins) {
    auto h = stats::histogram(data, bins);
    double total = 0;
    for (double c : h.count) total += c;
    Mat out;
    for (size_t i = 0; i < h.count.size(); ++i) out.push_back({h.lo[i], h.hi[i], total > 0 ? h.count[i] / total : 0});
    return out;
}

// ============================================================ MCMC

Vec metropolis(const std::function<double(double)>& logp, double x0, double scale, int n, int burnIn,
               int thinning, bool asymmetric, std::mt19937_64& rng) {
    if (n < 1) return {};
    if (thinning < 1) thinning = 1;
    if (burnIn < 0) burnIn = 0;
    std::uniform_real_distribution<double> uni(0, 1);
    std::normal_distribution<double> nd(0, scale);
    double x = x0, lp = logp(x0);
    // asymmetric uniform proposal on [x - 0.6s, x + 0.4s] / reverse [x' - 0.4s, x' + 0.6s]...
    // implemented as mixture of the two orientations so the kernel stays symmetric overall
    Vec out;
    out.reserve(n);
    long total = long(burnIn) + long(n) * thinning;
    for (long i = 0; i < total; ++i) {
        double xp;
        if (asymmetric) {
            bool right = uni(rng) < 0.5;
            double a = right ? -0.4 : -0.6, b = right ? 0.6 : 0.4;
            xp = x + scale * 2 * (a + (b - a) * uni(rng));
        } else {
            xp = x + nd(rng);
        }
        double lpp = logp(xp);
        if (std::isfinite(lpp) && (lpp >= lp || std::log(uni(rng)) < lpp - lp)) { x = xp; lp = lpp; }
        if (i >= burnIn && (i - burnIn) % thinning == 0) out.push_back(x);
    }
    return out;
}

namespace {
struct GridDensity {
    double lo, w;
    Vec d;
    double at(double x) const {
        if (d.empty()) return 0;
        double pos = (x - lo) / w - 0.5;
        if (pos < 0 || pos > d.size() - 1) return 0;
        size_t i = size_t(pos);
        double t = pos - i;
        return i + 1 < d.size() ? d[i] * (1 - t) + d[i + 1] * t : d[i];
    }
};
GridDensity gridKde(const Vec& data, double lo, double hi, int bins) {
    GridDensity g;
    g.lo = lo;
    g.w = (hi - lo) / bins;
    double bw = silverman(data);
    // binned KDE for speed
    auto h = stats::histogram(data, std::max(16, bins * 2));
    for (int i = 0; i < bins; ++i) {
        double x = lo + (i + 0.5) * g.w, acc = 0;
        for (size_t k = 0; k < h.count.size(); ++k) {
            double c = (h.lo[k] + h.hi[k]) / 2, z = (x - c) / bw;
            acc += h.count[k] * std::exp(-0.5 * z * z);
        }
        g.d.push_back(acc);
    }
    return g;
}
} // namespace

Vec posteriorMCMC(const Vec& prior0, const Vec& like0, int n, int burnIn, int thinning, int bins, bool asymmetric,
                  std::mt19937_64& rng) {
    Vec prior = stats::finite(prior0), like = stats::finite(like0);
    if (prior.size() < 2 || like.size() < 2) throw std::invalid_argument("prior and likelihood need at least 2 values each");
    double lo = std::min(stats::min(prior), stats::min(like)), hi = std::max(stats::max(prior), stats::max(like));
    double r = hi - lo;
    if (!(r > 0)) r = 1;
    lo -= 0.2 * r; hi += 0.2 * r;
    if (bins < 10) bins = 10;
    GridDensity gp = gridKde(prior, lo, hi, bins), gl = gridKde(like, lo, hi, bins);
    auto logp = [&](double x) {
        double v = gp.at(x) * gl.at(x);
        return v > 0 ? std::log(v) : -std::numeric_limits<double>::infinity();
    };
    // start at the grid maximum
    double best = lo, bv = -1;
    for (int i = 0; i < bins; ++i) {
        double x = lo + (i + 0.5) * gp.w, v = gp.at(x) * gl.at(x);
        if (v > bv) { bv = v; best = x; }
    }
    if (bv <= 0) throw std::runtime_error("prior and likelihood do not overlap");
    double scale = std::min(stats::stdev(prior), stats::stdev(like));
    if (!(scale > 0)) scale = r / 10;
    return metropolis(logp, best, scale, n, burnIn, thinning, asymmetric, rng);
}

Vec posteriorGrid(const Vec& prior0, const Vec& like0, int n, std::mt19937_64& rng) {
    Vec prior = stats::finite(prior0), like = stats::finite(like0);
    if (prior.size() < 2 || like.size() < 2) throw std::invalid_argument("prior and likelihood need at least 2 values each");
    double lo = std::min(stats::min(prior), stats::min(like)), hi = std::max(stats::max(prior), stats::max(like));
    double r = hi - lo;
    if (!(r > 0)) r = 1;
    lo -= 0.2 * r; hi += 0.2 * r;
    const int G = 2000;
    double bp = silverman(prior), bl = silverman(like), w = (hi - lo) / G;
    Vec cum(G);
    double c = 0;
    for (int i = 0; i < G; ++i) {
        double x = lo + (i + 0.5) * w;
        c += kdeDensity(prior, bp, x) * kdeDensity(like, bl, x);
        cum[i] = c;
    }
    if (!(c > 0)) throw std::runtime_error("prior and likelihood do not overlap");
    std::uniform_real_distribution<double> uni(0, 1);
    Vec out(n);
    for (auto& v : out) {
        double u = uni(rng) * c;
        size_t i = std::lower_bound(cum.begin(), cum.end(), u) - cum.begin();
        v = lo + (std::min<size_t>(i, G - 1) + uni(rng)) * w;
    }
    return out;
}

Vec combine(const std::vector<Vec>& samples, const Vec& weights, int n, std::mt19937_64& rng) {
    std::vector<Vec> s;
    Vec w;
    for (size_t i = 0; i < samples.size(); ++i) {
        Vec f = stats::finite(samples[i]);
        double wi = i < weights.size() ? weights[i] : 1.0;
        if (!f.empty() && wi > 0) { s.push_back(f); w.push_back(wi); }
    }
    if (s.empty()) throw std::invalid_argument("nothing to combine");
    std::discrete_distribution<size_t> which(w.begin(), w.end());
    Vec out(n);
    for (auto& v : out) {
        const Vec& src = s[which(rng)];
        v = src[std::uniform_int_distribution<size_t>(0, src.size() - 1)(rng)];
    }
    return out;
}

// ============================================================ Bayes factor

BayesFactorResult bayesFactor(double p0, double pObs, double attempts, double percentile) {
    if (!(p0 > 0 && p0 < 1)) throw std::invalid_argument("expected probability must be in (0,1)");
    if (!(pObs >= 0 && pObs <= 1)) throw std::invalid_argument("observed probability must be in [0,1]");
    double n = std::round(attempts);
    if (n < 1) throw std::invalid_argument("number of attempts must be at least 1");
    double k = std::round(pObs * n);
    // marginal likelihood under H1 with uniform prior: 1/(n+1); under H0: C(n,k) p0^k (1-p0)^(n-k)
    double lchoose = std::lgamma(n + 1) - std::lgamma(k + 1) - std::lgamma(n - k + 1);
    double l0 = lchoose + k * std::log(p0) + (n - k) * std::log(1 - p0);
    double l1 = -std::log(n + 1);
    BayesFactorResult r;
    r.bf10 = std::exp(l1 - l0);
    double a = k + 1, b = n - k + 1;
    r.posteriorMean = a / (a + b);
    r.credible = kNaN;
    if (percentile > 0 && percentile < 1)
        r.credible = invertCdf([&](double x) { return betaI(a, b, x); }, percentile, 0.0, 1.0);
    return r;
}

// ============================================================ regression

static Vec solve(Mat a, Vec b) {
    size_t n = b.size();
    for (size_t i = 0; i < n; ++i) a[i][i] += 1e-12 * (1 + std::fabs(a[i][i]));
    for (size_t c = 0; c < n; ++c) {
        size_t p = c;
        for (size_t r = c + 1; r < n; ++r) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        std::swap(a[c], a[p]);
        std::swap(b[c], b[p]);
        if (std::fabs(a[c][c]) < 1e-300) throw std::runtime_error("regression matrix is singular");
        for (size_t r = c + 1; r < n; ++r) {
            double f = a[r][c] / a[c][c];
            for (size_t k = c; k < n; ++k) a[r][k] -= f * a[c][k];
            b[r] -= f * b[c];
        }
    }
    Vec x(n);
    for (size_t i = n; i-- > 0;) {
        double s = b[i];
        for (size_t k = i + 1; k < n; ++k) s -= a[i][k] * x[k];
        x[i] = s / a[i][i];
    }
    return x;
}

Vec ols(const Vec& y, const Mat& x, const Vec& w, double& sigma) {
    size_t n = y.size();
    if (n == 0 || x.size() != n) throw std::invalid_argument("DataY and DataX must have the same number of rows");
    size_t m = x[0].size() + 1;
    if (n < m) throw std::invalid_argument("not enough observations for the regression");
    Mat a(m, Vec(m, 0.0));
    Vec b(m, 0.0);
    for (size_t i = 0; i < n; ++i) {
        Vec r(m);
        r[0] = 1;
        for (size_t j = 1; j < m; ++j) r[j] = x[i][j - 1];
        double wi = w.empty() ? 1.0 : w[i];
        for (size_t j = 0; j < m; ++j) {
            b[j] += wi * r[j] * y[i];
            for (size_t k = 0; k < m; ++k) a[j][k] += wi * r[j] * r[k];
        }
    }
    Vec beta = solve(a, b);
    double sse = 0, sw = 0;
    for (size_t i = 0; i < n; ++i) {
        double p = beta[0];
        for (size_t j = 1; j < m; ++j) p += beta[j] * x[i][j - 1];
        double wi = w.empty() ? 1.0 : w[i];
        sse += wi * (y[i] - p) * (y[i] - p);
        sw += wi;
    }
    double dof = std::max(1.0, double(n) - m);
    sigma = std::sqrt(sse / sw * n / dof);
    return beta;
}

Vec ols(const Vec& y, const Mat& x, double& sigma) { return ols(y, x, {}, sigma); }

static double median(Vec v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

Vec repeatedMedian(const Vec& y, const Vec& x, double& sigma) {
    size_t n = y.size();
    if (x.size() != n) throw std::invalid_argument("DataY and DataX must have the same length");
    if (n < 3) throw std::invalid_argument("not enough observations for the regression");
    Vec inner;
    for (size_t i = 0; i < n; ++i) {
        Vec s;
        for (size_t j = 0; j < n; ++j)
            if (j != i && x[j] != x[i]) s.push_back((y[j] - y[i]) / (x[j] - x[i]));
        if (!s.empty()) inner.push_back(median(s));
    }
    if (inner.empty()) throw std::runtime_error("X is constant");
    double slope = median(inner);
    Vec ic;
    for (size_t i = 0; i < n; ++i) ic.push_back(y[i] - slope * x[i]);
    double intercept = median(ic);
    double sse = 0;
    for (size_t i = 0; i < n; ++i) { double e = y[i] - intercept - slope * x[i]; sse += e * e; }
    sigma = std::sqrt(sse / std::max(1.0, double(n) - 2));
    return {intercept, slope};
}

static Vec fitRow(const Vec& y, const Mat& x, const Vec& w, bool med) {
    double s;
    Vec b;
    if (med && !x.empty() && x[0].size() == 1) {
        Vec xs, ys;
        for (size_t i = 0; i < y.size(); ++i) {
            int reps = w.empty() ? 1 : int(std::lround(w[i]));
            for (int k = 0; k < reps; ++k) { xs.push_back(x[i][0]); ys.push_back(y[i]); }
        }
        b = repeatedMedian(ys, xs, s);
    } else {
        b = ols(y, x, w, s);
    }
    b.push_back(s);
    return b;
}

Mat bootstrapRegression(const Vec& y, const Mat& x, int samples, bool med, std::mt19937_64& rng) {
    size_t n = y.size();
    std::uniform_int_distribution<size_t> pick(0, n - 1);
    Mat out;
    for (int s = 0; s < samples; ++s) {
        Vec w(n, 0.0);
        for (size_t i = 0; i < n; ++i) w[pick(rng)] += 1;
        try { out.push_back(fitRow(y, x, w, med)); } catch (...) { --s; if (out.empty() && s < -50) throw; }
    }
    return out;
}

Mat bayesRegression(const Vec& y, const Mat& x, int samples, bool med, int kfolds, std::mt19937_64& rng) {
    size_t n = y.size();
    if (kfolds < 1) kfolds = 1;
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::shuffle(idx.begin(), idx.end(), rng);
    Mat out;
    std::gamma_distribution<double> ex(1.0, 1.0);
    for (int f = 0; f < kfolds; ++f) {
        Vec yf;
        Mat xf;
        for (size_t i = f; i < n; i += kfolds) { yf.push_back(y[idx[i]]); xf.push_back(x[idx[i]]); }
        int per = samples / kfolds + (f < samples % kfolds ? 1 : 0);
        for (int s = 0; s < per; ++s) {
            Vec w(yf.size());
            double sum = 0;
            for (auto& v : w) { v = ex(rng); sum += v; }
            for (auto& v : w) v *= yf.size() / sum;  // Dirichlet(1) weights scaled to n
            if (med) for (auto& v : w) v = std::max(0.0, std::round(v));
            out.push_back(fitRow(yf, xf, w, med));
        }
    }
    return out;
}

Vec predictive(const Mat& params, const Vec& xv, bool noise, std::mt19937_64& rng) {
    std::normal_distribution<double> nd;
    Vec out;
    for (const auto& p : params) {
        double v = p[0];
        for (size_t j = 0; j < xv.size() && j + 1 < p.size() - 1; ++j) v += p[j + 1] * xv[j];
        if (noise) v += p.back() * nd(rng);
        out.push_back(v);
    }
    return out;
}

// ============================================================ naive Bayes

void NaiveBayes::train(const Mat& x, const std::vector<std::string>& y, const ClassifierOptions& o) {
    if (x.empty() || x.size() != y.size()) throw std::invalid_argument("FeatureData and ClassData must have the same number of rows");
    o_ = o;
    classes_.clear();
    std::map<std::string, int> ci;
    for (const auto& c : y)
        if (!ci.count(c)) { ci[c] = int(classes_.size()); classes_.push_back(c); }
    size_t K = classes_.size(), F = x[0].size();
    data_.assign(K, std::vector<Vec>(F));
    prior_.assign(K, 0);
    for (size_t i = 0; i < x.size(); ++i) {
        int c = ci[y[i]];
        prior_[c] += 1;
        for (size_t f = 0; f < F; ++f) data_[c][f].push_back(x[i][f]);
    }
    for (auto& p : prior_) p = o.flatPrior ? 1.0 / K : p / x.size();
    featureKind_.assign(F, 0);
    mu_.assign(K, Vec(F)); sd_.assign(K, Vec(F)); pb_.assign(K, Vec(F));
    bw_.assign(F, 1.0);
    for (size_t f = 0; f < F; ++f) {
        Vec all;
        for (size_t i = 0; i < x.size(); ++i) all.push_back(x[i][f]);
        bool binary = std::all_of(all.begin(), all.end(), [](double v) { return v == 0 || v == 1; });
        bool normal = false;
        stats::andersonDarling(all, normal);
        featureKind_[f] = binary ? 1 : (o.model == 1 ? 0 : o.model == 2 ? 2 : (normal ? 0 : 2));
        bw_[f] = silverman(all);
        for (size_t c = 0; c < K; ++c) {
            const Vec& d = data_[c][f];
            mu_[c][f] = stats::mean(d);
            double s = stats::stdev(d);
            sd_[c][f] = (s > 0 ? s : stats::stdev(all) * 0.1 + 1e-9);
            double ones = std::count(d.begin(), d.end(), 1.0);
            pb_[c][f] = (ones + 1) / (d.size() + 2);  // Laplace smoothing
        }
    }
    // feature weights: between-class / total variance ratio, normalised to mean 1
    weight_.assign(F, 1.0);
    if (o.weightedFeatures) {
        double sum = 0;
        for (size_t f = 0; f < F; ++f) {
            Vec all;
            for (size_t i = 0; i < x.size(); ++i) all.push_back(x[i][f]);
            double m = stats::mean(all), tv = stats::variance(all), bv = 0;
            for (size_t c = 0; c < K; ++c) bv += data_[c][f].size() * (mu_[c][f] - m) * (mu_[c][f] - m);
            bv /= x.size();
            weight_[f] = tv > 0 ? bv / tv + 1e-6 : 1e-6;
            sum += weight_[f];
        }
        for (auto& w : weight_) w = w / sum * F;
    }
}

Vec NaiveBayes::probabilities(const Vec& row) const {
    size_t K = classes_.size();
    Vec lp(K);
    for (size_t c = 0; c < K; ++c) {
        double l = std::log(std::max(prior_[c], 1e-300));
        for (size_t f = 0; f < row.size() && f < featureKind_.size(); ++f) {
            double p;
            if (featureKind_[f] == 1) p = row[f] == 1 ? pb_[c][f] : 1 - pb_[c][f];
            else if (featureKind_[f] == 0) {
                double z = (row[f] - mu_[c][f]) / sd_[c][f];
                p = std::exp(-0.5 * z * z) / (sd_[c][f] * std::sqrt(2 * kPi));
            } else p = kdeDensity(data_[c][f], bw_[f], row[f]);
            l += weight_[f] * std::log(std::max(p, 1e-300));
        }
        lp[c] = l;
    }
    double mx = *std::max_element(lp.begin(), lp.end()), s = 0;
    for (auto& v : lp) { v = std::exp(v - mx); s += v; }
    for (auto& v : lp) v /= s;
    return lp;
}

std::string NaiveBayes::predict(const Vec& row) const {
    Vec p = probabilities(row);
    return classes_[std::max_element(p.begin(), p.end()) - p.begin()];
}

// ============================================================ digit tests

static DigitTest digitTest(const Vec& counts, const Vec& expected, double n) {
    DigitTest t;
    t.n = n;
    t.chi2 = 0;
    t.mad = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        t.observed.push_back(counts[i] / n);
        t.expected.push_back(expected[i]);
        double e = n * expected[i];
        t.chi2 += (counts[i] - e) * (counts[i] - e) / e;
        t.mad += std::fabs(counts[i] / n - expected[i]) / counts.size();
    }
    t.pValue = 1 - chi2Cdf(t.chi2, double(counts.size() - 1));
    return t;
}

DigitTest benford(const Vec& data) {
    Vec c(9, 0.0), e;
    double n = 0;
    for (double v : data) {
        v = std::fabs(v);
        if (!(v > 0) || !std::isfinite(v)) continue;
        while (v >= 10) v /= 10;
        while (v < 1) v *= 10;
        c[std::min(8, int(v) - 1)] += 1;
        n += 1;
    }
    if (n < 1) throw std::invalid_argument("no positive numbers");
    for (int d = 1; d <= 9; ++d) e.push_back(std::log10(1 + 1.0 / d));
    return digitTest(c, e, n);
}

DigitTest lastDigit(const Vec& data) {
    Vec c(10, 0.0), e(10, 0.1);
    double n = 0;
    for (double v : data) {
        if (!std::isfinite(v)) continue;
        double ip = std::floor(std::fabs(v));
        c[size_t(std::fmod(ip, 10.0))] += 1;
        n += 1;
    }
    if (n < 1) throw std::invalid_argument("no numbers");
    return digitTest(c, e, n);
}

} // namespace mc::bayes
