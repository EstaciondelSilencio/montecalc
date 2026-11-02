// MonteCalc - fitting distributions, time series and Holt-Winters models
#include "fitting.h"
#include "specfun.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace mc {

static const double kBig = 1e100;

std::string fmtNum(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

static std::string formula(const std::string& fn, const std::vector<double>& p) {
    std::string s = "=FLOsimula_" + fn + "(";
    for (size_t i = 0; i < p.size(); ++i) s += (i ? ";" : "") + fmtNum(p[i]);
    return s + ")";
}

std::vector<double> nelderMead(const std::function<double(const std::vector<double>&)>& f,
                               std::vector<double> x0, std::vector<double> step, int maxIter,
                               double& fbest) {
    size_t n = x0.size();
    std::vector<std::vector<double>> s(n + 1, x0);
    for (size_t i = 0; i < n; ++i) s[i + 1][i] += step[i] != 0 ? step[i] : 0.1;
    std::vector<double> fv(n + 1);
    for (size_t i = 0; i <= n; ++i) fv[i] = f(s[i]);
    for (int it = 0; it < maxIter; ++it) {
        std::vector<size_t> idx(n + 1);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return fv[a] < fv[b]; });
        std::vector<std::vector<double>> s2;
        std::vector<double> f2;
        for (size_t i : idx) { s2.push_back(s[i]); f2.push_back(fv[i]); }
        s = s2; fv = f2;
        if (std::fabs(fv[n] - fv[0]) <= 1e-12 * (std::fabs(fv[0]) + 1e-20) && it > 20) break;
        std::vector<double> c(n, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j) c[j] += s[i][j] / n;
        auto lerp = [&](double t) {
            std::vector<double> x(n);
            for (size_t j = 0; j < n; ++j) x[j] = c[j] + t * (s[n][j] - c[j]);
            return x;
        };
        auto xr = lerp(-1);
        double fr = f(xr);
        if (fr < fv[0]) {
            auto xe = lerp(-2);
            double fe = f(xe);
            if (fe < fr) { s[n] = xe; fv[n] = fe; } else { s[n] = xr; fv[n] = fr; }
        } else if (fr < fv[n - 1]) {
            s[n] = xr; fv[n] = fr;
        } else {
            auto xc = fr < fv[n] ? lerp(-0.5) : lerp(0.5);
            double fc = f(xc);
            if (fc < std::min(fr, fv[n])) {
                s[n] = xc; fv[n] = fc;
            } else {
                for (size_t i = 1; i <= n; ++i) {
                    for (size_t j = 0; j < n; ++j) s[i][j] = s[0][j] + 0.5 * (s[i][j] - s[0][j]);
                    fv[i] = f(s[i]);
                }
            }
        }
    }
    size_t b = std::min_element(fv.begin(), fv.end()) - fv.begin();
    fbest = fv[b];
    return s[b];
}

// ============================================================ distributions

