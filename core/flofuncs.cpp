// MonteCalc - semantics of the FLOsimula_* model functions
#include "flofuncs.h"
#include "simulation.h"
#include "stats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <sstream>

namespace mc {

namespace {

using P = std::vector<double>;

FloFunction dist(const char* label, bool discrete, const char* desc, std::vector<FloParam> params,
                 std::function<DistPtr(const P&)> make) {
    FloFunction f;
    f.name = std::string("FLOsimula_") + label;
    f.kind = FloFunction::Dist;
    f.discrete = discrete;
    f.label = label;
    f.description = desc;
    f.params = std::move(params);
    f.make = std::move(make);
    return f;
}

FloFunction ts(const char* label, TsType t, const char* desc, std::vector<FloParam> params) {
    FloFunction f;
    f.name = std::string("FLOsimula_") + label;
    f.kind = FloFunction::TimeSeries;
    f.label = label;
    f.description = desc;
    f.params = std::move(params);
    f.ts = t;
    return f;
}

FloFunction other(const char* name, FloFunction::Kind k, const char* desc) {
    FloFunction f;
    f.name = name;
    f.kind = k;
    f.label = name + 10;
    f.description = desc;
    return f;
}

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

std::string describe(const std::string& label, const P& p) {
    std::ostringstream os;
    os << label << "(";
    for (size_t i = 0; i < p.size(); ++i) os << (i ? "; " : "") << p[i];
    os << ")";
    return os.str();
}

bool isNameArg(const Value& v) { return v.kind == Value::String && !v.str.empty() && !v.isNumberText(); }

double optNum(const std::vector<Value>& a, size_t i, double def) {
    if (i >= a.size() || a[i].isEmpty()) return def;
    return a[i].asNumber();
}

double roundTo(double v, int decimals) {
    double f = std::pow(10.0, decimals);
    return std::round(v * f) / f;
}

struct Options {
    Bounds bounds;
    int round = -1;   // decimals, -1 = no rounding
    int number = 1;
};

Options readOptions(const std::vector<Value>& a, size_t i) {
    Options o;
    o.bounds.lo = optNum(a, i, o.bounds.lo);
    o.bounds.hi = optNum(a, i + 1, o.bounds.hi);
    if (o.bounds.lo > o.bounds.hi) throw std::invalid_argument("Min must not exceed Max");
    if (i + 2 < a.size() && !a[i + 2].isEmpty()) {
        if (a[i + 2].kind == Value::Bool) o.round = a[i + 2].num != 0 ? 0 : -1;
        else o.round = int(std::lround(a[i + 2].asNumber()));
        if (o.round > 12) o.round = 12;
    }
    double n = optNum(a, i + 3, 1);
    if (n < 1 || n > 1e7) throw std::invalid_argument("Number must be between 1 and 10,000,000");
    o.number = int(std::lround(n));
    if (i + 4 < a.size())
        for (size_t k = i + 4; k < a.size(); ++k)
            if (!a[k].isEmpty()) throw std::invalid_argument("too many arguments");
    return o;
}

double inputWith(const std::string& name, std::string desc, std::shared_ptr<Distribution> d, const Options& o,
                 bool userDefined) {
    auto& sim = Simulation::instance();
    double expected = std::nan("");
    bool needExpected = sim.mode() != Mode::Simulate && sim.mode() != Mode::Random;
    if (needExpected || (!name.empty() && sim.isLocked(name))) {
        expected = expectedValue(*d, o.bounds) * o.number;
        if (o.round >= 0) expected = roundTo(expected, o.round);
    }
    if (o.bounds.active()) desc += " bounded";
    if (o.number > 1) desc += " x" + std::to_string(o.number);
    Bounds b = o.bounds;
    int n = o.number, r = o.round;
    return Simulation::instance().input(
        name, desc, expected,
        [d, b, n, r](double u, std::mt19937_64& rng) {
            double v = compoundSample(*d, b, u, n, rng);
            return r >= 0 ? roundTo(v, r) : v;
        },
        userDefined);
}

std::vector<std::vector<double>> columnsOf(const Value& data) {
    std::vector<std::vector<double>> cols;
    if (data.kind != Value::Array) {
        if (data.isNumeric()) cols.push_back({data.num});
        return cols;
    }
    size_t nc = 0;
    for (const auto& row : *data.arr) nc = std::max(nc, row.size());
    cols.resize(nc);
    for (const auto& row : *data.arr)
        for (size_t j = 0; j < nc; ++j)
            cols[j].push_back(j < row.size() && row[j].kind == Value::Number ? row[j].num : std::nan(""));
    return cols;
}

} // namespace

DistPtr userDefinedDistribution(const Value& data, int bins) {
    auto cols = columnsOf(data);
    size_t rows = cols.empty() ? 0 : cols[0].size();
    if (cols.size() >= 3 && rows >= 1) {
        // histogram template: Min, Max, Number [, Number cum., Percent, Percent cum.]
        std::vector<double> mins, maxs, cnt;
        for (size_t i = 0; i < rows; ++i)
            if (std::isfinite(cols[0][i]) && std::isfinite(cols[1][i]) && std::isfinite(cols[2][i])) {
                mins.push_back(cols[0][i]); maxs.push_back(cols[1][i]); cnt.push_back(cols[2][i]);
            }
        return makeHistogram(mins, maxs, cnt);
    }
    if (cols.size() == 2 && rows >= 2) {
        // quantile table: (quantile, value) rows; the manual's layout ends with a (min, max) row
        std::vector<std::pair<double, double>> pts;
        for (size_t i = 0; i < rows; ++i)
            if (std::isfinite(cols[0][i]) && std::isfinite(cols[1][i])) pts.push_back({cols[0][i], cols[1][i]});
        auto tryBuild = [](std::vector<std::pair<double, double>> qv) -> DistPtr {
            std::sort(qv.begin(), qv.end());
            std::vector<double> x, p;
            for (auto& [q, v] : qv) { p.push_back(q); x.push_back(v); }
            for (size_t i = 1; i < x.size(); ++i)
                if (x[i] < x[i - 1]) return nullptr;
            return makePiecewiseLinear(x, p);
        };
        bool allProb = std::all_of(pts.begin(), pts.end(), [](auto& q) { return q.first >= 0 && q.first <= 1; });
        if (allProb) {
            try { if (auto d = tryBuild(pts)) return d; } catch (...) {}
        }
        if (pts.size() >= 2) {
            double mn = pts.back().first, mx = pts.back().second;
            std::vector<std::pair<double, double>> q(pts.begin(), pts.end() - 1);
            q.push_back({0.0, mn});
            q.push_back({1.0, mx});
            if (auto d = tryBuild(q)) return d;
        }
        throw std::invalid_argument("the quantile table is not monotone");
    }
    std::vector<double> v = data.numbers();
    return makeEmpirical(v, bins);
}

const std::vector<FloFunction>& floFunctions() {
    static const std::vector<FloFunction> f = [] {
        std::vector<FloFunction> v;
        // ---- continuous ----
        v.push_back(dist("Uniform", false, "Continuous uniform distribution.", {{"Minimum", "Lower limit."}, {"Maximum", "Upper limit."}},
                         [](const P& p) { return makeUniform(p[0], p[1]); }));
        v.push_back(dist("Triangle", false, "Triangle distribution.", {{"Minimum", "Lower limit."}, {"Mode", "Most likely value."}, {"Maximum", "Upper limit."}},
                         [](const P& p) { return makeTriangular(p[0], p[1], p[2]); }));
        v.push_back(dist("PERT", false, "PERT distribution.", {{"Minimum", "Lower limit."}, {"Mode", "Most likely value."}, {"Maximum", "Upper limit."}},
                         [](const P& p) { return makePert(p[0], p[1], p[2]); }));
        v.push_back(dist("Normal", false, "Normal distribution.", {{"Mean", "Mean."}, {"StdDev", "Standard deviation."}},
                         [](const P& p) { return makeNormal(p[0], p[1]); }));
        v.push_back(dist("Lognormal", false, "Lognormal distribution (mean and standard deviation of the variable).",
                         {{"Mean", "Mean (> 0)."}, {"StdDev", "Standard deviation."}}, [](const P& p) { return makeLognormal(p[0], p[1]); }));
        v.push_back(dist("Beta", false, "Beta distribution scaled to [Minimum, Maximum].",
                         {{"Alpha", "Shape alpha."}, {"Beta", "Shape beta."}, {"Minimum", "Lower end."}, {"Maximum", "Upper end."}},
                         [](const P& p) { return makeBeta(p[0], p[1], p[2], p[3]); }));
        v.push_back(dist("Gumbel", false, "Gumbel (extreme value) distribution.", {{"Location", "Location."}, {"Scale", "Scale (> 0)."}},
                         [](const P& p) { return makeGumbel(p[0], p[1]); }));
        v.push_back(dist("Gamma", false, "Gamma distribution.", {{"Shape", "Shape k."}, {"Scale", "Scale theta."}},
                         [](const P& p) { return makeGamma(p[0], p[1]); }));
        v.push_back(dist("Exponential", false, "Exponential distribution.", {{"Rate", "Rate lambda (mean = 1/lambda)."}},
                         [](const P& p) { return makeExponential(p[0]); }));
        v.push_back(dist("TruncatedNormal", false, "Truncated normal distribution.",
                         {{"Mean", "Mean of the untruncated normal."}, {"StdDev", "Standard deviation."}, {"LowerLimit", "Lower limit."}, {"UpperLimit", "Upper limit."}},
                         nullptr));
        v.push_back(dist("Weibull", false, "Weibull distribution.", {{"Shape", "Shape k."}, {"Scale", "Scale lambda."}},
                         [](const P& p) { return makeWeibull(p[0], p[1]); }));
        v.push_back(dist("Chi2", false, "Chi-square distribution.", {{"DegreesOfFreedom", "Degrees of freedom."}},
                         [](const P& p) { return makeChiSquare(p[0]); }));
        v.push_back(dist("StudentT", false, "Student-t distribution.", {{"DegreesOfFreedom", "Degrees of freedom."}},
                         [](const P& p) { return makeStudentT(p[0]); }));
        v.push_back(dist("Pareto", false, "Pareto distribution.", {{"Shape", "Shape alpha."}, {"Scale", "Scale (minimum value)."}},
                         [](const P& p) { return makePareto(p[0], p[1]); }));
        v.push_back(dist("F", false, "F distribution.", {{"DegreesOfFreedom1", "Numerator degrees of freedom."}, {"DegreesOfFreedom2", "Denominator degrees of freedom."}},
                         [](const P& p) { return makeF(p[0], p[1]); }));
        v.push_back(dist("ArcSin", false, "Arcsine distribution.", {{"Minimum", "Lower limit."}, {"Maximum", "Upper limit."}},
                         [](const P& p) { return makeArcsine(p[0], p[1]); }));
        v.push_back(dist("Cauchy", false, "Cauchy distribution (the cell shows the median).", {{"Location", "Location."}, {"Scale", "Scale."}},
                         [](const P& p) { return makeCauchy(p[0], p[1]); }));
        v.push_back(dist("Frechet", false, "Frechet distribution.", {{"Shape", "Shape alpha."}, {"Scale", "Scale."}, {"Location", "Location (minimum)."}},
                         [](const P& p) { return makeFrechet(p[0], p[1], p[2]); }));
        v.push_back(dist("Logistic", false, "Logistic distribution.", {{"Location", "Location."}, {"Scale", "Scale."}},
                         [](const P& p) { return makeLogistic(p[0], p[1]); }));
        // ---- discrete ----
        v.push_back(dist("DiscreteUniform", true, "Discrete uniform distribution (integers).", {{"Minimum", "Lower limit."}, {"Maximum", "Upper limit."}},
                         [](const P& p) { return makeDiscreteUniform(p[0], p[1]); }));
        v.push_back(dist("Bernoulli", true, "Bernoulli distribution: 1 with probability P, else 0.", {{"Probability", "Probability of 1."}},
                         [](const P& p) { return makeBernoulli(p[0]); }));
        v.push_back(dist("Binomial", true, "Binomial distribution.", {{"Trials", "Number of trials."}, {"Probability", "Probability of success."}},
                         [](const P& p) { return makeBinomial(p[0], p[1]); }));
        v.push_back(dist("Poisson", true, "Poisson distribution.", {{"Lambda", "Mean number of events."}},
                         [](const P& p) { return makePoisson(p[0]); }));
        v.push_back(dist("Pascal", true, "Pascal (negative binomial) distribution: failures before the R-th success.",
                         {{"Successes", "Number of successes R."}, {"Probability", "Probability of success."}},
                         [](const P& p) { return makeNegBinomial(p[0], p[1]); }));
        v.push_back(dist("Geometric", true, "Geometric distribution: failures before the first success.", {{"Probability", "Probability of success."}},
                         [](const P& p) { return makeGeometric(p[0]); }));
        v.push_back(dist("HyperGeometric", true, "Hypergeometric distribution.",
                         {{"Population", "Population size."}, {"Successes", "Successes in the population."}, {"Draws", "Number of draws."}},
                         [](const P& p) { return makeHypergeometric(p[0], p[1], p[2]); }));
        // ---- time series ----
        v.push_back(ts("AR1", TsType::AR1, "AR(1) autoregressive process.",
                       {{"Mu", "Long-run mean."}, {"Phi", "Autoregressive coefficient (|Phi| < 1)."}, {"Sigma", "Noise standard deviation."}, {"LastValue", "Last observed value."}}));
        v.push_back(ts("MA1", TsType::MA1, "MA(1) moving-average process.",
                       {{"Mu", "Mean."}, {"Theta", "Moving-average coefficient."}, {"Sigma", "Noise standard deviation."}}));
        v.push_back(ts("ARMA11", TsType::ARMA11, "ARMA(1,1) process.",
                       {{"Mu", "Long-run mean."}, {"Phi", "Autoregressive coefficient."}, {"Theta", "Moving-average coefficient."}, {"Sigma", "Noise standard deviation."}, {"LastValue", "Last observed value."}}));
        v.push_back(ts("ARCH1", TsType::ARCH1, "ARCH(1) process (non-constant volatility).",
                       {{"Mu", "Mean."}, {"A0", "Constant variance term (> 0)."}, {"A1", "ARCH coefficient (0 <= A1 < 1)."}, {"LastValue", "Last observed value."}}));
        v.push_back(ts("WienerProcess", TsType::GBM, "Wiener process (geometric Brownian motion) with drift.",
                       {{"StartValue", "Current value (> 0)."}, {"Drift", "Drift per period."}, {"Volatility", "Volatility per period."}, {"Dt", "Length of one period."}}));
        // ---- special ----
        v.push_back(other("FLOsimula_UserDefined", FloFunction::UserDefined,
                          "User-defined continuous distribution from a data area: raw data (one row/column), a histogram (Min, Max, Number[, ...]) or a quantile table (quantile, value; last row minimum, maximum)."));
        v.push_back(other("FLOsimula_DiscreteUserDefined", FloFunction::DiscreteUserDefined,
                          "User-defined discrete distribution from values and probabilities (or weights)."));
        v.push_back(other("FLOsimula_Bounded", FloFunction::Bounded,
                          "Bounds an MC FLO variable: returns Reference limited to [LowerLimit, UpperLimit]."));
        v.push_back(other("FLOsimula_output", FloFunction::Output,
                          "Output variable. Use as =<cell>+FLOsimula_output(\"Name\") (returns 0) or FLOsimula_output(\"Name\"; Value) (returns Value)."));
        v.push_back(other("FLOsimula_Correlation", FloFunction::Correlation,
                          "Rank correlation between two input variables. Type: 0 normal, 1 Student-t, 2 Clayton, 3 r-Clayton, 4 Gumbel, 5 r-Gumbel, 6 asy. Gumbel, 7 asy. Frechet, 8 asy. Triangular."));
        return v;
    }();
    return f;
}

const FloFunction* findFlo(const std::string& name0) {
    std::string n = upper(name0);
    size_t p = n.rfind('.');
    if (p != std::string::npos) n = n.substr(p + 1);
    if (n.rfind("FLOSIMULA_", 0) != 0) n = "FLOSIMULA_" + n;
    for (const auto& f : floFunctions())
        if (upper(f.name) == n) return &f;
    return nullptr;
}

std::string floVariableName(const FloFunction& f, const std::vector<Value>& a) {
    switch (f.kind) {
    case FloFunction::Dist:
    case FloFunction::TimeSeries:
        return !a.empty() && isNameArg(a[0]) ? a[0].str : std::string();
    case FloFunction::UserDefined:
    case FloFunction::DiscreteUserDefined:
        return a.size() > 2 && a[2].kind == Value::String ? a[2].str : std::string();
    case FloFunction::Output:
        return !a.empty() && a[0].kind == Value::String ? a[0].str : std::string();
    default:
        return {};
    }
}

double evalFlo(const FloFunction& f, const std::vector<Value>& a) {
    auto& sim = Simulation::instance();
    switch (f.kind) {
    case FloFunction::Dist: {
        size_t i = 0;
        std::string name;
        if (!a.empty() && isNameArg(a[0])) { name = a[0].str; i = 1; }
        size_t np = f.params.size();
        if (a.size() < i + np) throw std::invalid_argument(f.name + " needs " + std::to_string(np) + " parameters");
        P p;
        for (size_t k = 0; k < np; ++k) {
            if (a[i + k].isEmpty()) throw std::invalid_argument(std::string("parameter ") + f.params[k].name + " is missing");
            p.push_back(a[i + k].asNumber());
        }
        Options o = readOptions(a, i + np);
        std::shared_ptr<Distribution> d;
        if (f.label == "TruncatedNormal") {
            d.reset(makeNormal(p[0], p[1]).release());
            o.bounds.lo = std::max(o.bounds.lo, p[2]);
            o.bounds.hi = std::min(o.bounds.hi, p[3]);
            if (!(o.bounds.lo < o.bounds.hi)) throw std::invalid_argument("LowerLimit must be below UpperLimit");
            return inputWith(name, describe(f.label, p), d, o, false);
        }
        d.reset(f.make(p).release());
        return inputWith(name, describe(f.label, p), d, o, false);
    }
    case FloFunction::TimeSeries: {
        size_t i = 0;
        std::string name;
        if (!a.empty() && isNameArg(a[0])) { name = a[0].str; i = 1; }
        size_t np = f.params.size();
        P p;
        for (size_t k = 0; k < np; ++k) {
            if (i + k >= a.size() || a[i + k].isEmpty()) {
                if (f.ts == TsType::GBM && k == 3) { p.push_back(1.0); continue; }  // Dt defaults to 1
                throw std::invalid_argument(std::string("parameter ") + f.params[k].name + " is missing");
            }
            p.push_back(a[i + k].asNumber());
        }
        int n = int(std::lround(optNum(a, i + np, 1)));
        if (n < 1 || n > 1000000) throw std::invalid_argument("Number (periods) must be between 1 and 1,000,000");
        TimeSeries t{f.ts, p};
        t.validate();
        return sim.input(name, describe(f.label, p) + (n > 1 ? " periods=" + std::to_string(n) : ""), t.expected(n),
                         [t, n](double u, std::mt19937_64& rng) { return t.simulate(u, n, rng); });
    }
    case FloFunction::UserDefined: {
        if (a.empty()) throw std::invalid_argument("the data area is missing");
        int bins = int(std::lround(optNum(a, 1, 0)));
        std::string name = a.size() > 2 && a[2].kind == Value::String ? a[2].str : std::string();
        Options o = readOptions(a, 3);
        std::shared_ptr<Distribution> d(userDefinedDistribution(a[0], bins).release());
        return inputWith(name, "UserDefined", d, o, true);
    }
    case FloFunction::DiscreteUserDefined: {
        if (a.size() < 2) throw std::invalid_argument("values and probabilities are needed");
        std::string name = a.size() > 2 && a[2].kind == Value::String ? a[2].str : std::string();
        Options o = readOptions(a, 3);
        std::shared_ptr<Distribution> d(makeDiscreteCustom(a[0].numbers(), a[1].numbers()).release());
        return inputWith(name, "DiscreteUserDefined", d, o, true);
    }
    case FloFunction::Bounded: {
        if (a.size() < 3) throw std::invalid_argument("FLOsimula_Bounded(Reference; LowerLimit; UpperLimit)");
        double v = a[0].asNumber(), lo = a[1].asNumber(), hi = a[2].asNumber();
        if (lo > hi) throw std::invalid_argument("LowerLimit must not exceed UpperLimit");
        return std::min(hi, std::max(lo, v));
    }
    case FloFunction::Output: {
        if (a.empty() || a[0].kind != Value::String) throw std::invalid_argument("FLOsimula_output needs the variable name as text");
        bool has = a.size() > 1 && !a[1].isEmpty();
        return sim.output(a[0].str, has ? a[1].asNumber() : 0.0, has);
    }
    case FloFunction::Correlation: {
        if (a.size() < 3) throw std::invalid_argument("FLOsimula_Correlation(Name1; Name2; Coefficient; [Type])");
        if (a[0].kind != Value::String || a[1].kind != Value::String)
            throw std::invalid_argument("the correlated variables must be given by name");
        return sim.correlate(a[0].str, a[1].str, a[2].asNumber(), int(std::lround(optNum(a, 3, 0))));
    }
    }
    return 0;
}

} // namespace mc
