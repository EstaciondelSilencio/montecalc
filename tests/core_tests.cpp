// MonteCalc - unit tests for the core library (no LibreOffice needed)
#include "../core/bayes.h"
#include "../core/copula.h"
#include "../core/fitting.h"
#include "../core/flofuncs.h"
#include "../core/formula.h"
#include "../core/runner.h"
#include "../core/distributions.h"
#include "../core/simulation.h"
#include "../core/specfun.h"
#include "../core/stats.h"
#include "../core/timeseries.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace mc;

static int g_fail = 0, g_pass = 0;

#define CHECK_NEAR(a, b, tol)                                                                  \
    do {                                                                                       \
        double _a = (a), _b = (b);                                                             \
        if (!(std::fabs(_a - _b) <= (tol))) {                                                  \
            std::printf("FAIL %s:%d  %s = %.10g, expected %.10g (tol %g)\n", __FILE__, __LINE__, \
                        #a, _a, _b, double(tol));                                              \
            ++g_fail;                                                                          \
        } else ++g_pass;                                                                       \
    } while (0)

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
        else ++g_pass;                                                                         \
    } while (0)

static void testSpecialFunctions() {
    CHECK_NEAR(normCdf(1.959963984540054), 0.975, 1e-12);
    CHECK_NEAR(normInv(0.975), 1.959963984540054, 1e-12);
    CHECK_NEAR(normInv(1e-10), -6.361340902404056, 1e-9);
    CHECK_NEAR(gammaP(2.5, 1.7), 0.3614300769, 1e-9);           // scipy.special.gammainc
    CHECK_NEAR(betaI(2, 3, 0.4), 0.5248, 1e-12);
    CHECK_NEAR(tCdf(2.0, 4), 0.9419417382, 1e-9);
    CHECK_NEAR(chi2Cdf(3.84145882, 1), 0.95, 1e-8);
    CHECK_NEAR(fCdf(3.0, 5, 10), 0.9344424379, 1e-9);
}

struct Case { std::string name; std::function<DistPtr()> make; double mean; double var; };

static void testDistributions() {
    std::vector<Case> cases = {
        {"uniform", [] { return makeUniform(5, 10); }, 7.5, 25.0 / 12},
        {"triangular", [] { return makeTriangular(1, 2, 6); }, 3, (1 + 4 + 36 - 2 - 6 - 12) / 18.0},
        {"pert", [] { return makePert(0, 2, 10); }, 3, NAN},
        {"normal", [] { return makeNormal(100, 15); }, 100, 225},
        {"lognormal", [] { return makeLognormal(50, 10); }, 50, 100},
        {"beta", [] { return makeBeta(2, 5, 0, 1); }, 2.0 / 7, 10.0 / (49 * 8)},
        {"gumbel", [] { return makeGumbel(0, 2); }, 2 * 0.5772156649, 3.14159265 * 3.14159265 / 6 * 4},
        {"gamma", [] { return makeGamma(3, 2); }, 6, 12},
        {"exponential", [] { return makeExponential(0.5); }, 2, 4},
        {"weibull", [] { return makeWeibull(2, 3); }, 3 * std::tgamma(1.5), 9 * (1 - 3.14159265 / 4)},
        {"chi2", [] { return makeChiSquare(5); }, 5, 10},
        {"t", [] { return makeStudentT(6); }, 0, 1.5},
        {"pareto", [] { return makePareto(6, 1); }, 6.0 / 5, 6.0 / (25 * 4)},
        {"F", [] { return makeF(5, 12); }, 1.2, 2 * 144 * 15 / (5 * 100 * 8.0)},
        {"arcsine", [] { return makeArcsine(0, 1); }, 0.5, 0.125},
        {"frechet", [] { return makeFrechet(5, 1, 0); }, std::tgamma(0.8), std::tgamma(0.6) - std::pow(std::tgamma(0.8), 2)},
        {"logistic", [] { return makeLogistic(3, 2); }, 3, 3.14159265 * 3.14159265 * 4 / 3},
        {"dunif", [] { return makeDiscreteUniform(1, 6); }, 3.5, 35.0 / 12},
        {"bernoulli", [] { return makeBernoulli(0.3); }, 0.3, 0.21},
        {"binomial", [] { return makeBinomial(20, 0.3); }, 6, 4.2},
        {"poisson", [] { return makePoisson(4.5); }, 4.5, 4.5},
        {"negbin", [] { return makeNegBinomial(3, 0.4); }, 4.5, 3 * 0.6 / 0.16},
        {"geometric", [] { return makeGeometric(0.25); }, 3, 0.75 / 0.0625},
        {"hypergeo", [] { return makeHypergeometric(50, 10, 12); }, 2.4, 12 * 0.2 * 0.8 * 38 / 49.0},
        {"dcustom", [] { return makeDiscreteCustom({1, 2, 10}, {0.5, 0.3, 0.2}); }, 3.1, 0.5 + 1.2 + 20 - 9.61},
        {"histogram", [] { return makeHistogram({0, 1}, {1, 2}, {1, 1}); }, 1, 1.0 / 3},
    };
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> uni(0, 1);
    const int n = 200000;
    for (auto& c : cases) {
        DistPtr d = c.make();
        CHECK_NEAR(d->mean(), c.mean, 1e-6 * std::max(1.0, std::fabs(c.mean)));
        std::vector<double> s(n);
        for (auto& x : s) x = d->quantile(uni(rng));
        double m = stats::mean(s), v = stats::variance(s);
        double se = std::sqrt((std::isfinite(c.var) ? c.var : v) / n);
        if (std::fabs(m - c.mean) > 5 * se + 1e-9) {
            std::printf("FAIL %s: sample mean %.6g vs %.6g\n", c.name.c_str(), m, c.mean);
            ++g_fail;
        } else ++g_pass;
        if (std::isfinite(c.var)) {
            if (std::fabs(v - c.var) > 0.03 * c.var + 1e-9) {
                std::printf("FAIL %s: sample variance %.6g vs %.6g\n", c.name.c_str(), v, c.var);
                ++g_fail;
            } else ++g_pass;
        }
        // quantile/cdf consistency
        for (double u : {0.01, 0.1, 0.37, 0.5, 0.8, 0.99}) {
            double x = d->quantile(u);
            if (d->isDiscrete()) {
                CHECK(d->cdf(x) >= u - 1e-12);
                CHECK(d->cdf(x - 1) < u + 1e-12 || x <= -1e300);
            } else {
                CHECK_NEAR(d->cdf(x), u, 1e-7);
            }
        }
    }
}