namespace {

struct Spec {
    const char* name;      // display name
    const char* fn;        // FLOsimula_ suffix
    double k;
    std::function<bool(const stats::Vec&)> applicable;
    std::function<std::vector<double>(const stats::Vec&)> init;
    std::function<DistPtr(const std::vector<double>&)> make;
};

double ksPValue(double d, double n) {
    double lam = (std::sqrt(n) + 0.12 + 0.11 / std::sqrt(n)) * d;
    double p = 0;
    for (int k = 1; k < 100; ++k) p += 2 * ((k % 2) ? 1 : -1) * std::exp(-2.0 * k * k * lam * lam);
    return std::min(1.0, std::max(0.0, p));
}

const std::vector<Spec>& specs() {
    using V = std::vector<double>;
    static const std::vector<Spec> s = {
        {"Uniform", "Uniform", 2.2, [](const V&) { return true; },
         [](const V& x) { double r = x.back() - x.front(); return V{x.front() - 0.01 * r, x.back() + 0.01 * r}; },
         [](const V& p) { return makeUniform(p[0], p[1]); }},
        {"Triangle", "Triangle", 3.2, [](const V&) { return true; },
         [](const V& x) {
             double r = x.back() - x.front(), m = stats::mean(x);
             double mo = std::min(x.back(), std::max(x.front(), 3 * m - x.front() - x.back()));
             return V{x.front() - 0.01 * r, mo, x.back() + 0.01 * r};
         },
         [](const V& p) { return makeTriangular(p[0], p[1], p[2]); }},
        {"PERT", "PERT", 3.2, [](const V&) { return true; },
         [](const V& x) {
             double r = x.back() - x.front(), m = stats::mean(x);
             double mo = std::min(x.back(), std::max(x.front(), (6 * m - x.front() - x.back()) / 4));
             return V{x.front() - 0.01 * r, mo, x.back() + 0.01 * r};
         },
         [](const V& p) { return makePert(p[0], p[1], p[2]); }},
        {"Normal", "Normal", 2, [](const V&) { return true; },
         [](const V& x) { return V{stats::mean(x), stats::stdev(x)}; },
         [](const V& p) { return makeNormal(p[0], p[1]); }},
        {"Lognormal", "Lognormal", 2, [](const V& x) { return x.front() > 0; },
         [](const V& x) { return V{stats::mean(x), stats::stdev(x)}; },
         [](const V& p) { return makeLognormal(p[0], p[1]); }},
        {"Beta", "Beta", 4.2, [](const V&) { return true; },
         [](const V& x) {
             double r = x.back() - x.front(), lo = x.front() - 0.01 * r, hi = x.back() + 0.01 * r;
             double m = (stats::mean(x) - lo) / (hi - lo), v = stats::variance(x) / ((hi - lo) * (hi - lo));
             double c = std::max(0.1, m * (1 - m) / v - 1);
             return V{std::max(0.2, m * c), std::max(0.2, (1 - m) * c), lo, hi};
         },
         [](const V& p) { return makeBeta(p[0], p[1], p[2], p[3]); }},
        {"Gumbel", "Gumbel", 2, [](const V&) { return true; },
         [](const V& x) { double b = stats::stdev(x) * std::sqrt(6.0) / kPi; return V{stats::mean(x) - 0.5772 * b, b}; },
         [](const V& p) { return makeGumbel(p[0], p[1]); }},
        {"Gamma", "Gamma", 2, [](const V& x) { return x.front() > 0; },
         [](const V& x) { double m = stats::mean(x), v = stats::variance(x); return V{m * m / v, v / m}; },
         [](const V& p) { return makeGamma(p[0], p[1]); }},
        {"Exponential", "Exponential", 1, [](const V& x) { return x.front() >= 0; },
         [](const V& x) { return V{1 / stats::mean(x)}; },
         [](const V& p) { return makeExponential(p[0]); }},
        {"Weibull", "Weibull", 2, [](const V& x) { return x.front() > 0; },
         [](const V& x) {
             double k = std::max(0.3, 1.2 / stats::cv(x));
             return V{k, stats::mean(x) / std::tgamma(1 + 1 / k)};
         },
         [](const V& p) { return makeWeibull(p[0], p[1]); }},
        {"Chi-2", "Chi2", 1, [](const V& x) { return x.front() > 0; },
         [](const V& x) { return V{stats::mean(x)}; },
         [](const V& p) { return makeChiSquare(p[0]); }},
        {"Student-T", "StudentT", 1, [](const V&) { return true; },
         [](const V&) { return V{5}; },
         [](const V& p) { return makeStudentT(p[0]); }},
        {"Pareto", "Pareto", 2, [](const V& x) { return x.front() > 0; },
         [](const V& x) {
             double xm = x.front() * 0.999, m = stats::mean(x);
             return V{std::max(1.05, m / std::max(1e-12, m - xm)), xm};
         },
         [](const V& p) { return makePareto(p[0], p[1]); }},
        {"F", "F", 2, [](const V& x) { return x.front() > 0; },
         [](const V& x) { double m = stats::mean(x); return V{5, m > 1.05 ? 2 * m / (m - 1) : 10}; },
         [](const V& p) { return makeF(p[0], p[1]); }},
        {"ArcSin", "ArcSin", 2.2, [](const V&) { return true; },
         [](const V& x) { double r = x.back() - x.front(); return V{x.front() - 0.01 * r, x.back() + 0.01 * r}; },
         [](const V& p) { return makeArcsine(p[0], p[1]); }},
        {"Cauchy", "Cauchy", 2, [](const V&) { return true; },
         [](const V& x) {
             return V{stats::percentile(x, 0.5), std::max(1e-9, (stats::percentile(x, 0.75) - stats::percentile(x, 0.25)) / 2)};
         },
         [](const V& p) { return makeCauchy(p[0], p[1]); }},
        {"Frechet", "Frechet", 3, [](const V&) { return true; },
         [](const V& x) {
             double s = stats::stdev(x);
             return V{4, 2 * s, x.front() - 1.5 * s};
         },
         [](const V& p) { return makeFrechet(p[0], p[1], p[2]); }},
        {"Logistic", "Logistic", 2, [](const V&) { return true; },
         [](const V& x) { return V{stats::mean(x), stats::stdev(x) * std::sqrt(3.0) / kPi}; },
         [](const V& p) { return makeLogistic(p[0], p[1]); }},
    };
    return s;
}

} // namespace

