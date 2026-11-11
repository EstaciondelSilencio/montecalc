// MonteCalc - Calc add-in exposing the MC FLO model and tool functions
#include "addin.hpp"
#include "util.hpp"
#ifndef MONTECALC_XLL
#include "model.hpp"
#endif

#include "../core/bayes.h"
#include "../core/copula.h"
#include "../core/fitting.h"
#include "../core/flofuncs.h"
#include "../core/formula.h"
#include "../core/simulation.h"
#include "../core/specfun.h"
#include "../core/stats.h"

#ifndef MONTECALC_XLL
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XSheetCellRange.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/text/XText.hpp>
#include <cppuhelper/supportsservice.hxx>
#endif

#include <algorithm>
#include <bitset>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
#ifndef MONTECALC_XLL
using com::sun::star::uno::UNO_QUERY;
#endif

namespace montecalc {

namespace {

using Vec = std::vector<double>;
using Rows = std::vector<std::vector<double>>;
const double kNaN = std::nan("");
const unsigned long long kDataSeed = 20260101ULL;  // data tools are deterministic between recalculations

struct ArgInfo { const char* name; const char* desc; };
struct FuncInfo { const char* prog; const char* name; const char* desc; std::vector<ArgInfo> args; };

const std::vector<FuncInfo>& functionTable() {
    static const std::vector<FuncInfo> table = {
#include "functions_meta.inc"
    };
    return table;
}

const FuncInfo* findFunc(const OUString& prog) {
    std::string p = u8(prog);
    for (const auto& f : functionTable())
        if (p == f.prog) return &f;
    return nullptr;
}

[[noreturn]] void raise(const std::string& msg) {
    throw lang::IllegalArgumentException(ou(msg), Reference<uno::XInterface>(), 0);
}

template <class Fn>
auto guard(Fn fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const lang::IllegalArgumentException&) {
        throw;
    } catch (const std::exception& e) {
        raise(e.what());
    }
}

// ---------------------------------------------------------------- conversions
mc::Value toValue(const Any& a) {
    if (!a.hasValue()) return {};
    double d;
    if (a >>= d) return mc::Value::number(d);
    bool b;
    if (a.getValueTypeClass() == uno::TypeClass_BOOLEAN && (a >>= b)) return mc::Value::boolean(b);
    sal_Int32 i;
    if (a >>= i) return mc::Value::number(i);
    OUString s;
    if (a >>= s) return mc::Value::text(u8(s));
    AnyMatrix m;
    if (a >>= m) {
        std::vector<std::vector<mc::Value>> rows;
        for (const auto& r : m) {
            std::vector<mc::Value> line;
            for (const auto& c : r) line.push_back(toValue(c));
            rows.push_back(std::move(line));
        }
        if (rows.size() == 1 && rows[0].size() == 1) return rows[0][0];
        return mc::Value::array(std::move(rows));
    }
    DoubleMatrix dm;
    if (a >>= dm) {
        std::vector<std::vector<mc::Value>> rows;
        for (const auto& r : dm) {
            std::vector<mc::Value> line;
            for (double x : r) line.push_back(mc::Value::number(x));
            rows.push_back(std::move(line));
        }
        return mc::Value::array(std::move(rows));
    }
    return {};
}

double optNum(const Any& a, double def) {
    mc::Value v = toValue(a);
    if (v.isEmpty()) return def;
    return v.asNumber();
}

bool optBool(const Any& a, bool def) {
    mc::Value v = toValue(a);
    if (v.isEmpty()) return def;
    if (v.kind == mc::Value::String) {
        std::string s = mc::lower(v.str);
        if (s == "true" || s == "wahr") return true;
        if (s == "false" || s == "falsch") return false;
    }
    return v.asNumber() != 0;
}

// all cells of a range (non-numeric -> NaN), row by row; keeps positions aligned
Vec cells(const AnyMatrix& m) {
    Vec r;
    for (const auto& row : m)
        for (const auto& c : row) {
            double d;
            r.push_back((c >>= d) ? d : kNaN);
        }
    return r;
}

Vec numbers(const AnyMatrix& m) { return mc::stats::finite(cells(m)); }

Rows matrixOf(const AnyMatrix& m) {
    Rows r;
    for (const auto& row : m) {
        Vec line;
        for (const auto& c : row) {
            double d;
            line.push_back((c >>= d) ? d : kNaN);
        }
        r.push_back(line);
    }
    return r;
}

std::vector<std::string> strings(const AnyMatrix& m) {
    std::vector<std::string> r;
    for (const auto& row : m)
        for (const auto& c : row) {
            OUString s;
            double d;
            if (c >>= s) { if (!s.isEmpty()) r.push_back(u8(s)); }
            else if (c >>= d) { char b[64]; std::snprintf(b, sizeof b, "%.15g", d); r.push_back(b); }
        }
    return r;
}

std::vector<std::string> stringsOfAny(const Any& a) {
    AnyMatrix m;
    if (a >>= m) return strings(m);
    OUString s;
    if (a >>= s) return {u8(s)};
    return {};
}

DoubleMatrix toMatrix(const Rows& rows) {
    DoubleMatrix m(sal_Int32(rows.size()));
    auto* p = m.getArray();
    for (size_t i = 0; i < rows.size(); ++i) {
        Sequence<double> r(sal_Int32(rows[i].size()));
        for (size_t j = 0; j < rows[i].size(); ++j) r.getArray()[j] = std::isfinite(rows[i][j]) ? rows[i][j] : 0.0;
        p[i] = r;
    }
    return m;
}

DoubleMatrix column(const Vec& v) {
    Rows r;
    for (double x : v) r.push_back({x});
    return toMatrix(r);
}

Any anyNum(double v) { return std::isfinite(v) ? Any(v) : Any(OUString()); }
Any anyText(const std::string& s) { return Any(ou(s)); }

AnyMatrix toAnyMatrix(const std::vector<std::vector<Any>>& rows) {
    size_t w = 0;
    for (auto& r : rows) w = std::max(w, r.size());
    AnyMatrix m(sal_Int32(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        Sequence<Any> r(static_cast<sal_Int32>(w));
        for (size_t j = 0; j < w; ++j) r.getArray()[j] = j < rows[i].size() ? rows[i][j] : Any(OUString());
        m.getArray()[i] = r;
    }
    return m;
}

double finiteOr(double v) {
    if (!std::isfinite(v)) throw std::invalid_argument("not enough valid data");
    return v;
}

// ---------------------------------------------------------------- result cache
void keyPart(std::string& k, double v) { char b[40]; std::snprintf(b, sizeof b, "|%.17g", v); k += b; }
void keyPart(std::string& k, const OUString& v) { k += "|s" + u8(v); }
void keyPart(std::string& k, const Any& a);
void keyPart(std::string& k, const AnyMatrix& m) {
    k += "|[";
    for (const auto& r : m) {
        for (const auto& c : r) keyPart(k, c);
        k += ";";
    }
    k += "]";
}
void keyPart(std::string& k, const Any& a) {
    double d;
    OUString s;
    bool b;
    AnyMatrix m;
    if (!a.hasValue()) k += "|_";
    else if (a.getValueTypeClass() == uno::TypeClass_BOOLEAN && (a >>= b)) k += b ? "|T" : "|F";
    else if (a >>= d) keyPart(k, d);
    else if (a >>= s) keyPart(k, s);
    else if (a >>= m) keyPart(k, m);
    else k += "|?";
}
template <class... Args>
std::string memoKey(const char* name, const Args&... args) {
    std::string k = name;
    (keyPart(k, args), ...);
    return k;
}
template <class T>
T memo(const std::string& key, const std::function<T()>& fn) {
    struct Entry { bool ok; T value; OUString error; };
    static std::map<std::string, Entry> cache;
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 2000) cache.clear();
        Entry e{true, T(), OUString()};
        try {
            e.value = fn();
        } catch (const lang::IllegalArgumentException& ex) {
            e.ok = false;
            e.error = ex.Message;
        }
        it = cache.emplace(key, e).first;
    }
    if (!it->second.ok) throw lang::IllegalArgumentException(it->second.error, Reference<uno::XInterface>(), 0);
    return it->second.value;
}