static void testTruncation() {
    auto d = makeNormal(0, 1);
    Bounds b{-1, 2};
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<double> uni(0, 1);
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 10000; ++i) {
        double x = sampleBounded(*d, uni(rng), b);
        lo = std::min(lo, x); hi = std::max(hi, x);
    }
    CHECK(lo >= -1 && hi <= 2);
    // E[X | -1<X<2] for N(0,1) = (phi(-1)-phi(2)) / (Phi(2)-Phi(-1))
    double phi = [](double x) { return std::exp(-x * x / 2) / std::sqrt(2 * kPi); }(-1);
    double phi2 = std::exp(-2.0) / std::sqrt(2 * kPi);
    CHECK_NEAR(expectedValue(*d, b), (phi - phi2) / (normCdf(2) - normCdf(-1)), 1e-4);
    auto p = makePoisson(3);
    Bounds bp{2, 4};
    for (int i = 0; i < 1000; ++i) {
        double x = sampleBounded(*p, uni(rng), bp);
        CHECK(x >= 2 && x <= 4);
    }
}

static void testStats() {
    std::vector<double> v = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
    CHECK_NEAR(stats::mean(v), 44.0 / 11, 1e-12);
    CHECK_NEAR(stats::variance(v), 5.6, 1e-12);                 // =VAR.S
    CHECK_NEAR(stats::percentile(v, 0.25), 2.5, 1e-12);         // =PERCENTILE.INC
    CHECK_NEAR(stats::skewness(v), 0.6640497716, 1e-9);         // =SKEW
    CHECK_NEAR(stats::kurtosis(v), 0.6505102041, 1e-9);                // =KURT
    std::vector<double> x = {1, 2, 3, 4, 5}, y = {2, 4, 5, 4, 5};
    CHECK_NEAR(stats::pearson(x, y), 0.7745966692, 1e-9);
    CHECK_NEAR(stats::spearman(x, y), 0.7378647874, 1e-9);
    CHECK_NEAR(stats::regressionSlope(x, y), 0.6, 1e-12);
    CHECK_NEAR(stats::regressionIntercept(x, y), 2.2, 1e-12);
    std::vector<double> pl = {-10, -5, 0, 5, 20};
    CHECK_NEAR(stats::expectedGain(pl), 12.5, 1e-12);
    CHECK_NEAR(stats::expectedLoss(pl), -7.5, 1e-12);
    CHECK_NEAR(stats::conditionalProbability(x, ">", 2, y, ">=", 5), 1.0, 1e-12);
    CHECK_NEAR(stats::conditionalProbability(x, ">=", 4, y, ">=", 4), 2.0 / 4, 1e-12);
    std::mt19937_64 rng(3);
    std::normal_distribution<double> nd(10, 2);
    std::vector<double> nrm(500);
    for (auto& z : nrm) z = nd(rng);
    bool normal = false;
    stats::andersonDarling(nrm, normal);
    CHECK(normal);
    std::exponential_distribution<double> ed(1);
    for (auto& z : nrm) z = ed(rng);
    stats::andersonDarling(nrm, normal);
    CHECK(!normal);
}

static void testCopulas() {
    std::mt19937_64 rng(7);
    const int n = 40000;
    for (int type = 0; type <= 5; ++type) {
        for (double rho : {0.7, -0.5}) {
            auto r = generateUniforms(2, n, {{0, 1, rho, type}}, rng);
            double s = stats::spearman(r.u[0], r.u[1]);
            // Archimedean copulas map the coefficient through Kendall's tau; allow a looser tolerance
            double tol = type <= 1 ? 0.02 : 0.06;
            if (std::fabs(s - rho) > tol) {
                std::printf("FAIL copula type %d rho %.2f -> spearman %.4f\n", type, rho, s);
                ++g_fail;
            } else ++g_pass;
        }
    }
    // inconsistent matrix gets repaired
    auto r = generateUniforms(3, 2000, {{0, 1, 0.9, 0}, {1, 2, 0.9, 0}, {0, 2, -0.9, 0}}, rng);
    CHECK(!r.warnings.empty());
    // archimedean variables must be in one pair only
    bool threw = false;
    try { generateUniforms(3, 10, {{0, 1, 0.5, 2}, {1, 2, 0.5, 0}}, rng); } catch (...) { threw = true; }
    CHECK(threw);
}