DistPtr makeFitted(const DistFit& f) {
    for (const auto& s : specs())
        if (f.name == s.name) return s.make(f.params);
    throw std::invalid_argument("unknown fitted distribution");
}

std::vector<DistFit> fitDistributions(const std::vector<double>& data, const FitOptions& opt) {
    stats::Vec x = stats::sorted(data);
    if (x.size() < 5) throw std::invalid_argument("at least 5 data points are needed to fit a distribution");
    if (x.back() == x.front()) throw std::invalid_argument("data are constant");
    const double n = x.size();
    std::vector<double> ecdf(x.size());
    for (size_t i = 0; i < x.size(); ++i) ecdf[i] = (i + 0.5) / n;
    std::vector<DistFit> out;
    for (const auto& sp : specs()) {
        if (opt.preferHigh && std::string(sp.name) != "Beta") continue;
        DistFit f;
        f.name = sp.name;
        f.k = sp.k;
        if (!sp.applicable(x)) { f.error = "not applicable to these data"; out.push_back(f); continue; }
        auto objective = [&](const std::vector<double>& p) {
            for (double v : p) if (!std::isfinite(v)) return kBig;
            try {
                DistPtr d = sp.make(p);
                double r = 0;
                for (size_t i = 0; i < x.size(); ++i) {
                    double e = d->cdf(x[i]) - ecdf[i];
                    double w = (opt.preferHigh && x[i] > opt.highThreshold) ? opt.highWeight : 1.0;
                    r += w * e * e;
                }
                return std::isfinite(r) ? r : kBig;
            } catch (...) {
                return kBig;
            }
        };
        try {
            auto p0 = sp.init(x);
            std::vector<double> step(p0.size());
            double scale = x.back() - x.front();
            for (size_t i = 0; i < p0.size(); ++i) step[i] = std::fabs(p0[i]) > 1e-9 ? 0.1 * std::fabs(p0[i]) : 0.1 * scale;
            double fb;
            auto best = nelderMead(objective, p0, step, 600, fb);
            // restart once from the optimum for robustness
            for (size_t i = 0; i < best.size(); ++i) step[i] = std::fabs(best[i]) > 1e-9 ? 0.05 * std::fabs(best[i]) : 0.05 * scale;
            best = nelderMead(objective, best, step, 600, fb);
            if (fb >= kBig) throw std::runtime_error("no valid parameters found");
            DistPtr d = sp.make(best);
            double ks = 0, rss = 0;
            for (size_t i = 0; i < x.size(); ++i) {
                double F = d->cdf(x[i]);
                ks = std::max({ks, std::fabs(F - i / n), std::fabs(F - (i + 1) / n)});
                rss += (F - ecdf[i]) * (F - ecdf[i]);
            }
            f.params = best;
            f.rss = rss;
            f.ks = ks;
            f.ksPValue = ksPValue(ks, n);
            f.aic = 2 * f.k + n * std::log(std::max(rss, 1e-300));
            f.formula = formula(sp.fn, best);
            f.ok = true;
        } catch (const std::exception& e) {
            f.error = e.what();
        }
        out.push_back(f);
    }
    std::stable_sort(out.begin(), out.end(), [](const DistFit& a, const DistFit& b) {
        if (a.ok != b.ok) return a.ok;
        return a.rss < b.rss;
    });
    return out;
}