// ---------------------------------------------------------------- model functions
double flo(const char* name, std::initializer_list<Any> args) {
    return guard([&] {
        const mc::FloFunction* f = mc::findFlo(name);
        if (!f) throw std::invalid_argument(std::string("unknown function ") + name);
        std::vector<mc::Value> v;
        for (const auto& a : args) v.push_back(toValue(a));
        while (!v.empty() && v.back().kind == mc::Value::Empty) v.pop_back();
        return mc::evalFlo(*f, v);
    });
}

// ---------------------------------------------------------------- simulation results
bool noResults() {
    auto& sim = mc::Simulation::instance();
    return sim.busy() || !sim.hasResults();
}

// Optional "Referenz" argument of the MC FLO result functions: the variant (result set of a
// multiple simulation) to read. Set for the duration of one function call.
thread_local int g_variant = -1;
thread_local std::string g_variantError;

struct VariantScope {
    int prev;
    std::string prevError;
    explicit VariantScope(const Any& ref) : prev(g_variant), prevError(g_variantError) {
        OUString s;
        double d;
        std::string want;
        if ((ref >>= s) && !s.isEmpty()) want = u8(s);
        else if (ref.getValueTypeClass() == uno::TypeClass_DOUBLE && (ref >>= d)) {
            char b[40];
            std::snprintf(b, sizeof b, "%.15g", d);
            want = b;
        }
        if (want.empty()) return;
        auto labels = mc::Simulation::instance().resultSetLabels();
        for (size_t i = 0; i < labels.size(); ++i)
            if (mc::lower(labels[i]) == mc::lower(want)) { g_variant = int(i); return; }
        g_variantError = "unknown variant (Referenz): " + want;
    }
    ~VariantScope() { g_variant = prev; g_variantError = prevError; }
};

Vec samplesOf(const std::string& name) {
    if (!g_variantError.empty()) throw std::invalid_argument(g_variantError);
    Vec v;
    auto& sim = mc::Simulation::instance();
    bool ok = g_variant >= 0 ? sim.samplesIn(g_variant, name, v) : sim.samples(name, v);
    if (!ok) throw std::invalid_argument("unknown variable: " + name);
    return v;
}

// MC FLO percentile: the ROUNDUP(n * p)-th smallest value (wiki "Referenz")
double percentileMcflo(const Vec& v0, double p) {
    if (p < 0 || p > 1) throw std::invalid_argument("percentile must be in [0,1]");
    Vec s = mc::stats::sorted(mc::stats::finite(v0));
    if (s.empty()) return kNaN;
    size_t k = size_t(std::ceil(double(s.size()) * p - 1e-9));
    k = std::max<size_t>(1, std::min(k, s.size()));
    return s[k - 1];
}

// MC FLO percentile value: position of the first value >= x among the sorted values, 0..1
double percentValueMcflo(const Vec& v0, double x) {
    Vec s = mc::stats::sorted(mc::stats::finite(v0));
    if (s.empty()) return kNaN;
    size_t i = size_t(std::lower_bound(s.begin(), s.end(), x) - s.begin());
    if (i >= s.size()) return 1.0;
    return s.size() > 1 ? double(i) / double(s.size() - 1) : 0.5;
}

// share of the iterations at x (integer data) or in the histogram column containing x
double densityShare(const Vec& v0, double x, const Any& binsAny) {
    Vec v = mc::stats::finite(v0);
    if (v.empty()) return kNaN;
    bool integers = std::all_of(v.begin(), v.end(), [](double d) { return d == std::floor(d); });
    if (integers && x == std::floor(x)) return double(std::count(v.begin(), v.end(), x)) / double(v.size());
    int bins = int(optNum(binsAny, 0));
    if (bins < 1) bins = mc::Simulation::instance().settings().bins;
    auto h = mc::stats::histogram(v, bins);
    for (size_t i = 0; i < h.count.size(); ++i)
        if (x >= h.lo[i] && (x < h.hi[i] || (i + 1 == h.count.size() && x <= h.hi[i]))) return h.count[i] / double(v.size());
    return 0.0;
}

double withSamples(const OUString& name, const std::function<double(const Vec&)>& fn) {
    return guard([&] {
        if (noResults()) return -1.0;
        return finiteOr(fn(samplesOf(u8(name))));
    });
}

double withPair(const OUString& n1, const OUString& n2, const std::function<double(const Vec&, const Vec&)>& fn) {
    return guard([&] {
        if (noResults()) return -1.0;
        return finiteOr(fn(samplesOf(u8(n1)), samplesOf(u8(n2))));
    });
}

double iterValue(const Vec& v, double it) {
    long i = std::lround(it);
    if (i < 1 || size_t(i) > v.size()) throw std::invalid_argument("position out of range");
    return v[i - 1];
}

double sortedValue(const Vec& v, double pos) {
    auto s = mc::stats::sorted(v);
    long i = std::lround(pos);
    if (i < 1 || size_t(i) > s.size()) throw std::invalid_argument("position out of range");
    return s[i - 1];
}

double conditionPos(const Vec& v, double value) {
    size_t best = 0;
    double bd = 1e300;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!std::isfinite(v[i])) continue;
        double d = std::fabs(v[i] - value);
        if (d <= 1e-12 * std::max(1.0, std::fabs(value))) return double(i + 1);
        if (d < bd) { bd = d; best = i; }
    }
    return double(best + 1);
}

double spider(const Vec& a, const Vec& b, double v1, double v2) {
    double lo = std::min(v1, v2), hi = std::max(v1, v2);
    if (lo >= 0 && hi <= 1) { lo = mc::stats::percentile(a, lo); hi = mc::stats::percentile(a, hi); }
    Vec x, y;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] >= lo && a[i] <= hi) { x.push_back(a[i]); y.push_back(b[i]); }
    return mc::stats::pearson(x, y);
}

double financialValue(const Vec& a, const Vec& b, double v2, double diff, double p) {
    double d = diff > 1 ? diff / 100 : diff;
    double tol = v2 != 0 ? std::fabs(v2) * d : d;
    Vec sub;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (std::fabs(b[i] - v2) <= tol) sub.push_back(a[i]);
    if (sub.empty()) throw std::invalid_argument("no iteration of event B within the search area");
    return mc::stats::percentile(sub, p);
}

double conditionalPercentile(const Vec& a, const Vec& b, const std::string& op, double v, double p) {
    Vec sub;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (std::isfinite(b[i]) && mc::stats::compare(b[i], op, v)) sub.push_back(a[i]);
    if (sub.empty()) throw std::invalid_argument("event B never occurs");
    return mc::stats::percentile(sub, p);
}

// Conditions: (Name; Comparison; Value) triples, or (Name; ">="&100) pairs as in the MC FLO examples.
double bayesMulti(std::initializer_list<Any> args, bool meanOfA) {
    return guard([&] {
        if (noResults()) return -1.0;
        std::vector<Any> v(args);
        struct Ev { Vec x; std::string op; double val; };
        std::vector<Ev> ev;
        size_t i = 0;
        while (i < v.size()) {
            OUString n;
            if (!(v[i] >>= n) || n.isEmpty()) { ++i; continue; }
            Ev e{samplesOf(u8(n)), "", 0};
            ++i;
            OUString op;
            if (i < v.size() && (v[i] >>= op)) {
                std::string o = u8(op);
                size_t k = 0;
                while (k < o.size() && std::strchr("<>=!", o[k])) ++k;
                std::string rest = o.substr(k);
                bool ok = false;
                double num = parseNumber(rest, ok);
                e.op = o.substr(0, k) == "!=" ? "<>" : o.substr(0, k);
                ++i;
                if (ok && !rest.empty()) e.val = num;                     // combined ">="&100
                else if (i < v.size()) { e.val = optNum(v[i], 0); ++i; }  // separate value
            }
            ev.push_back(e);
        }
        if (ev.size() < 2) throw std::invalid_argument("at least events A and B are needed");
        double cb = 0, cab = 0, sum = 0;
        for (size_t it = 0; it < ev[0].x.size(); ++it) {
            bool ok = true;
            for (size_t e = 1; e < ev.size() && ok; ++e)
                ok = it < ev[e].x.size() && std::isfinite(ev[e].x[it]) && mc::stats::compare(ev[e].x[it], ev[e].op, ev[e].val);
            if (!ok || !std::isfinite(ev[0].x[it])) continue;
            ++cb;
            if (ev[0].op.empty() || mc::stats::compare(ev[0].x[it], ev[0].op, ev[0].val)) { ++cab; sum += ev[0].x[it]; }
        }
        if (cb == 0) throw std::invalid_argument("the conditions never occur together");
        if (meanOfA) {
            if (cab == 0) throw std::invalid_argument("event A never occurs under the conditions");
            return sum / cab;
        }
        return cab / cb;
    });
}