static void testTimeSeries() {
    std::mt19937_64 rng(11);
    TimeSeries gbm{TsType::GBM, {100, 0.05, 0.2, 1}};
    gbm.validate();
    std::uniform_real_distribution<double> uni(0, 1);
    std::vector<double> s(100000);
    for (auto& x : s) x = gbm.simulate(uni(rng), 3, rng);
    CHECK_NEAR(stats::mean(s) / gbm.expected(3), 1.0, 0.01);
    TimeSeries ar{TsType::AR1, {10, 0.8, 1, 0}};
    for (auto& x : s) x = ar.simulate(uni(rng), 5, rng);
    CHECK_NEAR(stats::mean(s), ar.expected(5), 0.02);
    double vexp = (1 - std::pow(0.64, 5)) / (1 - 0.64);
    CHECK_NEAR(stats::variance(s), vexp, 0.05 * vexp);
}

static void testSimulationFlow() {
    Simulation& sim = Simulation::instance();
    auto a = makeTriangular(0, 5, 10);
    auto b = makeNormal(100, 10);
    auto model = [&](std::vector<double>* out) {
        double x = sim.input("Volume", "Triangular", a->mean(),
                             [&](double u, std::mt19937_64&) { return a->quantile(u); });
        double y = sim.input("Price", "Normal", b->mean(),
                             [&](double u, std::mt19937_64&) { return b->quantile(u); });
        sim.correlate("Volume", "Price", -0.6, 0);
        double r = sim.output("Revenue", x * y, true);
        if (out) out->push_back(r);
        return r;
    };
    auto run = [&](const Settings& s, const std::string& label = "") {
        sim.beginRegistration();
        model(nullptr);
        auto w = sim.prepare(s);
        for (int i = 0; i < s.iterations; ++i) { sim.setIteration(i); model(nullptr); }
        sim.finish(label);
        return w;
    };
    CHECK_NEAR(model(nullptr), 500, 1e-9);  // expected-value mode
    sim.beginRegistration();
    model(nullptr);
    CHECK(sim.variables().size() == 3);
    Settings s;
    s.iterations = 5000;
    s.seed = 123;
    sim.clearResultSets();
    CHECK(run(s).empty());
    std::vector<double> vol, price, rev;
    CHECK(sim.samples("volume", vol));
    CHECK(sim.samples("Price", price));
    CHECK(sim.samples("REVENUE", rev));
    CHECK_NEAR(stats::spearman(vol, price), -0.6, 0.03);
    CHECK_NEAR(stats::mean(vol), 5, 0.1);
    CHECK(sim.resultNames(VarKind::Output).size() == 1);
    // replay
    sim.setReplayIteration(17);
    std::vector<double> rr;
    model(&rr);
    CHECK_NEAR(rr[0], rev[17], 1e-9);
    sim.setMode(Mode::Expected);
    // seed reproducibility
    sim.clearResultSets();
    run(s);
    std::vector<double> rev2;
    sim.samples("Revenue", rev2);
    CHECK_NEAR(rev2[42], rev[42], 1e-12);
    // locking
    sim.toggleLock("Price");
    sim.clearResultSets();
    run(s);
    sim.samples("Price", price);
    CHECK_NEAR(stats::stdev(price), 0, 1e-12);
    sim.toggleLock("Price");
    // variants: two result sets, selectable
    sim.clearResultSets();
    run(s, "A");
    Settings s2 = s;
    s2.seed = 7;
    run(s2, "B");
    CHECK(sim.resultSetLabels().size() == 2 && sim.activeResultSet() == 1);
    std::vector<double> va, vb;
    sim.samplesIn(0, "Revenue", va);
    sim.samplesIn(1, "Revenue", vb);
    CHECK(va[0] != vb[0]);
    sim.selectResultSet(0);
    sim.samples("Revenue", rev2);
    CHECK_NEAR(rev2[0], va[0], 0);
    // engine-side recording of an unnamed cell
    sim.clearResultSets();
    sim.beginRegistration();
    model(nullptr);
    sim.prepare(s);
    for (int i = 0; i < 10; ++i) { sim.setIteration(i); model(nullptr); sim.record("Model_B9", VarKind::Input, i * 2.0); }
    sim.finish();
    std::vector<double> rec;
    CHECK(sim.samples("model_b9", rec) && rec[3] == 6.0 && std::isnan(rec[20]));
}