// ============================================================ time series

double theilU(const std::vector<double>& y, const std::vector<double>& f) {
    double num = 0, den = 0;
    for (size_t t = 1; t < y.size() && t < f.size(); ++t) {
        num += (f[t] - y[t]) * (f[t] - y[t]);
        den += (y[t] - y[t - 1]) * (y[t] - y[t - 1]);
    }
    return den > 0 ? std::sqrt(num / den) : std::numeric_limits<double>::quiet_NaN();
}

namespace {

double varOf(const std::vector<double>& e) {
    double s = 0;
    for (double v : e) s += v * v;
    return e.empty() ? 0 : s / e.size();
}

void finish(TsFit& f, const std::vector<double>& y, const std::vector<double>& resid, int k) {
    double v = varOf(resid);
    f.rss = v * resid.size();
    f.aic = std::log(std::max(v, 1e-300)) + 2.0 * k / y.size();
    f.theilU = theilU(y, f.fitted);
    f.ok = std::isfinite(f.aic);
}

} // namespace

std::vector<TsFit> fitTimeSeries(const std::vector<double>& y0) {
    std::vector<double> y = stats::finite(y0);
    if (y.size() < 8) throw std::invalid_argument("at least 8 observations are needed to fit a time series");
    const size_t n = y.size();
    std::vector<TsFit> out;
    double mu0 = stats::mean(y);

    // AR(1) by least squares
    {
        TsFit f;
        f.name = "AR(1)";
        std::vector<double> a(y.begin(), y.end() - 1), b(y.begin() + 1, y.end());
        double phi = std::max(-0.99, std::min(0.99, stats::regressionSlope(a, b)));
        double c = stats::regressionIntercept(a, b);
        double mu = std::fabs(1 - phi) > 1e-9 ? c / (1 - phi) : mu0;
        f.fitted = {y[0]};
        std::vector<double> e;
        for (size_t t = 1; t < n; ++t) {
            double p = mu + phi * (y[t - 1] - mu);
            f.fitted.push_back(p);
            e.push_back(y[t] - p);
        }
        double sigma = std::sqrt(varOf(e));
        f.ts = {TsType::AR1, {mu, phi, std::max(sigma, 1e-12), y.back()}};
        finish(f, y, e, 3);
        f.formula = formula("AR1", f.ts.p);
        out.push_back(f);
    }
    // MA(1) and ARMA(1,1) by conditional sum of squares
    auto css = [&](double mu, double phi, double theta, std::vector<double>* fitted, std::vector<double>* res) {
        double ePrev = 0, s = 0;
        if (fitted) *fitted = {y[0]};
        for (size_t t = 1; t < n; ++t) {
            double p = mu + phi * (y[t - 1] - mu) + theta * ePrev;
            double e = y[t] - p;
            if (fitted) fitted->push_back(p);
            if (res) res->push_back(e);
            s += e * e;
            ePrev = e;
        }
        return s;
    };
    for (int arma = 0; arma <= 1; ++arma) {
        TsFit f;
        f.name = arma ? "ARMA(1,1)" : "MA(1)";
        double fb;
        double sd = stats::stdev(y);
        std::vector<double> p0 = arma ? std::vector<double>{mu0, 0.3, 0.1} : std::vector<double>{mu0, 0.1};
        std::vector<double> st = arma ? std::vector<double>{0.1 * sd + 1e-9, 0.2, 0.2} : std::vector<double>{0.1 * sd + 1e-9, 0.2};
        auto best = nelderMead(
            [&](const std::vector<double>& p) {
                double phi = arma ? p[1] : 0, th = arma ? p[2] : p[1];
                if (std::fabs(phi) >= 0.99 || std::fabs(th) >= 0.99) return kBig;
                return css(p[0], phi, th, nullptr, nullptr);
            },
            p0, st, 1500, fb);
        std::vector<double> res;
        double phi = arma ? best[1] : 0, th = arma ? best[2] : best[1];
        css(best[0], phi, th, &f.fitted, &res);
        double sigma = std::max(std::sqrt(varOf(res)), 1e-12);
        if (arma) f.ts = {TsType::ARMA11, {best[0], phi, th, sigma, y.back()}};
        else f.ts = {TsType::MA1, {best[0], th, sigma}};
        finish(f, y, res, arma ? 4 : 3);
        f.formula = formula(arma ? "ARMA11" : "MA1", f.ts.p);
        out.push_back(f);
    }
    // ARCH(1) by maximum likelihood around the mean
    {
        TsFit f;
        f.name = "ARCH(1)";
        double v0 = stats::variance(y);
        double fb;
        auto nll = [&](const std::vector<double>& p) {
            double mu = p[0], a0 = std::exp(p[1]), a1 = 1 / (1 + std::exp(-p[2])) * 0.99;
            double s = 0;
            for (size_t t = 1; t < n; ++t) {
                double d = y[t - 1] - mu, h = a0 + a1 * d * d, e = y[t] - mu;
                s += std::log(h) + e * e / h;
            }
            return std::isfinite(s) ? s : kBig;
        };
        auto best = nelderMead(nll, {mu0, std::log(v0 * 0.7 + 1e-12), 0.0}, {0.1 * std::sqrt(v0) + 1e-9, 0.5, 0.5}, 1500, fb);
        double mu = best[0], a0 = std::exp(best[1]), a1 = 1 / (1 + std::exp(-best[2])) * 0.99;
        f.fitted = {y[0]};
        std::vector<double> e;
        for (size_t t = 1; t < n; ++t) { f.fitted.push_back(mu); e.push_back(y[t] - mu); }
        f.ts = {TsType::ARCH1, {mu, a0, a1, y.back()}};
        finish(f, y, e, 3);
        f.formula = formula("ARCH1", f.ts.p);
        out.push_back(f);
    }
    // Wiener process (geometric Brownian motion) on log returns
    if (*std::min_element(y.begin(), y.end()) > 0) {
        TsFit f;
        f.name = "Wiener process";
        std::vector<double> lr;
        for (size_t t = 1; t < n; ++t) lr.push_back(std::log(y[t] / y[t - 1]));
        double m = stats::mean(lr), s = stats::stdev(lr);
        if (!(s > 0)) s = 1e-9;
        double drift = m + s * s / 2;
        f.fitted = {y[0]};
        std::vector<double> e;
        for (size_t t = 1; t < n; ++t) {
            double p = y[t - 1] * std::exp(drift);
            f.fitted.push_back(p);
            e.push_back(y[t] - p);
        }
        f.ts = {TsType::GBM, {y.back(), drift, s, 1}};
        finish(f, y, e, 2);
        f.formula = formula("WienerProcess", f.ts.p);
        out.push_back(f);
    }
    std::stable_sort(out.begin(), out.end(), [](const TsFit& a, const TsFit& b) {
        if (a.ok != b.ok) return a.ok;
        return a.aic < b.aic;
    });
    return out;
}