std::vector<std::pair<std::string, double>> pearsonDrivers(const std::string& output) {
    auto& sim = mc::Simulation::instance();
    Vec out = samplesOf(output), in;
    std::vector<std::pair<std::string, double>> d;
    for (const auto& n : sim.resultNames(mc::VarKind::Input)) {
        in = samplesOf(n);
        double r = mc::stats::pearson(in, out);
        d.push_back({n, std::isfinite(r) ? r : 0.0});
    }
    std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
    return d;
}

OUString corrRankName(const OUString& name, double rank) {
    return guard([&] {
        if (noResults()) return OUString("-1");
        auto d = pearsonDrivers(u8(name));
        long i = std::lround(rank);
        if (i < 1 || size_t(i) > d.size()) throw std::invalid_argument("rank out of range");
        return ou(d[i - 1].first);
    });
}

double corrRankValue(const OUString& name, double rank) {
    return guard([&] {
        if (noResults()) return -1.0;
        auto d = pearsonDrivers(u8(name));
        long i = std::lround(rank);
        if (i < 1 || size_t(i) > d.size()) throw std::invalid_argument("rank out of range");
        return d[i - 1].second;
    });
}

double normalTest(const Vec& v) {
    bool normal = false;
    if (!std::isfinite(mc::stats::andersonDarling(v, normal))) throw std::invalid_argument("at least 8 values are needed");
    return normal ? 1.0 : 0.0;
}

double andersonDarling(const Any& a) {
    return guard([&] {
        OUString s;
        if (a >>= s) {
            if (noResults()) return -1.0;
            return normalTest(samplesOf(u8(s)));
        }
        AnyMatrix m;
        if (a >>= m) return normalTest(numbers(m));
        throw std::invalid_argument("expected a variable name or a data area");
    });
}

OUString bootstrapText(const OUString& name, const AnyMatrix* data, double samples, double confidence, const Any& p) {
    return guard([&] {
        Vec v;
        if (data) v = numbers(*data);
        else {
            if (noResults()) return OUString("-1");
            v = samplesOf(u8(name));
        }
        if (!(confidence > 0 && confidence < 1)) throw std::invalid_argument("Confidence must be in (0,1)");
        int ns = int(std::lround(samples));
        if (ns < 2 || ns > 1000000) throw std::invalid_argument("the number of samples must be 2..1,000,000");
        std::mt19937_64 rng(kDataSeed);
        auto r = mc::stats::bootstrap(v, ns, confidence, optNum(p, 0), rng);
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.10g; %.10g; %.10g", r.estimate, r.lower, r.upper);
        return ou(buf);
    });
}

DoubleMatrix outputSamples(const Vec& data, bool returnData, int bins) {
    if (returnData) return column(data);
    return toMatrix(mc::bayes::histogram3(data, bins > 0 ? bins : mc::Simulation::instance().settings().bins));
}

DoubleMatrix noResultMatrix() { return toMatrix({{-1.0}}); }

DoubleMatrix kernel(const OUString& name, double adapt, double bins, const Any& bw, const Any& hist) {
    return guard([&] {
        if (noResults()) return noResultMatrix();
        Vec v = mc::stats::finite(samplesOf(u8(name)));
        std::mt19937_64 rng(kDataSeed);
        double b = optNum(bw, 0);
        if (optBool(hist, false)) {
            double lo = mc::stats::min(v), hi = mc::stats::max(v), r = hi - lo;
            return toMatrix(mc::bayes::kdeHistogram(v, lo - adapt / 100 * r, hi + adapt / 100 * r, b, std::max(1, int(bins))));
        }
        return column(mc::bayes::kdeSample(v, int(v.size()), b, adapt / 100, rng));
    });
}

DoubleMatrix kernel2(const AnyMatrix& data, const Any& lo, const Any& hi, const Any& bw, const Any& binsAny, const Any& hist,
                     const Any& retSample, const Any& sampleN) {
    return guard([&] {
        Vec d = numbers(data);
        int bins = std::max(1, int(optNum(binsAny, 30)));
        if (optBool(retSample, false)) {
            std::mt19937_64 rng(kDataSeed);
            Vec smp = mc::bayes::kdeSample(d, std::max(1, int(optNum(sampleN, 1000))), optNum(bw, 0), 0, rng);
            double l = optNum(lo, 0), h = optNum(hi, 0);
            if (h > l) for (auto& x : smp) x = std::min(h, std::max(l, x));
            return column(smp);
        }
        auto hgram = mc::bayes::kdeHistogram(d, optNum(lo, 0), optNum(hi, 0), optNum(bw, 0), bins);
        if (optBool(hist, false)) return toMatrix(hgram);
        Rows r;
        for (auto& x : hgram) r.push_back({x[2]});
        return toMatrix(r);
    });
}

// ---------------------------------------------------------------- Holt-Winters with given factors
DoubleMatrix hwModel(double a, double b, double g, double period, const AnyMatrix& data, const Any& number, const Any& order, bool mult) {
    return guard([&] {
        Vec y = numbers(data);
        int m = std::max(1, int(std::lround(period)));
        if (y.size() < size_t(2 * m)) throw std::invalid_argument("at least 2 x Period values are needed");
        if (a < 0 || a > 1 || b < 0 || b > 1 || g < 0 || g > 1) throw std::invalid_argument("alpha, beta and gamma must be in [0,1]");
        auto hw = mc::runHoltWinters(y, m, mult, a, b, g);
        if (!hw.ok) throw std::invalid_argument(mult ? "the multiplicative model needs positive data" : "the model could not be computed");
        int h = std::max(1, int(optNum(number, 1)));
        Vec out;
        if (optNum(order, 0) == 1) out = hw.fitted;
        for (double f : hw.forecast(h)) out.push_back(f);
        return column(out);
    });
}

// ---------------------------------------------------------------- Memory Calculation vector functions
Vec vectorOf(const Any& a) {
    mc::Value v = toValue(a);
    if (v.kind == mc::Value::Array) return v.numbers();
    if (v.isNumeric()) return {v.num};
    return {};
}

std::mt19937_64& memRng() {
    static std::mt19937_64 r(std::random_device{}());
    return r;
}

DoubleMatrix simulaMC(const char* flo, std::initializer_list<Any> params, const Any& countAny) {
    return guard([&] {
        const mc::FloFunction* f = mc::findFlo(flo);
        if (!f) throw std::invalid_argument("unknown distribution");
        std::vector<double> p;
        for (const auto& a : params) {
            mc::Value v = toValue(a);
            if (v.isEmpty()) throw std::invalid_argument("a parameter is missing");
            p.push_back(v.asNumber());
        }
        double n = optNum(countAny, 30000);
        if (n < 1 || n > 1048576) throw std::invalid_argument("Count must be between 1 and 1,048,576");
        std::unique_ptr<mc::Distribution> d;
        mc::Bounds bnd;
        if (f->make) d = f->make(p);
        else {   // TruncNormal
            d = mc::makeNormal(p[0], p[1]);
            bnd.lo = p[2];
            bnd.hi = p[3];
        }
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        Vec out(static_cast<size_t>(n));
        auto& rng = memRng();
        for (auto& x : out) x = mc::compoundSample(*d, bnd, uni(rng), 1, rng);
        return column(out);
    });
}