static void testFitting() {
    std::mt19937_64 rng(5);
    std::gamma_distribution<double> g(3, 2);
    std::vector<double> d(800);
    for (auto& x : d) x = g(rng);
    auto f = fitDistributions(d);
    CHECK(f[0].ok && f[0].ksPValue > 0.5);
    bool gammaTop3 = false;
    for (int i = 0; i < 3; ++i) gammaTop3 |= f[i].name == "Gamma";
    CHECK(gammaTop3);
    for (auto& x : f)
        if (x.name == "Gamma") { CHECK_NEAR(x.params[0] * x.params[1], 6, 0.4); }
    // time series: AR(1) should win
    std::normal_distribution<double> nd;
    std::vector<double> y{10};
    for (int t = 1; t < 300; ++t) y.push_back(5 + 0.7 * (y.back() - 5) + nd(rng));
    auto ts = fitTimeSeries(y);
    CHECK(ts[0].name == "AR(1)" || ts[0].name == "ARMA(1,1)");
    CHECK_NEAR(ts[0].ts.p[0], 5, 0.4);
    CHECK_NEAR(ts[0].ts.p[1], 0.7, 0.1);
    CHECK(ts[0].theilU < 1);
    auto fc = forecastTimeSeries(ts[0].ts, y, 50, 0.5, 2000, rng);
    CHECK_NEAR(fc.back(), 5, 0.3);  // mean reversion
    // Holt-Winters on a seasonal trend
    std::vector<double> sn;
    for (int t = 0; t < 72; ++t) sn.push_back(100 + 2 * t + 15 * std::sin(2 * kPi * t / 12) + nd(rng) * 2);
    auto hw = bestHoltWinters(sn, 12, 0);
    CHECK(hw.ok);
    CHECK_NEAR(hw.forecast(1)[0], 100 + 2 * 72 + 15 * std::sin(2 * kPi * 72 / 12), 6);
    auto lo = hw.forecastPercentile(3, 0.05, 4000, rng), hi = hw.forecastPercentile(3, 0.95, 4000, rng);
    CHECK(lo[2] < hw.forecast(3)[2] && hw.forecast(3)[2] < hi[2]);
}

static void testBayes() {
    std::mt19937_64 rng(3);
    auto bf = bayes::bayesFactor(0.5, 0.62, 200, 0.95);
    CHECK(bf.bf10 > 3);
    CHECK_NEAR(bf.posteriorMean, 125.0 / 202, 1e-9);
    auto bf0 = bayes::bayesFactor(0.5, 0.5, 200, 0);
    CHECK(bf0.bf10 < 1);
    std::normal_distribution<double> a(10, 2), b(12, 1);
    std::vector<double> p(3000), l(3000);
    for (auto& x : p) x = a(rng);
    for (auto& x : l) x = b(rng);
    double exact = (10 / 4.0 + 12) / (1 / 4.0 + 1);
    CHECK_NEAR(stats::mean(bayes::posteriorMCMC(p, l, 6000, 500, 2, 60, false, rng)), exact, 0.15);
    CHECK_NEAR(stats::mean(bayes::posteriorMCMC(p, l, 6000, 500, 2, 60, true, rng)), exact, 0.15);
    CHECK_NEAR(stats::mean(bayes::posteriorGrid(p, l, 6000, rng)), exact, 0.15);
    auto mix = bayes::combine({p, l}, {1, 3}, 20000, rng);
    CHECK_NEAR(stats::mean(mix), 0.25 * 10 + 0.75 * 12, 0.1);
    // regression
    std::vector<double> y;
    bayes::Mat X;
    for (int i = 0; i < 200; ++i) { double x = i / 10.0; X.push_back({x}); y.push_back(3 + 2 * x + a(rng) - 10); }
    double s;
    auto be = bayes::ols(y, X, s);
    CHECK_NEAR(be[1], 2, 0.1);
    CHECK_NEAR(s, 2, 0.3);
    std::vector<double> xs;
    for (auto& r : X) xs.push_back(r[0]);
    auto rm = bayes::repeatedMedian(y, xs, s);
    CHECK_NEAR(rm[1], 2, 0.15);
    auto br = bayes::bayesRegression(y, X, 1000, false, 2, rng);
    auto pred = bayes::predictive(br, {5}, true, rng);
    CHECK_NEAR(stats::mean(pred), 13, 0.5);
    CHECK_NEAR(stats::stdev(pred), 2, 0.4);
    // naive Bayes on two separated classes
    bayes::Mat fx;
    std::vector<std::string> cls;
    for (int i = 0; i < 200; ++i) {
        bool c = i % 2;
        fx.push_back({(c ? 5.0 : 0.0) + a(rng) - 10, double(c)});
        cls.push_back(c ? "yes" : "no");
    }
    bayes::NaiveBayes nb;
    nb.train(fx, cls, {});
    CHECK(nb.predict({5, 1}) == "yes" && nb.predict({0, 0}) == "no");
    // KDE integrates to ~1
    auto kh = bayes::kdeHistogram(p, 0, 20, 0, 200);
    double area = 0;
    for (auto& r : kh) area += r[2] * (r[1] - r[0]);
    CHECK_NEAR(area, 1, 0.02);
    // Benford: powers of 2 follow Benford's law
    std::vector<double> pw;
    double v = 1;
    for (int i = 0; i < 1000; ++i) { pw.push_back(v); v *= 2; if (v > 1e300) v /= 1e300; }
    auto bt = bayes::benford(pw);
    CHECK(bt.pValue > 0.05);
}

static void testCopulaExtras() {
    std::mt19937_64 rng(9);
    for (int t = 6; t <= 8; ++t) {
        auto c = generateUniforms(2, 20000, {{0, 1, 0.45, t}}, rng);
        CHECK_NEAR(stats::spearman(c.u[0], c.u[1]), 0.45, 0.06);
        auto d = generateUniforms(2, 3000, {{0, 1, 0.5, t}}, rng);
        CHECK(fitCopula(d.u[0], d.u[1], rng) == t);
    }
}