TimeSeries anchorAt(const TimeSeries& ts, double last) {
    TimeSeries t = ts;
    switch (t.type) {
    case TsType::AR1: t.p[3] = last; break;
    case TsType::ARMA11: t.p[4] = last; break;
    case TsType::ARCH1: t.p[3] = last; break;
    case TsType::GBM: t.p[0] = last; break;
    case TsType::MA1: break;
    }
    return t;
}

std::vector<double> forecastTimeSeries(const TimeSeries& ts0, const std::vector<double>& history, int h,
                                       double p, int n, std::mt19937_64& rng) {
    if (h < 1) return {};
    TimeSeries ts = history.empty() ? ts0 : anchorAt(ts0, history.back());
    std::vector<std::vector<double>> paths(h, std::vector<double>(std::max(1, n)));
    for (int i = 0; i < std::max(1, n); ++i) {
        auto path = ts.path(h, rng);
        for (int t = 0; t < h; ++t) paths[t][i] = path[t];
    }
    std::vector<double> out(h);
    for (int t = 0; t < h; ++t) out[t] = stats::percentile(paths[t], p);
    return out;
}

// ============================================================ Holt-Winters

namespace {

struct HwState {
    double l, b;
    std::vector<double> s;
};

double hwRun(const std::vector<double>& y, int m, bool mult, double a, double be, double g, HwState& st,
             std::vector<double>* fitted, std::vector<double>* relres) {
    size_t n = y.size();
    // initialisation from the first season(s)
    double l0 = 0;
    for (int i = 0; i < m; ++i) l0 += y[i];
    l0 /= m;
    double b0 = 0;
    if (n >= size_t(2 * m) && m > 1) {
        double l1 = 0;
        for (int i = m; i < 2 * m; ++i) l1 += y[i];
        b0 = (l1 / m - l0) / m;
    } else if (m == 1) {
        b0 = y[1] - y[0];
    }
    // l0 is the level in the middle of the first season; seasonal factors are taken
    // relative to the de-trended level, and the state starts one period before y[0]
    st.b = b0;
    st.s.assign(m, mult ? 1.0 : 0.0);
    if (m > 1)
        for (int i = 0; i < m; ++i) {
            double li = l0 + (i - (m - 1) / 2.0) * b0;
            st.s[i] = mult ? (li != 0 ? y[i] / li : 1.0) : y[i] - li;
        }
    st.l = l0 - ((m - 1) / 2.0 + 1) * b0;
    double sse = 0;
    if (fitted) fitted->clear();
    for (size_t t = 0; t < n; ++t) {
        double& si = st.s[t % m];
        double yhat = mult ? (st.l + st.b) * si : st.l + st.b + si;
        if (fitted) fitted->push_back(yhat);
        double e = y[t] - yhat;
        if (t >= size_t(m)) {
            sse += e * e;
            if (relres && yhat != 0) relres->push_back(e / yhat);
        }
        double lPrev = st.l;
        if (mult) {
            st.l = a * (si != 0 ? y[t] / si : y[t]) + (1 - a) * (st.l + st.b);
            st.b = be * (st.l - lPrev) + (1 - be) * st.b;
            if (m > 1) si = g * (st.l != 0 ? y[t] / st.l : 1.0) + (1 - g) * si;
        } else {
            st.l = a * (y[t] - si) + (1 - a) * (st.l + st.b);
            st.b = be * (st.l - lPrev) + (1 - be) * st.b;
            if (m > 1) si = g * (y[t] - st.l) + (1 - g) * si;
        }
    }
    return sse;
}

double logistic(double v) { return 1 / (1 + std::exp(-v)); }

} // namespace