DoubleMatrix calculationMC(const Any& a1, const Any& a2, const OUString& opU) {
    return guard([&] {
        std::string op = u8(opU);
        mc::Value m1 = toValue(a1);
        Vec v2 = vectorOf(a2);
        auto& rng = memRng();
        if (op == "inc" || op == "const") {
            int n = int(optNum(a1, 0));
            double s = v2.empty() ? 0 : v2[0];
            Vec out;
            for (int i = 0; i < n; ++i) out.push_back(op == "inc" ? s + i + 1 : s);
            return column(out);
        }
        Rows rows;   // Vector1 as rows (n x m for + and *)
        if (m1.kind == mc::Value::Array)
            for (auto& r : *m1.arr) {
                Vec line;
                for (auto& c : r) if (c.kind == mc::Value::Number) line.push_back(c.num);
                if (!line.empty()) rows.push_back(line);
            }
        else if (m1.isNumeric()) rows.push_back({m1.num});
        if (op == "com") {   // compound: values (Vector1) summed over a random number of events (Vector2)
            Vec val;
            for (auto& r : rows) for (double x : r) val.push_back(x);
            if (val.empty()) throw std::invalid_argument("Vector1 is empty");
            std::uniform_int_distribution<size_t> pick(0, val.size() - 1);
            Vec out;
            for (double c : v2) {
                double s = 0;
                for (int k = 0; k < int(std::max(0.0, std::round(c))); ++k) s += val[pick(rng)];
                out.push_back(s);
            }
            return column(out);
        }
        if (op == "csme") {   // combine experts: draw from each column (expert) in turn
            Vec out;
            size_t cols = rows.empty() ? 0 : rows[0].size();
            if (cols == 0) throw std::invalid_argument("Vector1 is empty");
            std::uniform_int_distribution<size_t> pr(0, rows.size() - 1), pc(0, cols - 1);
            size_t n = v2.empty() ? rows.size() : size_t(std::max(1.0, v2[0]));
            for (size_t i = 0; i < n; ++i) {
                auto& r = rows[pr(rng)];
                out.push_back(r[std::min(r.size() - 1, pc(rng))]);
            }
            return column(out);
        }
        size_t n = std::max(rows.size(), v2.size());
        auto at2 = [&](size_t i) { return v2.empty() ? 0.0 : v2.size() == 1 ? v2[0] : v2[i % v2.size()]; };
        Vec out;
        for (size_t i = 0; i < n; ++i) {
            const Vec& r = rows.empty() ? Vec{0.0} : rows[rows.size() == 1 ? 0 : i % rows.size()];
            double x = r[0], y = at2(i), z;
            if (op == "+") { z = 0; for (double c : r) z += c; z += (r.size() == 1 ? y : 0); }
            else if (op == "*") { z = 1; for (double c : r) z *= c; z *= (r.size() == 1 ? y : 1); }
            else if (op == "-") z = x - y;
            else if (op == "/") z = x / y;
            else if (op == "*/+") z = x + x * y;
            else if (op == "*/-") z = x - x * y;
            else if (op == "//+") z = x + x / y;
            else if (op == "//-") z = x - x / y;
            else if (mc::lower(op) == "max") z = std::max(x, y);
            else if (mc::lower(op) == "min") z = std::min(x, y);
            else throw std::invalid_argument("unknown MMOperation: " + op);
            out.push_back(z);
        }
        return column(out);
    });
}

DoubleMatrix correlationMC(const Any& a1, const Any& a2, double r, const Any& type, const Any& doSort) {
    return guard([&] {
        Vec x = vectorOf(a1), y = vectorOf(a2);
        size_t n = std::min(x.size(), y.size());
        if (n < 2) throw std::invalid_argument("two vectors with at least 2 values are needed");
        x.resize(n);
        y.resize(n);
        std::sort(x.begin(), x.end());
        std::sort(y.begin(), y.end());
        auto res = mc::generateUniforms(2, int(n), {{0, 1, r, mc::copulaFromMcflo(int(optNum(type, 0)))}}, memRng());
        Vec rx = mc::stats::ranks(res.u[0]), ry = mc::stats::ranks(res.u[1]);
        Rows out;
        for (size_t i = 0; i < n; ++i) {
            size_t ix = std::min(n - 1, size_t(std::max(0.0, rx[i] - 1))), iy = std::min(n - 1, size_t(std::max(0.0, ry[i] - 1)));
            out.push_back({x[ix], y[iy]});
        }
        if (optBool(doSort, false)) std::sort(out.begin(), out.end(), [](const Vec& p, const Vec& q) { return p[0] < q[0]; });
        return toMatrix(out);
    });
}

DoubleMatrix boundedMC(const Any& a1, double lo, double hi, const Any& number) {
    return guard([&] {
        if (lo > hi) throw std::invalid_argument("LowerBound must not exceed UpperBound");
        Vec in;
        for (double x : vectorOf(a1)) if (x >= lo && x <= hi) in.push_back(x);
        if (in.empty()) throw std::invalid_argument("no value lies within the bounds");
        double n = optNum(number, 0);
        if (n >= 1) {   // resample to the requested size
            std::uniform_int_distribution<size_t> pick(0, in.size() - 1);
            Vec out(static_cast<size_t>(n));
            for (auto& x : out) x = in[pick(memRng())];
            return column(out);
        }
        return column(in);
    });
}

double predictiveLine(const OUString& slope, const OUString& icpt, const OUString& se, double x, const Any& p) {
    return guard([&] {
        if (noResults()) return -1.0;
        Vec b = samplesOf(u8(slope)), a = samplesOf(u8(icpt)), s;
        if (!se.isEmpty()) s = samplesOf(u8(se));
        std::mt19937_64 rng(kDataSeed);
        std::normal_distribution<double> nd;
        Vec y;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            double v = a[i] + b[i] * x;
            if (!s.empty() && i < s.size()) v += std::fabs(s[i]) * nd(rng);
            y.push_back(v);
        }
        double pc = optNum(p, 0);
        return finiteOr(pc > 0 && pc < 1 ? mc::stats::percentile(y, pc) : mc::stats::mean(y));
    });
}

DoubleMatrix mcmc(const OUString& n1, const OUString& n2, double burn, double thin, double n, const Any& retData,
                  const Any& bins, const Any& binsDivide, bool asym) {
    return guard([&] {
        if (noResults()) return noResultMatrix();
        std::mt19937_64 rng(kDataSeed);
        int ni = int(std::lround(n));
        if (ni < 1 || ni > 10000000) throw std::invalid_argument("NumberIterations must be 1..10,000,000");
        Vec post = mc::bayes::posteriorMCMC(samplesOf(u8(n1)), samplesOf(u8(n2)), ni, int(burn), int(std::max(1.0, thin)),
                                            int(optNum(binsDivide, 100)), asym, rng);
        return outputSamples(post, optBool(retData, true), int(optNum(bins, 0)));
    });
}

DoubleMatrix robustPosterior(const AnyMatrix& v1, const AnyMatrix& v2) {
    return guard([&] {
        Vec a = numbers(v1), b = numbers(v2);
        std::mt19937_64 rng(kDataSeed);
        return column(mc::bayes::posteriorGrid(a, b, int(std::max(a.size(), b.size())), rng));
    });
}

DoubleMatrix bayesPrediction(const OUString& n1, const Any& n2, double count, double attempts, double n, const Any& retData,
                             const Any& bins) {
    return guard([&] {
        if (noResults()) return noResultMatrix();
        std::mt19937_64 rng(kDataSeed);
        Vec p = mc::stats::finite(samplesOf(u8(n1)));
        OUString like;
        if ((n2 >>= like) && !like.isEmpty()) p = mc::bayes::posteriorGrid(p, samplesOf(u8(like)), int(p.size()), rng);
        int ni = int(std::lround(n)), k = int(std::lround(count));
        if (ni < 1 || k < 1) throw std::invalid_argument("Count and NumberIterations must be at least 1");
        std::uniform_int_distribution<size_t> pick(0, p.size() - 1);
        Vec out;
        for (int i = 0; i < ni; ++i) {
            double q = std::min(1 - 1e-9, std::max(1e-9, p[pick(rng)]));
            double f = std::negative_binomial_distribution<int>(k, q)(rng);
            out.push_back(std::min(f, attempts + 1));
        }
        return outputSamples(out, optBool(retData, true), int(optNum(bins, 0)));
    });
}