static void testFloFunctions() {
    Simulation& sim = Simulation::instance();
    sim.setMode(Mode::Expected);
    auto call = [](const char* fn, std::vector<Value> args) { return evalFlo(*findFlo(fn), args); };
    using V = Value;
    // MC FLO syntax (wiki): parameters; Variable_name; Number; [Min]; [Max]; [Round]
    CHECK_NEAR(call("FLOsimula_Triangular", {V::number(1), V::number(6), V::number(2), V::text("T")}), 3, 1e-12);
    CHECK_NEAR(call("FLOsimula_PERT", {V::number(0), V::number(100), V::number(30), V::text("P")}), (0 + 4 * 30 + 100) / 6.0, 1e-9);
    CHECK_NEAR(call("FLOSIMULA_DISCRETEUNIFORM", {V::number(300), V::number(500)}), 400, 1e-12);  // unnamed
    CHECK_NEAR(call("FLOsimula_DiscreteUniforme", {V::number(1), V::number(11), V::text("d")}), 6, 1e-12);  // alias
    CHECK_NEAR(call("FLOsimula_Uniforme", {V::number(1000), V::number(1200), V::text("Anzahl_Kunden")}), 1100, 1e-12);
    CHECK_NEAR(call("ORG.MONTECALC.MONTECALC.FLOSIMULA_NORMAL", {V::number(10), V::number(2), V(), V(), V::number(8), V::number(12)}), 10, 1e-9);
    CHECK_NEAR(call("FLOsimula_Normal", {V::number(10), V::number(2), V(), V(), V(), V(), V::boolean(true)}), 10, 0);  // Round TRUE
    CHECK_NEAR(call("FLOsimula_Uniform", {V::number(0), V::number(1), V::text("U"), V::number(4)}), 2, 1e-12);  // Number = 4
    CHECK_NEAR(call("FLOsimula_TruncNormal", {V::number(0), V::number(1), V::number(0), V::number(10)}), std::sqrt(2 / kPi), 1e-3);
    CHECK_NEAR(call("FLOsimula_LogNormal", {V::number(5), V::number(0), V::number(0.3)}), 5 + std::exp(0.045), 1e-9);
    CHECK_NEAR(call("FLOsimula_Gamma", {V::number(5), V::number(3), V::number(7)}), 5 + 21, 1e-9);        // Shift + scale*shape
    CHECK_NEAR(call("FLOsimula_Exponential", {V::number(1), V::number(2)}), 3, 1e-9);                     // Shift + mean
    CHECK_NEAR(call("FLOsimula_Weibull", {V::number(0), V::number(1), V::number(5)}), std::tgamma(1.2), 1e-9);
    CHECK_NEAR(call("FLOsimula_Pareto", {V::number(1), V::number(10)}), 10.0 / 9, 1e-9);
    CHECK_NEAR(call("FLOsimula_Binomial", {V::number(0.25), V::number(10)}), 2.5, 1e-9);                  // (Probability; Tries)
    CHECK_NEAR(call("FLOsimula_Hypergeometric", {V::number(10), V::number(45), V::number(20)}), 10 * 20 / 45.0, 1e-9);
    CHECK_NEAR(call("FLOsimula_Pascal", {V::number(0.4), V::number(5)}), 5 * 0.4 / 0.6, 1e-9);           // failures, P = failure prob.
    CHECK_NEAR(call("FLOsimula_studentt", {V::number(5)}), 0, 1e-12);
    CHECK_NEAR(call("FLOsimula_Fisher", {V::number(10), V::number(100)}), 100.0 / 98, 1e-9);
    CHECK_NEAR(call("FLOsimula_TemporalS_WPmodel", {V::number(100), V::number(0.01), V::number(0.1), V::number(1), V::number(0), V::text("p"), V::number(2)}),
               100 * std::exp(0.2), 1e-9);
    CHECK_NEAR(call("FLOsimula_TemporalS_WPmodel", {V::number(100), V::number(0.01), V::number(0), V::number(0), V::text("p")}), 100, 1e-9);
    CHECK_NEAR(call("FLOsimula_TemporalS_ARmodel", {V::number(3), V::number(0.3), V::number(1), V::number(0), V::text("AR")}), 3, 1e-12);
    CHECK_NEAR(call("FLOsimula_Bounded", {V::number(15), V::number(0), V::number(10), V::text("B")}), 15, 0);  // shows the reference
    CHECK_NEAR(call("FLOsimula_output", {V::text("NPV")}), 0, 0);
    CHECK_NEAR(call("FLOsimula_output", {V::text("NPV"), V::number(7)}), 7, 0);
    CHECK_NEAR(call("FLOsimula_correlation", {V::text("A"), V::text("B"), V::number(0.8), V::number(0)}), 0.8, 0);
    {   // the name must come after the parameters
        bool bad = false;
        try { call("FLOsimula_Normal", {V::text("Price"), V::number(10), V::number(1)}); } catch (...) { bad = true; }
        CHECK(bad);
    }
    // MC FLO correlation type codes
    CHECK(copulaFromMcflo(1) == 2 && copulaFromMcflo(5) == 1 && copulaFromMcflo(7) == 8 && copulaFromMcflo(8) == 7);
    for (int t = 0; t <= kMaxCopulaType; ++t) CHECK(copulaToMcflo(copulaFromMcflo(t)) == t);
    CHECK(!copulaIsBivariateOnly(0) && !copulaIsBivariateOnly(5) && copulaIsBivariateOnly(1));
    bool threw = false;
    try { call("FLOsimula_Uniform", {V::number(5), V::number(1)}); } catch (...) { threw = true; }
    CHECK(threw);
    // user-defined: the manual's histogram template (8 values, 12 rows)
    std::vector<std::vector<Value>> h;
    double rowsv[12][3] = {{266347, 296025, 1}, {296025, 325703, 0}, {325703, 355381, 0}, {355381, 385059, 2},
                           {385059, 414736, 1}, {414736, 444414, 0}, {444414, 474092, 1}, {474092, 503770, 1},
                           {503770, 533448, 0}, {533448, 563126, 1}, {563126, 592804, 0}, {592804, 622481, 1}};
    for (auto& r : rowsv) h.push_back({V::number(r[0]), V::number(r[1]), V::number(r[2])});
    double m = call("FLOsimula_UserDefined", {V::array(h), V::number(12), V::text("Hist")});
    CHECK(m > 400000 && m < 470000);
    // quantile table in the manual's layout: (quantile, value) rows, last row (min, max)
    std::vector<std::vector<Value>> q = {{V::number(0.1), V::number(120)}, {V::number(0.5), V::number(140)},
                                         {V::number(0.9), V::number(165)}, {V::number(100), V::number(180)}};
    auto qd = userDefinedDistribution(V::array(q), 0);
    CHECK_NEAR(qd->quantile(0.5), 140, 1e-9);
    CHECK_NEAR(qd->quantile(0.0), 100, 1e-9);
    CHECK(floVariableName(*findFlo("FLOsimula_Normal"), {V::number(1), V::number(2), V::text("Price")}) == "Price");
    CHECK(floVariableName(*findFlo("FLOsimula_Normal"), {V::number(1), V::number(2)}).empty());
    CHECK(floVariableName(*findFlo("FLOsimula_TemporalS_WPmodel"), {V::number(100), V::number(0.01), V::number(0), V::number(0), V::text("W")}) == "W");
    CHECK(floVariableName(*findFlo("FLOsimula_TemporalS_WPmodel"),
                          {V::number(100), V::number(0.01), V::number(0), V::number(1), V::number(0), V::text("W5")}) == "W5");
    CHECK(floVariableName(*findFlo("FLOsimula_Bounded"), {V::number(1), V::number(0), V::number(2), V::text("Bd")}) == "Bd");
}

