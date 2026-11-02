// MonteCalc - unit tests for the core library (no LibreOffice needed)
#include "../core/copula.h"
#include "../core/distributions.h"
#include "../core/simulation.h"
#include "../core/specfun.h"
#include "../core/stats.h"
#include "../core/timeseries.h"

#include <cmath>
#include <cstdio>
#include <functional>
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
        double r = sim.output("Revenue", x * y);
        if (out) out->push_back(r);
        return r;
    };
    CHECK_NEAR(model(nullptr), 500, 1e-9);  // expected-value mode
    sim.beginRegistration();
    model(nullptr);
    CHECK(sim.variables().size() == 3);
    Settings s;
    s.iterations = 5000;
    s.seed = 123;
    auto w = sim.prepare(s);
    CHECK(w.empty());
    for (int i = 0; i < s.iterations; ++i) { sim.setIteration(i); model(nullptr); }
    sim.finish();
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
    sim.beginRegistration(); model(nullptr); sim.prepare(s);
    for (int i = 0; i < s.iterations; ++i) { sim.setIteration(i); model(nullptr); }
    sim.finish();
    std::vector<double> rev2;
    sim.samples("Revenue", rev2);
    CHECK_NEAR(rev2[42], rev[42], 1e-12);
    // locking
    sim.toggleLock("Price");
    sim.beginRegistration(); model(nullptr); sim.prepare(s);
    for (int i = 0; i < s.iterations; ++i) { sim.setIteration(i); model(nullptr); }
    sim.finish();
    sim.samples("Price", price);
    CHECK_NEAR(stats::stdev(price), 0, 1e-12);
    sim.toggleLock("Price");
}

int main() {
    testSpecialFunctions();
    testDistributions();
    testTruncation();
    testStats();
    testCopulas();
    testTimeSeries();
    testSimulationFlow();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