DoubleMatrix bayesCombine(std::initializer_list<Any> args, const Any& retData, const Any& bins) {
    return guard([&] {
        if (noResults()) return noResultMatrix();
        std::vector<Any> v(args);
        std::vector<Vec> s;
        Vec w;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            OUString n;
            if (!(v[i] >>= n) || n.isEmpty()) continue;
            s.push_back(samplesOf(u8(n)));
            w.push_back(optNum(v[i + 1], 1));
        }
        std::mt19937_64 rng(kDataSeed);
        int n = std::max(1000, mc::Simulation::instance().iterations());
        return outputSamples(mc::bayes::combine(s, w, n, rng), optBool(retData, true), int(optNum(bins, 0)));
    });
}

// ---------------------------------------------------------------- information / settings
double scenarioLevel(double mc::Settings::*field, const Any& value) {
    return guard([&] {
        auto& sim = mc::Simulation::instance();
        mc::Settings s = sim.settings();
        double v = optNum(value, -1);
        if (v >= 0 && v <= 1) { s.*field = v; sim.setSettings(s); }
        return s.*field;
    });
}

OUString variableName(int kind, double pos) {
    return guard([&] {
        auto& sim = mc::Simulation::instance();
        std::vector<std::string> n;
        for (const auto& v : sim.resultVariables()) {
            if (kind == 0 && v.kind == mc::VarKind::Input) n.push_back(v.name);
            if (kind == 1 && v.kind == mc::VarKind::Output) n.push_back(v.name);
            if (kind == 2 && v.kind == mc::VarKind::Input && v.userDefined) n.push_back(v.name);
        }
        if (n.empty()) return OUString("-1");
        long i = std::lround(pos);
        if (i < 1 || size_t(i) > n.size()) throw std::invalid_argument("position out of range");
        return ou(n[i - 1]);
    });
}

#ifdef MONTECALC_XLL
OUString variableInCell(const XlCell& cell) {
    return guard([&] {
        if (!cell.valid) throw std::invalid_argument("a cell reference is needed");
        std::string func;
        std::vector<std::string> args;
        if (!mc::formula::findCall(cell.formula, "FLOSIMULA_", func, args)) throw std::invalid_argument("the cell contains no MC FLO formula");
        const mc::FloFunction* fl = mc::findFlo(func);
        size_t nameArg = fl ? mc::nameArgIndex(*fl, args) : 0;
        if (fl && fl->kind == mc::FloFunction::Correlation) return ou(func);
        if (nameArg < args.size()) {
            bool isStr;
            std::string t = mc::formula::unquote(args[nameArg], isStr);
            if (isStr) return ou(t);
            int col, row;
            if (t.find('!') == std::string::npos && mc::formula::parseCellName(t, col, row) && cell.textAt) {
                std::string text = cell.textAt(row, col);
                bool numeric = false;
                parseNumber(text, numeric);
                if (!text.empty() && !numeric) return ou(text);
            }
        }
        return ou(mc::formula::sanitizeName(cell.sheet) + "_" + mc::formula::columnName(cell.col) + std::to_string(cell.row + 1));
    });
}
#else
OUString variableInCell(const Reference<table::XCellRange>& range) {
    return guard([&] {
        if (!range.is()) throw std::invalid_argument("a cell reference is needed");
        Reference<table::XCell> cell = range->getCellByPosition(0, 0);
        std::string f = u8(cell->getFormula());
        Reference<sheet::XSheetCellRange> scr(range, UNO_QUERY);
        Reference<sheet::XCellRangeAddressable> addr(range, UNO_QUERY);
        std::string sheetName;
        if (scr.is()) sheetName = u8(Reference<container::XNamed>(scr->getSpreadsheet(), UNO_QUERY)->getName());
        std::string func;
        std::vector<std::string> args;
        if (!mc::formula::findCall(f, "FLOSIMULA_", func, args)) throw std::invalid_argument("the cell contains no MC FLO formula");
        const mc::FloFunction* fl = mc::findFlo(func);
        size_t nameArg = fl ? mc::nameArgIndex(*fl, args) : 0;   // MC FLO order: the name follows the parameters
        if (fl && fl->kind == mc::FloFunction::Correlation) return ou(func);
        if (nameArg < args.size()) {
            bool isStr;
            std::string t = mc::formula::unquote(args[nameArg], isStr);
            if (isStr) return ou(t);
            int col, row;
            std::string ref = t;
            size_t dot = ref.rfind('.');
            if (dot != std::string::npos) ref = ref.substr(dot + 1);
            if (mc::formula::parseCellName(ref, col, row) && scr.is() && dot == std::string::npos) {
                std::string text = u8(Reference<text::XText>(scr->getSpreadsheet()->getCellByPosition(col, row), UNO_QUERY)->getString());
                bool numeric = false;
                parseNumber(text, numeric);
                if (!text.empty() && !numeric) return ou(text);
            }
        }
        // unnamed input: default name <Sheet>_<Cell>
        auto a = addr->getRangeAddress();
        return ou(mc::formula::sanitizeName(sheetName) + "_" + mc::formula::columnName(a.StartColumn) + std::to_string(a.StartRow + 1));
    });
}

#endif

double setBins(double v) {
    return guard([&] {
        if (v < 2 || v > 1000) throw std::invalid_argument("bins must be 2..1000");
        auto& sim = mc::Simulation::instance();
        mc::Settings s = sim.settings();
        s.bins = int(std::lround(v));
        sim.setSettings(s);
        return v;
    });
}

double simulaOn(const Any& v) {
    return guard([&] {
        bool on = optBool(v, false);
        auto& sim = mc::Simulation::instance();
        if (!sim.busy()) sim.setMode(on ? mc::Mode::Random : mc::Mode::Expected);
        return on ? 1.0 : 0.0;
    });
}

// ---------------------------------------------------------------- data tools
double sortValue(const AnyMatrix& data, double pos, const Any& desc) {
    return guard([&] {
        auto s = mc::stats::sorted(numbers(data));
        if (optNum(desc, 0) != 0) std::reverse(s.begin(), s.end());
        long i = std::lround(pos);
        if (i < 1 || size_t(i) > s.size()) throw std::invalid_argument("position out of range");
        return s[i - 1];
    });
}

double bsmValue(double s0, double t, double r, double sigma) {
    return guard([&] {
        if (!(s0 > 0) || t < 0 || sigma < 0) throw std::invalid_argument("requires Basis > 0, Time >= 0, Sigma >= 0");
        auto& sim = mc::Simulation::instance();
        mc::Mode m = sim.mode();
        if (m == mc::Mode::Simulate || m == mc::Mode::Random) {
            double z = std::normal_distribution<double>()(sim.rng());
            return s0 * std::exp((r - 0.5 * sigma * sigma) * t + sigma * std::sqrt(t) * z);
        }
        return s0 * std::exp(r * t);
    });
}

DoubleMatrix bootstrapVector(const AnyMatrix& data, double n, const Any& pct, const Any& hits, const Any& compound) {
    return guard([&] {
        Vec v = numbers(data);
        if (v.empty()) throw std::invalid_argument("no data");
        int ns = int(std::lround(n));
        if (ns < 1 || ns > 1000000) throw std::invalid_argument("NumNewExamples must be 1..1,000,000");
        double p = optNum(pct, 0);
        bool h = optBool(hits, false);
        int comp = int(std::lround(optNum(compound, 1)));
        std::mt19937_64 rng(kDataSeed);
        std::uniform_int_distribution<size_t> pick(0, v.size() - 1);
        Vec out, draw(v.size());
        for (int s = 0; s < ns; ++s) {
            if (comp > 1) {
                double sum = 0;
                for (int k = 0; k < comp; ++k) sum += v[pick(rng)];
                out.push_back(sum);
                continue;
            }
            for (auto& d : draw) d = v[pick(rng)];
            if (h) out.push_back(double(std::count(draw.begin(), draw.end(), 1.0)) / draw.size());
            else out.push_back(p > 0 && p < 1 ? mc::stats::percentile(draw, p) : mc::stats::mean(draw));
        }
        return column(out);
    });
}