// time series paths, Bounded truncation and correlation codes in a simulation run
static void testMcfloSemantics() {
    using V = Value;
    Simulation& sim = Simulation::instance();
    auto ar = findFlo("FLOsimula_TemporalS_ARmodel");
    auto wp = findFlo("FLOsimula_TemporalS_WPmodel");
    auto tri = findFlo("FLOsimula_Triangular");
    auto bnd = findFlo("FLOsimula_Bounded");
    auto cor = findFlo("FLOsimula_correlation");
    std::vector<V> aArgs{V::number(10), V::number(0.8), V::number(1), V::number(0), V::text("ARpath")};
    std::vector<V> rArgs{V::number(10), V::number(0.8), V::number(1), V::number(1), V::text("ARrestart")};
    std::vector<V> gArgs{V::number(100), V::number(0.1), V::number(0), V::number(1), V::number(0), V::text("GBM3"), V::number(3)};
    std::vector<V> tArgs{V::number(10), V::number(150), V::number(120), V::text("Tri")};
    auto run = [&](int n, std::vector<std::vector<double>>& out) {
        sim.clearResultSets();
        sim.beginRegistration();
        auto pass = [&] {
            out[0].push_back(evalFlo(*ar, aArgs));
            out[1].push_back(evalFlo(*ar, rArgs));
            out[2].push_back(evalFlo(*wp, gArgs));
            double t = evalFlo(*tri, tArgs);
            out[3].push_back(t);
            out[4].push_back(evalFlo(*bnd, {V::number(t), V::number(30), V::number(140), V::text("Bounded")}));
            evalFlo(*cor, {V::text("Tri"), V::text("ARrestart"), V::number(0.7), V::number(3)});   // 3 = Gumbel in MC FLO
        };
        pass();
        for (auto& o : out) o.clear();
        Settings s;
        s.iterations = n;
        s.seed = 11;
        sim.prepare(s);
        for (int i = 0; i < n; ++i) { sim.setIteration(i); pass(); }
        sim.finish();
    };
    std::vector<std::vector<double>> o(5);
    run(20000, o);
    // continuing path: stationary AR(1) with variance 1/(1-0.8^2) and lag-1 autocorrelation 0.8
    CHECK_NEAR(stats::mean(o[0]), 10, 0.15);
    CHECK_NEAR(stats::variance(o[0]), 1 / (1 - 0.64), 0.25);
    std::vector<double> lag0(o[0].begin(), o[0].end() - 1), lag1(o[0].begin() + 1, o[0].end());
    CHECK_NEAR(stats::pearson(lag0, lag1), 0.8, 0.03);
    // UseBefore = 1: one step from Start in every iteration
    CHECK_NEAR(stats::variance(o[1]), 1, 0.05);
    std::vector<double> r0(o[1].begin(), o[1].end() - 1), r1(o[1].begin() + 1, o[1].end());
    CHECK_NEAR(stats::pearson(r0, r1), 0, 0.03);
    // Number = 3: period 3 of a path starting at 100, log sd = 0.1 * sqrt(3)
    std::vector<double> lg;
    for (double v : o[2]) lg.push_back(std::log(v / 100));
    CHECK_NEAR(stats::stdev(lg), 0.1 * std::sqrt(3.0), 0.01);
    // Bounded: truncation of the triangular variable to [30, 140]
    CHECK(stats::min(o[4]) >= 30 && stats::max(o[4]) <= 140);
    CHECK(stats::min(o[3]) < 30 && stats::max(o[3]) > 140);
    CHECK(std::count(o[4].begin(), o[4].end(), 30.0) + std::count(o[4].begin(), o[4].end(), 140.0) < 5);   // not clamped
    CHECK_NEAR(stats::percentile(o[4], 0.01), 33.4, 0.8);   // quantile of the truncated triangle
    std::vector<double> tri2, ar2;
    sim.samples("Tri", tri2);
    sim.samples("ARrestart", ar2);
    CHECK_NEAR(stats::spearman(tri2, ar2), 0.7, 0.05);
}