HoltWinters fitHoltWinters(const std::vector<double>& y0, int season, bool mult) {
    std::vector<double> y = stats::finite(y0);
    HoltWinters hw;
    hw.multiplicative = mult;
    if (season < 1) season = 1;
    if (y.size() < size_t(2 * season) + 2) season = 1;  // not enough data for seasonality
    hw.season = season;
    if (y.size() < 4) return hw;
    if (mult && *std::min_element(y.begin(), y.end()) <= 0) return hw;
    double fb;
    HwState st;
    auto best = nelderMead(
        [&](const std::vector<double>& p) {
            HwState s;
            double r = hwRun(y, season, mult, logistic(p[0]), logistic(p[1]), logistic(p[2]), s, nullptr, nullptr);
            return std::isfinite(r) ? r : kBig;
        },
        {-1, -2, -2}, {1, 1, 1}, 800, fb);
    hw.alpha = logistic(best[0]);
    hw.beta = logistic(best[1]);
    hw.gamma = logistic(best[2]);
    std::vector<double> rel;
    hw.sse = hwRun(y, season, mult, hw.alpha, hw.beta, hw.gamma, st, &hw.fitted, &rel);
    hw.level = st.l;
    hw.trend = st.b;
    // rotate seasonal factors so that seasonal[0] is the one for the next period
    hw.seasonal.resize(season);
    for (int i = 0; i < season; ++i) hw.seasonal[i] = st.s[(y.size() + i) % season];
    size_t m = y.size() > size_t(season) ? y.size() - season : 1;
    hw.sigma = mult ? std::sqrt(varOf(rel)) : std::sqrt(hw.sse / m);
    int k = 3 + (season > 1 ? season : 0);
    hw.aic = m * std::log(std::max(hw.sse / m, 1e-300)) + 2 * k;
    hw.theilU = theilU(y, hw.fitted);
    hw.ok = std::isfinite(hw.sse);
    return hw;
}