double financialDTrans(double initial, double periods, double year, double amount) {
    return guard([&] {
        if (!(periods > 0)) throw std::invalid_argument("Periods must be positive");
        double a = std::max(initial, year), b = std::min(initial + periods, year + 1);
        return b > a ? amount * (b - a) / periods : 0.0;
    });
}

// ---------------------------------------------------------------- regression
struct RegData { Vec y; mc::bayes::Mat x; };

RegData regData(const AnyMatrix& Y, const AnyMatrix& X) {
    Vec y = cells(Y);
    Rows xm = matrixOf(X);
    RegData d;
    size_t n = y.size();
    if (xm.size() != n) {
        // series given in rows: transpose
        if (!xm.empty() && xm[0].size() == n) {
            Rows t(n, Vec(xm.size()));
            for (size_t i = 0; i < xm.size(); ++i)
                for (size_t j = 0; j < n; ++j) t[j][i] = xm[i][j];
            xm = t;
        } else {
            throw std::invalid_argument("DataY and DataX must have the same number of observations");
        }
    }
    for (size_t i = 0; i < n; ++i) {
        bool ok = std::isfinite(y[i]);
        for (double v : xm[i]) ok = ok && std::isfinite(v);
        if (ok) { d.y.push_back(y[i]); d.x.push_back(xm[i]); }
    }
    if (d.y.size() < d.x[0].size() + 2) throw std::invalid_argument("not enough complete observations");
    return d;
}

mc::bayes::Mat bootstrapParams(const AnyMatrix& Y, const AnyMatrix& X, double n, const Any& urm) {
    RegData d = regData(Y, X);
    int ns = int(std::lround(n));
    if (ns < 2 || ns > 1000000) throw std::invalid_argument("NumberNewSamples must be 2..1,000,000");
    std::mt19937_64 rng(kDataSeed);
    return mc::bayes::bootstrapRegression(d.y, d.x, ns, optBool(urm, false), rng);
}

DoubleMatrix bootstrapRegressionColumn(const AnyMatrix& Y, const AnyMatrix& X, double n, const Any& urm, int col) {
    return guard([&] {
        auto p = bootstrapParams(Y, X, n, urm);
        Vec v;
        for (auto& r : p) v.push_back(col < 0 ? r.back() : r[col]);
        return column(v);
    });
}

DoubleMatrix bootstrapRegressionAll(const AnyMatrix& Y, const AnyMatrix& X, double n, const Any& urm) {
    return guard([&] { return toMatrix(bootstrapParams(Y, X, n, urm)); });
}

AnyMatrix paramSummary(const mc::bayes::Mat& p, double ci, const Any& labelsAny) {
    auto labels = stringsOfAny(labelsAny);
    size_t m = p.empty() ? 0 : p[0].size();
    std::vector<std::vector<Any>> rows{{anyText("Parameter"), anyText("Mean"), anyText("Lower"), anyText("Upper")}};
    double a = (1 - ci) / 2;
    for (size_t j = 0; j < m; ++j) {
        Vec col;
        for (auto& r : p) col.push_back(r[j]);
        std::string label = j == 0 ? "Intercept" : j + 1 == m ? "Std. error of regression"
                                                              : (j - 1 < labels.size() ? labels[j - 1] : "Variable_" + std::to_string(j));
        rows.push_back({anyText(label), anyNum(mc::stats::mean(col)), anyNum(mc::stats::percentile(col, a)),
                        anyNum(mc::stats::percentile(col, 1 - a))});
    }
    return toAnyMatrix(rows);
}

AnyMatrix bootstrapRegressionParams(const AnyMatrix& Y, const AnyMatrix& X, double n, const Any& urm, const Any& ci, const Any& labels) {
    return guard([&] { return paramSummary(bootstrapParams(Y, X, n, urm), optNum(ci, 0.95), labels); });
}

mc::bayes::Mat bayesParams(RegData& d, const Any& useLog, const Any& nNew, const Any& urm, const Any& kfolds) {
    if (optBool(useLog, false))
        for (auto& v : d.y) {
            if (v <= 0) throw std::invalid_argument("UseLogForY needs positive Y values");
            v = std::log(v);
        }
    int n = int(std::lround(optNum(nNew, 2000)));
    if (n < 10 || n > 1000000) throw std::invalid_argument("NumNewSample must be 10..1,000,000");
    std::mt19937_64 rng(kDataSeed);
    return mc::bayes::bayesRegression(d.y, d.x, n, optBool(urm, false), int(optNum(kfolds, 1)), rng);
}

double bayesRegPredict(const AnyMatrix& Y, const AnyMatrix& X, const Any& value, const Any& pct, const Any& useLog,
                       const Any& urm, const Any& nNew, const Any& kfolds) {
    return guard([&] {
        RegData d = regData(Y, X);
        auto p = bayesParams(d, useLog, nNew, urm, kfolds);
        mc::Value xv = toValue(value);
        Vec x = xv.numbers();
        if (x.size() != d.x[0].size()) throw std::invalid_argument("Value must have one entry per feature");
        std::mt19937_64 rng(kDataSeed + 1);
        Vec pred = mc::bayes::predictive(p, x, true, rng);
        if (optBool(useLog, false)) for (auto& v : pred) v = std::exp(v);
        double pc = optNum(pct, 0);
        return pc > 0 && pc < 1 ? mc::stats::percentile(pred, pc) : mc::stats::mean(pred);
    });
}

AnyMatrix bayesRegParams(const AnyMatrix& Y, const AnyMatrix& X, const Any& ci, const Any& labels, const Any& useLog,
                         const Any& nNew, const Any& urm, const Any& kfolds) {
    return guard([&] {
        RegData d = regData(Y, X);
        return paramSummary(bayesParams(d, useLog, nNew, urm, kfolds), optNum(ci, 0.95), labels);
    });
}

// kind 0: coverage rate, 1: MAE; second = residuals
std::pair<double, Vec> bayesRegTest(int kind, const AnyMatrix& ty, const AnyMatrix& tx, const AnyMatrix& vy, const AnyMatrix& vx,
                                    const Any& pct, const Any& useLog, const Any& nNew, const Any& urm, const Any& kfolds) {
    return guard([&] {
        RegData d = regData(ty, tx), t = regData(vy, vx);
        auto p = bayesParams(d, useLog, nNew, urm, kfolds);
        bool lg = optBool(useLog, false);
        double cov = optNum(pct, 0.95);
        std::mt19937_64 rng(kDataSeed + 2);
        double inside = 0, mae = 0;
        Vec res;
        for (size_t i = 0; i < t.y.size(); ++i) {
            Vec pred = mc::bayes::predictive(p, t.x[i], true, rng);
            if (lg) for (auto& v : pred) v = std::exp(v);
            double lo = mc::stats::percentile(pred, (1 - cov) / 2), hi = mc::stats::percentile(pred, 1 - (1 - cov) / 2);
            if (t.y[i] >= lo && t.y[i] <= hi) ++inside;
            double m = mc::stats::mean(pred);
            mae += std::fabs(t.y[i] - m);
            res.push_back(t.y[i] - m);
        }
        double n = double(t.y.size());
        return std::make_pair(kind == 0 ? inside / n / cov : mae / n, res);
    });
}

DoubleMatrix bayesRegResiduals(const AnyMatrix& ty, const AnyMatrix& tx, const AnyMatrix& vy, const AnyMatrix& vx, const Any& useLog,
                               const Any& nNew, const Any& urm, const Any& kfolds) {
    return column(bayesRegTest(1, ty, tx, vy, vx, Any(), useLog, nNew, urm, kfolds).second);
}