// tiny in-memory workbook for the formula engine
struct MemSource : formula::Source {
    std::map<formula::CellAddr, formula::CellContent> cells;
    void set(int col, int row, const std::string& f) {
        formula::CellContent c;
        if (!f.empty() && f[0] == '=') { c.type = formula::CellContent::Formula; c.text = f; }
        else { c.type = formula::CellContent::Number; c.value = std::atof(f.c_str()); }
        cells[{0, col, row}] = c;
    }
    formula::CellContent cell(const formula::CellAddr& a) override {
        auto it = cells.find(a);
        return it == cells.end() ? formula::CellContent{} : it->second;
    }
    int sheetIndex(const std::string& n) override { return n == "Model" ? 0 : -1; }
    bool namedRange(const std::string& n, formula::RangeAddr& r) override {
        if (n != "FLO_i_Rate") return false;
        r.a = r.b = {0, 1, 0};
        return true;
    }
};

static void testFormulaEngine() {
    using namespace formula;
    int c, r;
    CHECK(parseCellName("$AB$12", c, r) && c == 27 && r == 11);
    CHECK(columnName(27) == "AB");
    MemSource src;
    src.set(1, 0, "0.1");                                                        // B1 rate
    src.set(1, 1, "=ORG.MONTECALC.MONTECALC.FLOSIMULA_TRIANGULAR(80;120;100;\"Vol\")");  // B2
    src.set(1, 2, "=FLOSIMULA_NORMAL(10;1;\"Price\")");                         // B3
    src.set(1, 3, "=B2*B3");                                                     // B4
    src.set(1, 4, "=NPV(FLO_i_Rate;B4;B4;B4)-2000");                             // B5
    src.set(1, 5, "=IF(AND(B5>0;$Model.B4>=1000);ROUND(B5/1000;1);-1)");         // B6
    src.set(1, 6, "=SUM(B2:B4)+MAX(B2;B3)^2-2%");                                // B7
    src.set(1, 7, "=MOD(-7;3)&\"x\"");                                           // B8
    src.set(1, 8, "=IFERROR(1/0;42)");                                           // B9
    src.set(1, 9, "=SUMPRODUCT(B2:B3;B2:B3)");                                   // B10
    src.set(1, 10, "=VLOOKUP(1;B1:B3;1)");                                       // B11 unsupported
    Model m;
    std::string why;
    CHECK(m.build(src, {{0, 1, 4}, {0, 1, 5}, {0, 1, 6}, {0, 1, 7}, {0, 1, 8}, {0, 1, 9}}, why));
    Simulation::instance().setMode(Mode::Expected);
    m.beginIteration();
    double npv = 1000 * (1 / 1.1 + 1 / 1.21 + 1 / 1.331) - 2000;
    CHECK_NEAR(m.value({0, 1, 4}).num, npv, 1e-9);
    CHECK_NEAR(m.value({0, 1, 5}).num, std::round(npv / 100) / 10, 1e-12);
    CHECK_NEAR(m.value({0, 1, 6}).num, 100 + 10 + 1000 + 10000 - 0.02, 1e-9);
    CHECK(m.value({0, 1, 7}).str == "2x");
    CHECK_NEAR(m.value({0, 1, 8}).num, 42, 0);
    CHECK_NEAR(m.value({0, 1, 9}).num, 10100, 1e-9);
    Model bad;
    CHECK(!bad.build(src, {{0, 1, 10}}, why) && why.find("VLOOKUP") != std::string::npos);
    // in a simulation the memory model draws named inputs from the shared state
    Simulation& sim = Simulation::instance();
    sim.clearResultSets();
    sim.beginRegistration();
    m.beginIteration();
    m.value({0, 1, 4});
    Settings s;
    s.iterations = 3000;
    s.seed = 5;
    sim.prepare(s);
    std::vector<double> npvs;
    for (int i = 0; i < s.iterations; ++i) {
        sim.setIteration(i);
        m.beginIteration();
        npvs.push_back(m.value({0, 1, 4}).num);
    }
    sim.finish();
    std::vector<double> vol;
    sim.samples("Vol", vol);
    CHECK_NEAR(stats::mean(vol), 100, 1);
    CHECK_NEAR(stats::mean(npvs), npv, 30);
    CHECK(stats::stdev(npvs) > 50);
}