std::vector<double> HoltWinters::forecast(int h) const {
    std::vector<double> f;
    for (int i = 1; i <= h; ++i) {
        double s = seasonal.empty() ? (multiplicative ? 1 : 0) : seasonal[(i - 1) % seasonal.size()];
        f.push_back(multiplicative ? (level + i * trend) * s : level + i * trend + s);
    }
    return f;
}

std::vector<double> HoltWinters::forecastPercentile(int h, double p, int n, std::mt19937_64& rng) const {
    if (p == 0.5 || n < 2 || !(sigma > 0)) return forecast(h);
    std::normal_distribution<double> nd(0, sigma);
    std::vector<std::vector<double>> paths(h, std::vector<double>(n));
    for (int it = 0; it < n; ++it) {
        double l = level, b = trend;
        std::vector<double> s = seasonal.empty() ? std::vector<double>{multiplicative ? 1.0 : 0.0} : seasonal;
        for (int t = 0; t < h; ++t) {
            double& si = s[t % s.size()];
            double yhat = multiplicative ? (l + b) * si : l + b + si;
            double y = multiplicative ? yhat * (1 + nd(rng)) : yhat + nd(rng);
            paths[t][it] = y;
            double lp = l;
            if (multiplicative) {
                l = alpha * (si != 0 ? y / si : y) + (1 - alpha) * (l + b);
                b = beta * (l - lp) + (1 - beta) * b;
                if (s.size() > 1) si = gamma * (l != 0 ? y / l : 1) + (1 - gamma) * si;
            } else {
                l = alpha * (y - si) + (1 - alpha) * (l + b);
                b = beta * (l - lp) + (1 - beta) * b;
                if (s.size() > 1) si = gamma * (y - l) + (1 - gamma) * si;
            }
        }
    }
    std::vector<double> out(h);
    for (int t = 0; t < h; ++t) out[t] = stats::percentile(paths[t], p);
    return out;
}

HoltWinters bestHoltWinters(const std::vector<double>& y, int season, int selection) {
    HoltWinters a = fitHoltWinters(y, season, false);
    HoltWinters m = fitHoltWinters(y, season, true);
    if (!m.ok) return a;
    if (!a.ok) return m;
    if (selection == 0) return m.aic < a.aic ? m : a;
    return m.sse < a.sse ? m : a;
}

} // namespace mc