double bayesFactor(double p0, double pObs, double n, const Any& pct) {
    return guard([&] {
        double pc = optNum(pct, 0);
        auto r = mc::bayes::bayesFactor(p0, pObs, n, pc);
        return pc > 0 && pc < 1 ? r.credible : r.bf10;
    });
}

AnyMatrix classifier(const AnyMatrix& feat, const AnyMatrix& cls, const AnyMatrix& pred, const Any& testProp, const Any& model,
                     const Any& confusion, const Any& predInterval, const Any& interval, const Any& iterations, const Any& hisBins,
                     const Any& lower, const Any& upper, const Any& impact, const Any& featureDesc, const Any& kfolds,
                     const Any& flatPrior, const Any& weighted) {
    return guard([&] {
        Rows x = matrixOf(feat);
        auto y = strings(cls);
        if (x.size() != y.size()) throw std::invalid_argument("FeatureData and ClassData must have the same number of rows");
        mc::bayes::ClassifierOptions o;
        o.model = int(optNum(model, 0));
        o.flatPrior = optBool(flatPrior, false);
        o.weightedFeatures = optBool(weighted, false);
        o.bins = int(optNum(hisBins, 30));
        o.lower = optNum(lower, 0);
        o.upper = optNum(upper, 0);
        double tp = optNum(testProp, 0.2);
        if (!(tp >= 0 && tp < 1)) throw std::invalid_argument("Testproportion must be in [0,1)");
        std::mt19937_64 rng(kDataSeed);
        auto split = [&](Rows& trX, std::vector<std::string>& trY, Rows& teX, std::vector<std::string>& teY) {
            std::vector<size_t> idx(x.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::shuffle(idx.begin(), idx.end(), rng);
            size_t nt = size_t(std::round(tp * x.size()));
            for (size_t k = 0; k < idx.size(); ++k) {
                if (k < nt) { teX.push_back(x[idx[k]]); teY.push_back(y[idx[k]]); }
                else { trX.push_back(x[idx[k]]); trY.push_back(y[idx[k]]); }
            }
        };
        if (optBool(confusion, false)) {
            if (tp <= 0) throw std::invalid_argument("Testproportion must be > 0 for a confusion matrix");
            int folds = std::max(1, int(optNum(kfolds, 1)));
            std::map<std::pair<std::string, std::string>, double> cm;
            std::vector<std::string> classes;
            for (int f = 0; f < folds; ++f) {
                Rows trX, teX;
                std::vector<std::string> trY, teY;
                split(trX, trY, teX, teY);
                mc::bayes::NaiveBayes nb;
                nb.train(trX, trY, o);
                if (classes.empty()) classes = nb.classes();
                for (size_t i = 0; i < teX.size(); ++i) cm[{teY[i], nb.predict(teX[i])}] += 1.0 / folds;
            }
            for (const auto& c : y) if (std::find(classes.begin(), classes.end(), c) == classes.end()) classes.push_back(c);
            std::vector<std::vector<Any>> rows;
            std::vector<Any> head{anyText("actual \\ predicted")};
            for (auto& c : classes) head.push_back(anyText(c));
            rows.push_back(head);
            for (auto& a : classes) {
                std::vector<Any> r{anyText(a)};
                for (auto& p : classes) r.push_back(anyNum(cm[{a, p}]));
                rows.push_back(r);
            }
            return toAnyMatrix(rows);
        }
        if (optBool(impact, false)) {
            Rows trX, teX;
            std::vector<std::string> trY, teY;
            split(trX, trY, teX, teY);
            if (teX.empty()) { teX = trX; teY = trY; }
            mc::bayes::NaiveBayes nb;
            nb.train(trX, trY, o);
            auto acc = [&](const Rows& data) {
                double ok = 0;
                for (size_t i = 0; i < data.size(); ++i) ok += nb.predict(data[i]) == teY[i];
                return ok / data.size();
            };
            double base = acc(teX);
            auto labels = stringsOfAny(featureDesc);
            std::vector<std::vector<Any>> rows{{anyText("Feature"), anyText("Accuracy drop")}};
            for (size_t f = 0; f < teX[0].size(); ++f) {
                Rows perm = teX;
                std::vector<double> col;
                for (auto& r : perm) col.push_back(r[f]);
                std::shuffle(col.begin(), col.end(), rng);
                for (size_t i = 0; i < perm.size(); ++i) perm[i][f] = col[i];
                rows.push_back({anyText(f < labels.size() ? labels[f] : "Variable_" + std::to_string(f + 1)), anyNum(base - acc(perm))});
            }
            return toAnyMatrix(rows);
        }
        // predictions for PredictData (trained on all data)
        Rows px = matrixOf(pred);
        mc::bayes::NaiveBayes nb;
        nb.train(x, y, o);
        std::vector<std::vector<Any>> rows;
        if (!optBool(predInterval, false)) {
            for (auto& r : px) rows.push_back({anyText(nb.predict(r))});
            return toAnyMatrix(rows);
        }
        double lvl = optNum(interval, 0.9);
        int its = std::min(500, std::max(10, int(optNum(iterations, 100))));
        std::vector<Rows> probs(px.size());
        std::uniform_int_distribution<size_t> pick(0, x.size() - 1);
        std::vector<std::string> cls0 = nb.classes();
        for (int it = 0; it < its; ++it) {
            Rows bx;
            std::vector<std::string> by;
            for (size_t i = 0; i < x.size(); ++i) { size_t k = pick(rng); bx.push_back(x[k]); by.push_back(y[k]); }
            mc::bayes::NaiveBayes b;
            try { b.train(bx, by, o); } catch (...) { continue; }
            for (size_t r = 0; r < px.size(); ++r) {
                Vec pr = b.probabilities(px[r]);
                Vec full(cls0.size(), 0.0);
                for (size_t c = 0; c < b.classes().size(); ++c) {
                    size_t pos = std::find(cls0.begin(), cls0.end(), b.classes()[c]) - cls0.begin();
                    if (pos < full.size()) full[pos] = pr[c];
                }
                probs[r].push_back(full);
            }
        }
        rows.push_back({anyText("Class"), anyText("Probability lower"), anyText("Probability upper")});
        for (size_t r = 0; r < px.size(); ++r) {
            std::string c = nb.predict(px[r]);
            size_t ci = std::find(cls0.begin(), cls0.end(), c) - cls0.begin();
            Vec pc;
            for (auto& p : probs[r]) pc.push_back(p[ci]);
            rows.push_back({anyText(c), anyNum(mc::stats::percentile(pc, (1 - lvl) / 2)), anyNum(mc::stats::percentile(pc, 1 - (1 - lvl) / 2))});
        }
        return toAnyMatrix(rows);
    });
}

// ---------------------------------------------------------------- batch / matrix
OUString batchMarker(const AnyMatrix& data, int kind) {
    return guard([&] {
        size_t n = numbers(data).size();
        if (n < 8) throw std::invalid_argument("at least 8 historical values are needed");
        return ou(std::string(kind ? "FLO batch (Holt-Winters): " : "FLO batch (time series): ") + std::to_string(n) +
                  " values - run Start Batch");
    });
}

std::string varRef(const std::string& name, bool input) { return std::string(input ? "FLO_i_" : "FLO_o_") + mc::formula::sanitizeName(name); }

AnyMatrix portfolioMatrix(const AnyMatrix& names, const Any& asInput) {
    return guard([&] {
        auto n = strings(names);
        if (n.empty() || n.size() > 16) throw std::invalid_argument("1..16 titles are supported");
        bool in = optBool(asInput, true);
        std::vector<std::vector<Any>> rows;
        size_t total = (size_t(1) << n.size()) - 1;
        std::vector<size_t> masks;
        for (size_t m = 1; m <= total; ++m) masks.push_back(m);
        std::stable_sort(masks.begin(), masks.end(), [](size_t a, size_t b) { return std::bitset<64>(a).count() < std::bitset<64>(b).count(); });
        for (size_t m : masks) {
            std::string f = "=", label;
            for (size_t i = 0; i < n.size(); ++i)
                if (m >> i & 1) {
                    f += (f.size() > 1 ? "+" : "") + varRef(n[i], in);
                    label += (label.empty() ? "" : "+") + n[i];
                }
            rows.push_back({anyText(f), anyText(label)});
        }
        return toAnyMatrix(rows);
    });
}

AnyMatrix portfolioCorr(const AnyMatrix& names, const Any& fill) {
    return guard([&] {
        auto n = strings(names);
        bool rnd = optBool(fill, false);
        std::mt19937_64 rng(kDataSeed);
        std::uniform_real_distribution<double> u(-0.3, 0.3);
        std::vector<std::vector<Any>> rows;
        for (size_t i = 0; i < n.size(); ++i)
            for (size_t j = i + 1; j < n.size(); ++j) {
                char c[32];
                std::snprintf(c, sizeof c, "%.3f", rnd ? u(rng) : 0.0);
                rows.push_back({anyText("=FLOsimula_correlation(\"" + n[i] + "\";\"" + n[j] + "\";" + c + ")")});
            }
        if (rows.empty()) throw std::invalid_argument("at least two titles are needed");
        return toAnyMatrix(rows);
    });
}

AnyMatrix portfolioCorrF(const AnyMatrix& names, const Any& type) {
    return guard([&] {
        if (noResults()) return toAnyMatrix({{Any(-1.0)}});
        auto n = strings(names);
        bool sp = optNum(type, 0) != 0;
        std::vector<std::vector<Any>> rows;
        for (size_t i = 0; i < n.size(); ++i)
            for (size_t j = i + 1; j < n.size(); ++j) {
                Vec a = samplesOf(n[i]), b = samplesOf(n[j]);
                rows.push_back({anyText(n[i]), anyText(n[j]), anyNum(sp ? mc::stats::spearman(a, b) : mc::stats::pearson(a, b))});
            }
        if (rows.empty()) throw std::invalid_argument("at least two variables are needed");
        return toAnyMatrix(rows);
    });
}

AnyMatrix portfolioManual(const AnyMatrix& names, const AnyMatrix& choice, const Any& isInput) {
    return guard([&] {
        auto n = strings(names);
        Rows c = matrixOf(choice);
        if (c.size() != n.size()) throw std::invalid_argument("aMatrix and cMatrix must have the same number of rows");
        bool in = optBool(isInput, true);
        std::vector<std::vector<Any>> rows;
        for (size_t j = 0; !c.empty() && j < c[0].size(); ++j) {
            std::string f = "=", label;
            for (size_t i = 0; i < n.size(); ++i)
                if (j < c[i].size() && c[i][j] == 1) {
                    f += (f.size() > 1 ? "+" : "") + varRef(n[i], in);
                    label += (label.empty() ? "" : "+") + n[i];
                }
            if (!label.empty()) rows.push_back({anyText(f), anyText(label)});
        }
        if (rows.empty()) throw std::invalid_argument("no portfolio selected");
        return toAnyMatrix(rows);
    });
}

DoubleMatrix markov(const AnyMatrix& m, double periods) {
    return guard([&] {
        Rows a = matrixOf(m);
        if (a.empty()) throw std::invalid_argument("empty matrix");
        size_t k = a[0].size();
        Vec state(k, 0.0);
        Rows P;
        if (a.size() == k + 1) { state = a[0]; P.assign(a.begin() + 1, a.end()); }
        else if (a.size() == k) { state[0] = 1; P = a; }
        else throw std::invalid_argument("aMatrix must be m x m (start in state 1) or (m+1) x m with the start distribution first");
        for (auto& r : P) {
            double s = 0;
            for (double v : r) { if (!(v >= 0)) throw std::invalid_argument("negative transition probability"); s += v; }
            if (std::fabs(s - 1) > 1e-6) throw std::invalid_argument("each row of the transition matrix must sum to 1");
        }
        long n = std::lround(periods);
        if (n < 0 || n > 1000000) throw std::invalid_argument("n must be 0..1,000,000");
        for (long t = 0; t < n; ++t) {
            Vec next(k, 0.0);
            for (size_t i = 0; i < k; ++i)
                for (size_t j = 0; j < k; ++j) next[j] += state[i] * P[i][j];
            state = next;
        }
        return toMatrix({state});
    });
}

AnyMatrix digitTest(const AnyMatrix& data, const Any& summary, bool last) {
    return guard([&] {
        auto t = last ? mc::bayes::lastDigit(numbers(data)) : mc::bayes::benford(numbers(data));
        std::vector<std::vector<Any>> rows;
        for (size_t i = 0; i < t.observed.size(); ++i)
            rows.push_back({Any(double(last ? i : i + 1)), anyNum(t.observed[i]), anyNum(t.expected[i])});
        bool sum = optBool(summary, true);
        std::vector<std::pair<std::string, Any>> s = {
            {"Observations", anyNum(t.n)}, {"Chi-squared", anyNum(t.chi2)}, {"Degrees of freedom", anyNum(double(t.observed.size() - 1))},
            {"p-value", anyNum(t.pValue)}, {"Conforming (5%)", Any(t.pValue >= 0.05)}, {"Mean absolute deviation", anyNum(t.mad)}};
        for (size_t i = 0; i < 9; ++i) {
            if (sum && i < s.size()) rows.push_back({anyText(s[i].first), s[i].second, Any(OUString())});
            else rows.push_back({Any(OUString()), Any(OUString()), Any(OUString())});
        }
        return toAnyMatrix(rows);
    });
}

} // namespace