// Memory Calculation on worker threads: the same results for any thread count; stop keeps
// the completed iterations; pause holds the workers
static std::vector<double> parallelRun(int threads, int iterations, mc::RunControl& ctl, std::vector<double>* unnamedOut,
                                       const std::function<void()>& idle) {
    using namespace formula;
    MemSource src;
    src.set(1, 0, "0.1");
    src.set(1, 1, "=FLOSIMULA_TRIANGULAR(80;120;100;\"Vol\")");
    src.set(1, 2, "=FLOSIMULA_NORMAL(10;1;\"Price\")");
    src.set(1, 3, "=B2*B3+FLOSIMULA_NORMAL(0;5)");   // an unnamed input: drawn from the worker's generator
    src.set(1, 4, "=NPV(FLO_i_Rate;B4;B4;B4)-2000");
    Simulation& sim = Simulation::instance();
    sim.clearResultSets();
    sim.beginRegistration();
    Model reg;
    std::string why;
    std::vector<CellAddr> roots = {{0, 1, 1}, {0, 1, 2}, {0, 1, 3}, {0, 1, 4}};
    reg.build(src, roots, why);
    reg.beginIteration();
    reg.value({0, 1, 4});
    sim.setVariableInfo("NPV", VarKind::Output, "Output", "B5");
    sim.setVariableInfo("Unnamed", VarKind::Output, "Output", "B4");
    Settings s;
    s.iterations = iterations;
    s.seed = 11;
    sim.prepare(s);
    int npvIdx = sim.variableIndex("NPV"), unIdx = sim.variableIndex("Unnamed");
    Model base;
    base.build(src, roots, why);
    mc::runParallel(0, iterations, threads,
                    [&]() -> mc::IterationFn {
                        auto m = std::make_shared<Model>(base);   // every worker has its own copy
                        return [m, npvIdx, unIdx, &sim](int) {
                            m->beginIteration();
                            sim.recordAt(npvIdx, m->value({0, 1, 4}).num);
                            sim.recordAt(unIdx, m->value({0, 1, 3}).num);
                        };
                    },
                    ctl, idle);
    sim.finish();
    std::vector<double> npv;
    sim.samples("NPV", npv);
    if (unnamedOut) sim.samples("Unnamed", *unnamedOut);
    return npv;
}

static void testParallelRun() {
    mc::RunControl c1, c4, c8;
    std::vector<double> u1, u4;
    auto one = parallelRun(1, 4000, c1, &u1, nullptr);
    auto four = parallelRun(4, 4000, c4, &u4, nullptr);
    auto eight = parallelRun(8, 4000, c8, nullptr, nullptr);
    CHECK(one.size() == 4000 && four.size() == 4000 && eight.size() == 4000);
    CHECK(one == four && four == eight);   // independent of the thread count
    CHECK(u1 == u4);
    CHECK(c4.threads == 4);
    double npv = 1000 * (1 / 1.1 + 1 / 1.21 + 1 / 1.331) - 2000;
    CHECK_NEAR(stats::mean(one), npv, 30);
    // stop after a while: the completed iterations are the result
    mc::RunControl stop;
    int calls = 0;
    auto part = parallelRun(4, 200000, stop, nullptr, [&] {
        if (++calls == 3) stop.stop = true;
    });
    CHECK(!part.empty() && part.size() < 200000);
    CHECK(int(part.size()) == Simulation::instance().iterations());
    for (double v : part) CHECK(std::isfinite(v));
    // pause: no progress while paused, then resume to the end
    mc::RunControl pause;
    pause.pause = true;
    int idleCalls = 0, doneWhilePaused = -1;
    auto all = parallelRun(3, 3000, pause, nullptr, [&] {
        ++idleCalls;
        if (idleCalls == 5) doneWhilePaused = Simulation::instance().completedIterations();
        if (idleCalls == 6) pause.pause = false;
    });
    CHECK(doneWhilePaused == 0);
    CHECK(all.size() == 3000);
    // a worker error stops the run and reaches the caller
    Simulation& sim = Simulation::instance();
    sim.beginRegistration();
    sim.setVariableInfo("X", VarKind::Output, "Output", "A1");
    Settings s;
    s.iterations = 1000;
    sim.prepare(s);
    mc::RunControl err;
    bool thrown = false;
    try {
        mc::runParallel(0, 1000, 4, [] { return mc::IterationFn([](int i) { if (i == 500) throw std::runtime_error("boom"); }); }, err, nullptr);
    } catch (const std::exception& e) {
        thrown = std::string(e.what()) == "boom";
    }
    sim.abort();
    CHECK(thrown);
}

int main() {
    testSpecialFunctions();
    testDistributions();
    testTruncation();
    testStats();
    testCopulas();
    testTimeSeries();
    testSimulationFlow();
    testFitting();
    testBayes();
    testCopulaExtras();
    testFloFunctions();
    testMcfloSemantics();
    testFormulaEngine();
    testParallelRun();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
