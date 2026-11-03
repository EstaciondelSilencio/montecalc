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
                 std::function<DistPtr(const P&)> make, std::vector<std::string> aliases = {}) {
    FloFunction f;
    f.name = std::string("FLOsimula_") + label;
    for (auto& a : aliases) f.aliases.push_back("FLOsimula_" + a);
    f.kind = FloFunction::Dist;
    f.discrete = discrete;
    f.label = label;
    f.description = desc;
    f.params = std::move(params);
    f.make = std::move(make);
    return f;
}

FloFunction ts(const char* name, const char* label, TsType t, const char* desc, std::vector<FloParam> params, size_t minParams = 0) {
    FloFunction f;
    f.name = std::string("FLOsimula_") + name;
    f.kind = FloFunction::TimeSeries;
    f.label = label;
    f.minParams = minParams;
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

// options after the variable name: [Number]; [Min]; [Max]; [Round]
Options readOptions(const std::vector<Value>& a, size_t i) {
    Options o;
    double n = optNum(a, i, 1);
    if (n == 0) n = 1;   // MC FLO default "Number = 0": one draw
    if (n < 1 || n > 1e7) throw std::invalid_argument("Number must be between 1 and 10,000,000");
    o.number = int(std::lround(n));
    o.bounds.lo = optNum(a, i + 1, o.bounds.lo);
    o.bounds.hi = optNum(a, i + 2, o.bounds.hi);
    if (o.bounds.lo > o.bounds.hi) throw std::invalid_argument("Min must not exceed Max");
    if (i + 3 < a.size() && !a[i + 3].isEmpty()) {
        if (a[i + 3].kind == Value::Bool) o.round = a[i + 3].num != 0 ? 0 : -1;
        else o.round = int(std::lround(a[i + 3].asNumber()));
        if (o.round > 12) o.round = 12;
    }
    for (size_t k = i + 4; k < a.size(); ++k)
        if (!a[k].isEmpty()) throw std::invalid_argument("too many arguments");
    return o;
}

// variable name at position i (text; empty / missing = unnamed)
std::string nameAt(const std::vector<Value>& a, size_t i) {
    if (i >= a.size() || a[i].isEmpty()) return {};
    if (a[i].kind == Value::String) return a[i].str;
    throw std::invalid_argument("argument " + std::to_string(i + 1) + " must be the variable name (text)");
}

// MC FLO time series. Deviation form around Start (Start = level of the process):
//   AR    x = Start + Weight*(x_prev - Start) + Sd*z
//   MA    x = Start + Sd*z + Weight*e_prev
//   ARMA  x = Start + Weight1*(x_prev - Start) + Sd*z + Weight2*e_prev
//   ARCH  s2 = Omega + Alpha*x_prev^2, x = sqrt(s2)*z
//   WP    x = x_prev * exp(Mu - s^2/2 + s*z), s = Drift*StDeviation (geometric Brownian motion)
struct FloSeries {
    TsType type;
    P p;   // AR: Start, Weight, Sd; MA: Start, Weight, Sd; ARMA: Start, W1, Sd, W2; ARCH: Omega, Alpha; WP: Start, Drift, Mu, Sd

    double start() const { return type == TsType::ARCH1 ? 0.0 : p[0]; }
    // one step; state = {x_prev, e_prev}
    double step(std::vector<double>& st, double z) const {
        double x = st[0], e = st[1], nx = 0, ne = 0;
        switch (type) {
        case TsType::AR1: ne = p[2] * z; nx = p[0] + p[1] * (x - p[0]) + ne; break;
        case TsType::MA1: ne = p[2] * z; nx = p[0] + ne + p[1] * e; break;
        case TsType::ARMA11: ne = p[2] * z; nx = p[0] + p[1] * (x - p[0]) + ne + p[3] * e; break;
        case TsType::ARCH1: ne = std::sqrt(std::max(0.0, p[0] + p[1] * x * x)) * z; nx = ne; break;
        case TsType::GBM: {
            double sg = p[1] * p[3];
            ne = sg * z;
            nx = x * std::exp(p[2] - 0.5 * sg * sg + ne);
            break;
        }
        }
        st[0] = nx;
        st[1] = ne;
        return nx;
    }
    double expected(int periods) const {
        switch (type) {
        case TsType::ARCH1: return 0;
        case TsType::GBM: return p[0] * std::exp(p[2] * std::max(1, periods));
        default: return p[0];
        }
    }
    void validate() const {
        switch (type) {
        case TsType::AR1:
        case TsType::MA1: if (p[2] < 0) throw std::invalid_argument("StDeviation must not be negative"); break;
        case TsType::ARMA11: if (p[2] < 0) throw std::invalid_argument("StDeviation1 must not be negative"); break;
        case TsType::ARCH1:
            if (p[0] <= 0) throw std::invalid_argument("Omega must be positive");
            if (p[1] < 0 || p[1] >= 1) throw std::invalid_argument("Alpha must be in [0, 1)");
            break;
        case TsType::GBM:
            if (p[0] <= 0) throw std::invalid_argument("Start must be positive");
            if (p[1] < 0 || p[3] < 0) throw std::invalid_argument("Drift and StDeviation must not be negative");
            break;
        }
    }
};

double normalQ(double u) {
    u = std::min(1 - 1e-12, std::max(1e-12, u));
    return makeNormal(0, 1)->quantile(u);
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
        // ---- continuous (wiki "Stetige Verteilungen") ----
        v.push_back(dist("Uniform", false, "Continuous uniform distribution.", {{"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}},
                         [](const P& p) { return makeUniform(p[0], p[1]); }, {"Uniforme"}));
        v.push_back(dist("Triangular", false, "Triangular distribution.",
                         {{"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}, {"Mode", "Most likely value."}},
                         [](const P& p) { return makeTriangular(p[0], p[2], p[1]); }));
        v.push_back(dist("PERT", false, "PERT distribution.", {{"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}, {"Mode", "Most likely value."}},
                         [](const P& p) { return makePert(p[0], p[2], p[1]); }));
        v.push_back(dist("Normal", false, "Normal distribution.", {{"Mean", "Mean."}, {"StDeviation", "Standard deviation."}},
                         [](const P& p) { return makeNormal(p[0], p[1]); }));
        v.push_back(dist("LogNormal", false, "Log-normal distribution: Shift + exp(Normal(Mean; StDeviation)).",
                         {{"Shift", "Shift of all values (0 = positive values only)."}, {"Mean", "Mean of the logarithm."}, {"StDeviation", "Standard deviation of the logarithm."}},
                         [](const P& p) {
                             if (!(p[2] > 0)) throw std::invalid_argument("StDeviation must be positive");
                             double m = std::exp(p[1] + p[2] * p[2] / 2), sd = m * std::sqrt(std::expm1(p[2] * p[2]));
                             return makeShifted(makeLognormal(m, sd), p[0]);
                         }));
        v.push_back(dist("Beta", false, "Beta distribution between Lower_Limit and Upper_Limit.",
                         {{"param_alfa", "Shape alpha."}, {"param_beta", "Shape beta."}, {"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}},
                         [](const P& p) { return makeBeta(p[0], p[1], p[2], p[3]); }));
        v.push_back(dist("Gumbel", false, "Gumbel (extreme value) distribution.", {{"Shift", "Location."}, {"param_alfa", "Scale (> 0)."}},
                         [](const P& p) { return makeGumbel(p[0], p[1]); }));
        v.push_back(dist("Gamma", false, "Gamma distribution: Shift + Gamma(scale param_alfa, shape param_beta).",
                         {{"Shift", "Left limit."}, {"param_alfa", "Scale."}, {"param_beta", "Shape."}},
                         [](const P& p) { return makeShifted(makeGamma(p[2], p[1]), p[0]); }));
        v.push_back(dist("Exponential", false, "Exponential distribution: Shift + Exponential(mean param_alfa).",
                         {{"Shift", "Left limit."}, {"param_alfa", "Mean (average survival time)."}},
                         [](const P& p) {
                             if (!(p[1] > 0)) throw std::invalid_argument("param_alfa must be positive");
                             return makeShifted(makeExponential(1 / p[1]), p[0]);
                         }));
        v.push_back(dist("TruncNormal", false, "Truncated normal distribution.",
                         {{"Mean", "Mean of the untruncated normal."}, {"StDeviation", "Standard deviation."}, {"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}},
                         nullptr, {"TruncatedNormal"}));
        v.push_back(dist("Weibull", false, "Weibull distribution: Shift + Weibull(scale param_alfa, shape param_beta).",
                         {{"Shift", "Left limit."}, {"param_alfa", "Scale."}, {"param_beta", "Shape."}},
                         [](const P& p) { return makeShifted(makeWeibull(p[2], p[1]), p[0]); }));
        v.push_back(dist("chi2", false, "Chi-square distribution.", {{"Freedom", "Degrees of freedom."}},
                         [](const P& p) { return makeChiSquare(p[0]); }));
        v.push_back(dist("studentt", false, "Student-t distribution.", {{"Freedom", "Degrees of freedom."}},
                         [](const P& p) { return makeStudentT(p[0]); }));
        v.push_back(dist("Pareto", false, "Pareto distribution with minimum Shift (> 0).", {{"Shift", "Minimum value (> 0)."}, {"param_alfa", "Shape alpha."}},
                         [](const P& p) { return makePareto(p[1], p[0]); }));
        v.push_back(dist("Fisher", false, "F distribution.", {{"Freedom1", "Numerator degrees of freedom."}, {"Freedom2", "Denominator degrees of freedom."}},
                         [](const P& p) { return makeF(p[0], p[1]); }));
        v.push_back(dist("ArcSin", false, "Arcsine distribution.", {{"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}},
                         [](const P& p) { return makeArcsine(p[0], p[1]); }));
        v.push_back(dist("Cauchy", false, "Cauchy distribution (the cell shows the median).", {{"Shift", "Location (peak)."}, {"param_alfa", "Scale (width)."}},
                         [](const P& p) { return makeCauchy(p[0], p[1]); }));
        v.push_back(dist("Frechet", false, "Frechet distribution.", {{"Start", "Minimum value."}, {"Shift", "Scale."}, {"param_alfa", "Shape."}},
                         [](const P& p) { return makeFrechet(p[2], p[1], p[0]); }));
        v.push_back(dist("Logistic", false, "Logistic distribution.", {{"Location", "Location."}, {"Shift", "Scale."}},
                         [](const P& p) { return makeLogistic(p[0], p[1]); }));
        // ---- discrete (wiki "Diskrete Verteilungen") ----
        v.push_back(dist("DiscreteUniform", true, "Discrete uniform distribution (integers).", {{"Lower_Limit", "Lower limit."}, {"Upper_Limit", "Upper limit."}},
                         [](const P& p) { return makeDiscreteUniform(p[0], p[1]); }, {"DiscreteUniforme"}));
        v.push_back(dist("Bernoulli", true, "Bernoulli distribution: 1 (TRUE) with the given probability, else 0.", {{"Probability", "Probability of TRUE."}},
                         [](const P& p) { return makeBernoulli(p[0]); }));
        v.push_back(dist("Binomial", true, "Binomial distribution: number of successes in Tries trials.", {{"Probability", "Probability of success."}, {"Tries", "Number of trials."}},
                         [](const P& p) { return makeBinomial(p[1], p[0]); }));
        v.push_back(dist("Poisson", true, "Poisson distribution.", {{"Occurency", "Mean number of events."}},
                         [](const P& p) { return makePoisson(p[0]); }));
        v.push_back(dist("Pascal", true, "Pascal (negative binomial) distribution: failed attempts until Success successes.",
                         {{"Probability", "Probability of a failure (FALSE)."}, {"Success", "Number of successes."}},
                         [](const P& p) { return makeNegBinomial(p[1], 1 - p[0]); }));
        v.push_back(dist("Geometric", true, "Geometric distribution: failures before the first success.", {{"Probability", "Probability of success (TRUE)."}},
                         [](const P& p) { return makeGeometric(p[0]); }));
        v.push_back(dist("Hypergeometric", true, "Hypergeometric distribution (draws without replacement).",
                         {{"Tries", "Sample size."}, {"Population", "Population size."}, {"Success_Possibles", "Elements with the property in the population."}},
                         [](const P& p) { return makeHypergeometric(p[1], p[2], p[0]); }));
        // ---- time series (wiki "Zeitreihen") ----
        v.push_back(ts("TemporalS_ARmodel", "AR(1)", TsType::AR1, "AR(1) process around the level Start.",
                       {{"Start", "Start value and level."}, {"Weight", "Weight of the previous period."}, {"StDeviation", "Standard deviation of the noise."},
                        {"UseBefore", "1 = every iteration starts at Start (e.g. the previous period's cell); 0 = the path continues."}}));
        v.push_back(ts("TemporalS_MAmodel", "MA(1)", TsType::MA1, "MA(1) process.",
                       {{"Start", "Mean."}, {"Weight", "Weight of the previous noise."}, {"StDeviation", "Standard deviation of the noise."}}));
        v.push_back(ts("TemporalS_ARMAmodel", "ARMA(1,1)", TsType::ARMA11, "ARMA(1,1) process.",
                       {{"Start", "Start value and level."}, {"Weight1", "Weight of the previous period."}, {"StDeviation1", "Standard deviation of the noise."},
                        {"Weight2", "Weight of the previous noise."}}));
        v.push_back(ts("TemporalS_ARCHmodel", "ARCH(1)", TsType::ARCH1, "ARCH(1) process (non-constant volatility, mean 0).",
                       {{"Omega", "Constant variance term (> 0)."}, {"Alpha", "ARCH coefficient (0 <= Alpha < 1)."}}));
        v.push_back(ts("TemporalS_WPmodel", "Wiener", TsType::GBM, "Wiener process (geometric Brownian motion).",
                       {{"Start", "Start value (> 0)."}, {"Drift", "Volatility (0 = not random)."}, {"Mu", "Trend (> 0 rising, < 0 falling)."},
                        {"StDeviation", "Optional scale of the volatility (default 1)."}, {"UseBefore", "1 = every iteration starts at Start."}}, 4));
        // ---- special ----
        v.push_back(other("FLOsimula_UserDefined", FloFunction::UserDefined,
                          "User-defined continuous distribution from a data area: raw data (one row/column, divided into NumberOfBins intervals), a histogram (Min, Max, Number[, ...]) or quantile/probability (QP) pairs (quantile, value; last row minimum, maximum)."));
        v.push_back(other("FLOsimula_DiscreteUserDefined", FloFunction::DiscreteUserDefined,
                          "User-defined discrete distribution from values and probabilities (or weights)."));
        v.push_back(other("FLOsimula_Bounded", FloFunction::Bounded,
                          "Bounded variable: truncates an existing MC FLO variable to [LowerLimit, UpperLimit] (only values within the limits are drawn)."));
        v.push_back(other("FLOsimula_output", FloFunction::Output,
                          "Output variable. Use as =<cell>+FLOsimula_output(\"Name\") (returns 0) or FLOsimula_output(\"Name\"; Value) (returns Value)."));
        v.push_back(other("FLOsimula_correlation", FloFunction::Correlation,
                          "Correlation between two input variables. CorType: 0 Normal, 1 Clayton, 2 rClayton, 3 Gumbel, 4 rGumbel, 5 Student-t, 6 asy. Gumbel, 7 asy. Triangular, 8 asy. Frechet."));
        return v;
    }();
    return f;
}

const FloFunction* findFlo(const std::string& name0) {
    std::string n = upper(name0);
    size_t p = n.rfind('.');
    if (p != std::string::npos) n = n.substr(p + 1);
    if (n.rfind("FLOSIMULA_", 0) != 0) n = "FLOSIMULA_" + n;
    for (const auto& f : floFunctions()) {
        if (upper(f.name) == n) return &f;
        for (const auto& a : f.aliases)
            if (upper(a) == n) return &f;
    }
    return nullptr;
}

// number of process parameters actually given (the Wiener process has an optional StDeviation)
size_t paramCount(const FloFunction& f, const std::vector<Value>& a) {
    size_t np = f.params.size();
    if (f.minParams && f.minParams < np) {
        // the parameters end where the name (text) or the end of the list starts
        size_t k = f.minParams;
        if (k < a.size() && a[k].kind != Value::String && !a[k].isEmpty()) return np;
        return f.minParams;
    }
    return np;
}

std::string floVariableName(const FloFunction& f, const std::vector<Value>& a) {
    auto textAt = [&](size_t i) { return i < a.size() && a[i].kind == Value::String ? a[i].str : std::string(); };
    switch (f.kind) {
    case FloFunction::Dist:
    case FloFunction::TimeSeries: return textAt(paramCount(f, a));
    case FloFunction::UserDefined:
    case FloFunction::DiscreteUserDefined: return textAt(2);
    case FloFunction::Bounded: return textAt(3);
    case FloFunction::Output: return textAt(0);
    default: return {};
    }
}

namespace {

P readParams(const FloFunction& f, const std::vector<Value>& a, size_t np) {
    if (a.size() < np) throw std::invalid_argument(f.name + " needs " + std::to_string(np) + " parameters");
    P p;
    for (size_t k = 0; k < np; ++k) {
        if (a[k].isEmpty()) throw std::invalid_argument(std::string("parameter ") + f.params[k].name + " is missing");
        if (a[k].kind == Value::String && !a[k].isNumberText())
            throw std::invalid_argument(std::string("parameter ") + f.params[k].name + " must be a number (the variable name comes after the parameters)");
        p.push_back(a[k].asNumber());
    }
    return p;
}

// monotone inverse of a sampler on [lo, hi]: the u-interval whose values lie within the limits
bool uInterval(const Sampler& s, double lo, double hi, double& ulo, double& uhi) {
    auto at = [&](double u) {
        std::mt19937_64 dummy(1);   // same extra draws for every u
        return s(u, dummy);
    };
    const double e = 1e-9;
    if (at(e) > at(1 - e)) return false;   // not monotone
    auto first = [&](double x, bool strictlyAbove) {
        double l = e, h = 1 - e;
        for (int i = 0; i < 50; ++i) {
            double m = (l + h) / 2;
            bool cond = strictlyAbove ? at(m) > x : at(m) >= x;
            (cond ? h : l) = m;
        }
        return h;
    };
    ulo = std::isfinite(lo) ? first(lo, false) : e;
    uhi = std::isfinite(hi) ? first(hi, true) : 1 - e;
    return uhi > ulo;
}

} // namespace

double evalFlo(const FloFunction& f, const std::vector<Value>& a) {
    auto& sim = Simulation::instance();
    switch (f.kind) {
    case FloFunction::Dist: {
        size_t np = f.params.size();
        P p = readParams(f, a, np);
        std::string name = nameAt(a, np);
        Options o = readOptions(a, np + 1);
        std::shared_ptr<Distribution> d;
        if (!f.make) {   // TruncNormal
            d.reset(makeNormal(p[0], p[1]).release());
            o.bounds.lo = std::max(o.bounds.lo, p[2]);
            o.bounds.hi = std::min(o.bounds.hi, p[3]);
            if (!(o.bounds.lo < o.bounds.hi)) throw std::invalid_argument("Lower_Limit must be below Upper_Limit");
            return inputWith(name, describe(f.label, p), d, o, false);
        }
        d.reset(f.make(p).release());
        return inputWith(name, describe(f.label, p), d, o, false);
    }
    case FloFunction::TimeSeries: {
        size_t np = paramCount(f, a);
        P p = readParams(f, a, np);
        std::string name = nameAt(a, np);
        double nn = optNum(a, np + 1, 0);
        if (nn < 0 || nn > 1000000) throw std::invalid_argument("Number (periods) must be between 0 and 1,000,000");
        int n = int(std::lround(nn));
        Options o = readOptions(a, np + 1);   // Min / Max / Round after Number
        bool useBefore = false;
        FloSeries sr{f.ts, {}};
        switch (f.ts) {
        case TsType::AR1: sr.p = {p[0], p[1], p[2]}; useBefore = p[3] != 0; break;
        case TsType::MA1: sr.p = {p[0], p[1], p[2]}; break;
        case TsType::ARMA11: sr.p = {p[0], p[1], p[2], p[3]}; break;
        case TsType::ARCH1: sr.p = {p[0], p[1]}; break;
        case TsType::GBM:
            if (np == 5) { sr.p = {p[0], p[1], p[2], p[3]}; useBefore = p[4] != 0; }
            else { sr.p = {p[0], p[1], p[2], 1.0}; useBefore = p[3] != 0; }
            break;
        }
        sr.validate();
        std::string key = name;
        Bounds b = o.bounds;
        int r = o.round;
        auto finish = [b, r](double v) {
            v = std::min(b.hi, std::max(b.lo, v));
            return r >= 0 ? roundTo(v, r) : v;
        };
        double expected = finish(sr.expected(n));
        std::string desc = describe(f.label, p) + (n > 0 ? " periods=" + std::to_string(n) : "");
        return sim.input(name, desc, expected, [sr, n, useBefore, key, finish](double u, std::mt19937_64& rng) {
            std::normal_distribution<double> nd(0.0, 1.0);
            double z = normalQ(u);   // the (correlated) uniform drives the noise of this iteration
            if (n >= 1 || useBefore || key.empty()) {
                std::vector<double> st{sr.start(), 0.0};
                double x = sr.step(st, z);
                for (int k = 1; k < n; ++k) x = sr.step(st, nd(rng));
                return finish(x);
            }
            auto& st = Simulation::instance().seriesState(key);   // path continues from the last iteration
            if (st.size() != 2) st = {sr.start(), 0.0};
            return finish(sr.step(st, z));
        });
    }
    case FloFunction::UserDefined: {
        if (a.empty()) throw std::invalid_argument("the data area is missing");
        int bins = int(std::lround(optNum(a, 1, 0)));
        std::string name = nameAt(a, 2);
        Options o = readOptions(a, 3);
        std::shared_ptr<Distribution> d(userDefinedDistribution(a[0], bins).release());
        return inputWith(name, "UserDefined", d, o, true);
    }
    case FloFunction::DiscreteUserDefined: {
        if (a.size() < 2) throw std::invalid_argument("values and probabilities are needed");
        std::string name = nameAt(a, 2);
        Options o = readOptions(a, 3);
        std::shared_ptr<Distribution> d(makeDiscreteCustom(a[0].numbers(), a[1].numbers()).release());
        return inputWith(name, "DiscreteUserDefined", d, o, true);
    }
    case FloFunction::Bounded: {
        if (a.size() < 3) throw std::invalid_argument("FLOsimula_Bounded(Variable; LowerLimit; UpperLimit; [Name])");
        double v = a[0].asNumber(), lo = a[1].asNumber(), hi = a[2].asNumber();
        if (lo > hi) throw std::invalid_argument("LowerLimit must not exceed UpperLimit");
        std::string name = nameAt(a, 3);
        // the referenced input was evaluated just before (it is a precedent of this cell)
        Sampler ref;
        bool have = sim.samplerOfValue(v, ref);
        double ulo = 0, uhi = 1;
        bool mono = have && uInterval(ref, lo, hi, ulo, uhi);
        Sampler bounded = [ref, have, mono, ulo, uhi, lo, hi, v](double u, std::mt19937_64& rng) {
            if (mono) {
                double x = ref(ulo + u * (uhi - ulo), rng);
                if (x >= lo && x <= hi) return x;   // exact for plain distributions
            }
            if (have) {   // paths / compound sums are not monotone in u: rejection, then the barrier
                std::uniform_real_distribution<double> uni(0.0, 1.0);
                for (int k = 0; k < 10000; ++k) {
                    double x = ref(k == 0 ? u : uni(rng), rng);
                    if (x >= lo && x <= hi) return x;
                }
            }
            return std::min(hi, std::max(lo, v));
        };
        // before a simulation the referenced variable's expected value is shown (as in MC FLO)
        return sim.input(name, "Bounded(" + std::to_string(lo) + "; " + std::to_string(hi) + ")", v, bounded);
    }
    case FloFunction::Output: {
        if (a.empty() || a[0].kind != Value::String) throw std::invalid_argument("FLOsimula_output needs the variable name as text");
        bool has = a.size() > 1 && !a[1].isEmpty();
        return sim.output(a[0].str, has ? a[1].asNumber() : 0.0, has);
    }
    case FloFunction::Correlation: {
        if (a.size() < 3) throw std::invalid_argument("FLOsimula_correlation(Name1; Name2; Coef_corr; [CorType])");
        if (a[0].kind != Value::String || a[1].kind != Value::String)
            throw std::invalid_argument("the correlated variables must be given by name");
        return sim.correlate(a[0].str, a[1].str, a[2].asNumber(), int(std::lround(optNum(a, 3, 0))));
    }
    }
    return 0;
}

} // namespace mc