// ============================== generated function bodies ================
#ifdef MONTECALC_XLL
#include "functions_impl_xll.inc"
#else
#include "functions_impl.inc"
#endif

#ifndef MONTECALC_XLL
// ============================== XAddIn ===================================

OUString MonteCalcAddIn::getProgrammaticFuntionName(const OUString& aDisplayName) {
    for (const auto& f : functionTable())
        if (aDisplayName.equalsIgnoreAsciiCaseAscii(f.name)) return OUString::createFromAscii(f.prog);
    return OUString();
}

OUString MonteCalcAddIn::getDisplayFunctionName(const OUString& prog) {
    const FuncInfo* f = findFunc(prog);
    return f ? OUString::createFromAscii(f->name) : OUString();
}

OUString MonteCalcAddIn::getFunctionDescription(const OUString& prog) {
    const FuncInfo* f = findFunc(prog);
    return f ? ou(f->desc) : OUString();
}

OUString MonteCalcAddIn::getDisplayArgumentName(const OUString& prog, sal_Int32 n) {
    const FuncInfo* f = findFunc(prog);
    if (!f || n < 0 || size_t(n) >= f->args.size()) return OUString();
    return ou(f->args[n].name);
}

OUString MonteCalcAddIn::getArgumentDescription(const OUString& prog, sal_Int32 n) {
    const FuncInfo* f = findFunc(prog);
    if (!f || n < 0 || size_t(n) >= f->args.size()) return OUString();
    return ou(f->args[n].desc);
}

OUString MonteCalcAddIn::getProgrammaticCategoryName(const OUString&) { return "Add-In"; }
OUString MonteCalcAddIn::getDisplayCategoryName(const OUString&) { return "Add-In"; }
OUString MonteCalcAddIn::getServiceName() { return OUString::createFromAscii(kAddInServiceName); }
OUString MonteCalcAddIn::getImplementationName() { return OUString::createFromAscii(kAddInImplName); }
sal_Bool MonteCalcAddIn::supportsService(const OUString& s) { return cppu::supportsService(this, s); }
Sequence<OUString> MonteCalcAddIn::getSupportedServiceNames() { return serviceNames(); }
Sequence<OUString> MonteCalcAddIn::serviceNames() {
    return seq<OUString>({OUString::createFromAscii(kAddInServiceName), OUString("com.sun.star.sheet.AddIn")});
}
void MonteCalcAddIn::setLocale(const lang::Locale& l) { locale_ = l; }
lang::Locale MonteCalcAddIn::getLocale() { return locale_; }

#endif

} // namespace montecalc
