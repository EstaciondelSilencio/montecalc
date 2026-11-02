// MonteCalc - Calc add-in exposing the simulation functions
#include "addin.hpp"
#include "util.hpp"

#include "../core/simulation.h"
#include "../core/specfun.h"
#include "../core/stats.h"

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <cppuhelper/supportsservice.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Sequence;

namespace montecalc {

namespace {

struct ArgInfo { const char* name; const char* desc; };
struct FuncInfo {
    const char* prog;
    const char* display;
    const char* desc;
    std::vector<ArgInfo> args;
};

const std::vector<FuncInfo>& functionTable() {
    static const std::vector<FuncInfo> table = {
#include "functions_meta.inc"
    };
    return table;
}

const FuncInfo* findProg(const OUString& prog) {
    std::string p = u8(prog);
    for (const auto& f : functionTable())
        if (p == f.prog) return &f;
    return nullptr;
}

double optNum(const Any& a, double def) {
    if (!a.hasValue()) return def;
    double d;
    if (a >>= d) return d;
    sal_Int32 i;
    if (a >>= i) return i;
    bool b;
    if (a >>= b) return b ? 1 : 0;
    OUString s;
    if (a >>= s) {
        if (s.trim().isEmpty()) return def;
        bool ok = false;
        double v = parseNumber(u8(s), ok);
        if (ok) return v;
    }
    throw std::invalid_argument("optional argument must be a number");
}

std::vector<double> numbers(const AnyMatrix& m) {
    std::vector<double> r;
    for (const auto& row : m)
        for (const auto& c : row) {
            double d;
            if (c >>= d) r.push_back(d);
        }
    return r;
}

// Columns of a range as vectors of numbers (non-numeric cells -> NaN)
std::vector<std::vector<double>> columns(const AnyMatrix& m) {
    size_t ncol = 0;
    for (const auto& row : m) ncol = std::max<size_t>(ncol, row.getLength());
    std::vector<std::vector<double>> cols(ncol);
    for (const auto& row : m)
        for (sal_Int32 j = 0; j < row.getLength(); ++j) {
            double d;
            cols[j].push_back((row[j] >>= d) ? d : std::nan(""));
        }
    return cols;
}

double normalTest(const std::vector<double>& v) {
    bool normal = false;
    mc::stats::andersonDarling(v, normal);
    return normal ? 1.0 : 0.0;
}

double iterValue(const std::vector<double>& v, double it) {
    long i = std::lround(it);
    if (i < 1 || size_t(i) > v.size()) throw std::invalid_argument("iteration out of range");
    return v[i - 1];
}

double sortedValue(const std::vector<double>& v, double pos) {
    auto s = mc::stats::sorted(v);
    long i = std::lround(pos);
    if (i < 1 || size_t(i) > s.size()) throw std::invalid_argument("position out of range");
    return s[i - 1];
}

int binsDefault() { return mc::Simulation::instance().settings().bins; }

std::string describe(const char* label, std::initializer_list<double> params) {
    std::ostringstream os;
    os << label << "(";
    bool first = true;
    for (double p : params) {
        if (!first) os << "; ";
        os << p;
        first = false;
    }
    os << ")";
    return os.str();
}

[[noreturn]] void raise(const std::string& msg) {
    throw lang::IllegalArgumentException(ou(msg), uno::Reference<uno::XInterface>(), 0);
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

mc::Bounds bounds(const Any& lower, const Any& upper) {
    mc::Bounds b;
    b.lo = optNum(lower, b.lo);
    b.hi = optNum(upper, b.hi);
    if (b.lo > b.hi) throw std::invalid_argument("lower bound must not exceed upper bound");
    return b;
}

int countArg(const Any& count) {
    double c = optNum(count, 1);
    if (c < 1 || c > 1e7) throw std::invalid_argument("Count must be between 1 and 10,000,000");
    return int(std::lround(c));
}

} // namespace

// ============================== helpers used by the generated bodies =====

double MonteCalcAddIn::distInput(const OUString& name, const char* label,
                                 std::initializer_list<double> params,
                                 const std::function<mc::DistPtr()>& make, const Any& lower,
                                 const Any& upper, const Any& count) {
    return guard([&] {
        std::shared_ptr<mc::Distribution> d(make().release());
        mc::Bounds b = bounds(lower, upper);
        int n = countArg(count);
        double expected = mc::expectedValue(*d, b) * n;
        std::string desc = describe(label, params);
        if (b.active()) desc += " bounded";
        if (n > 1) desc += " x" + std::to_string(n);
        return mc::Simulation::instance().input(
            u8(name), desc, expected,
            [d, b, n](double u, std::mt19937_64& rng) { return mc::compoundSample(*d, b, u, n, rng); });
    });
}

double MonteCalcAddIn::customInput(const OUString& name, const AnyMatrix& data, const Any& bins,
                                   const Any& lower, const Any& upper, const Any& count) {
    return guard([&] {
        auto cols = columns(data);
        std::function<mc::DistPtr()> make;
        std::string label;
        if (cols.size() >= 3) {
            // histogram: Min, Max, Number (further columns ignored)
            std::vector<double> mins, maxs, cnt;
            for (size_t i = 0; i < cols[0].size(); ++i)
                if (std::isfinite(cols[0][i]) && std::isfinite(cols[1][i]) && std::isfinite(cols[2][i])) {
                    mins.push_back(cols[0][i]); maxs.push_back(cols[1][i]); cnt.push_back(cols[2][i]);
                }
            make = [=] { return mc::makeHistogram(mins, maxs, cnt); };
            label = "CustomHistogram";
        } else if (cols.size() == 2 && data.getLength() > 2) {
            // quantile table: Percentile, Value
            std::vector<std::pair<double, double>> pts;
            for (size_t i = 0; i < cols[0].size(); ++i)
                if (std::isfinite(cols[0][i]) && std::isfinite(cols[1][i]))
                    pts.push_back({cols[1][i], cols[0][i]});
            std::sort(pts.begin(), pts.end());
            std::vector<double> x, p;
            for (auto& [xv, pv] : pts) { x.push_back(xv); p.push_back(pv); }
            make = [=] { return mc::makePiecewiseLinear(x, p); };
            label = "CustomQuantiles";
        } else {
            std::vector<double> v = numbers(data);
            int nb = int(optNum(bins, 0));
            make = [=] { return mc::makeEmpirical(v, nb); };
            label = "CustomData";
        }
        return distInput(name, label.c_str(), {}, make, lower, upper, count);
    });
}

double MonteCalcAddIn::discreteCustomInput(const OUString& name, const AnyMatrix& values,
                                           const AnyMatrix& probs, const Any& lower,
                                           const Any& upper, const Any& count) {
    std::vector<double> v = numbers(values), p = numbers(probs);
    return distInput(name, "DiscreteCustom", {}, [=] { return mc::makeDiscreteCustom(v, p); },
                     lower, upper, count);
}

double MonteCalcAddIn::tsInput(const OUString& name, const char* label, mc::TsType type,
                               std::initializer_list<double> params, const Any& periods) {
    return guard([&] {
        mc::TimeSeries ts{type, std::vector<double>(params)};
        ts.validate();
        int n = int(std::lround(optNum(periods, 1)));
        if (n < 1 || n > 1000000) throw std::invalid_argument("Periods must be between 1 and 1,000,000");
        std::string desc = describe(label, params) + " periods=" + std::to_string(n);
        return mc::Simulation::instance().input(
            u8(name), desc, ts.expected(n),
            [ts, n](double u, std::mt19937_64& rng) { return ts.simulate(u, n, rng); });
    });
}

double MonteCalcAddIn::withSamples(const OUString& name,
                                   const std::function<double(const std::vector<double>&)>& fn) {
    return guard([&] {
        auto& sim = mc::Simulation::instance();
        if (sim.busy() || !sim.hasResults()) return -1.0;
        std::vector<double> v;
        if (!sim.samples(u8(name), v)) throw std::invalid_argument("unknown variable: " + u8(name));
        double r = fn(v);
        if (!std::isfinite(r)) throw std::invalid_argument("result is not defined for this variable");
        return r;
    });
}

double MonteCalcAddIn::withPair(const OUString& n1, const OUString& n2,
                                const std::function<double(const std::vector<double>&,
                                                           const std::vector<double>&)>& fn) {
    return guard([&] {
        auto& sim = mc::Simulation::instance();
        if (sim.busy() || !sim.hasResults()) return -1.0;
        std::vector<double> a, b;
        if (!sim.samples(u8(n1), a)) throw std::invalid_argument("unknown variable: " + u8(n1));
        if (!sim.samples(u8(n2), b)) throw std::invalid_argument("unknown variable: " + u8(n2));
        double r = fn(a, b);
        if (!std::isfinite(r)) throw std::invalid_argument("result is not defined for these variables");
        return r;
    });
}

OUString MonteCalcAddIn::bootstrapText(const OUString& name, const AnyMatrix* data, double samples,
                                       double confidence, const Any& p) {
    return guard([&] {
        std::vector<double> v;
        if (data) {
            v = numbers(*data);
        } else {
            auto& sim = mc::Simulation::instance();
            if (sim.busy() || !sim.hasResults()) return OUString("-1");
            if (!sim.samples(u8(name), v)) throw std::invalid_argument("unknown variable: " + u8(name));
        }
        if (!(confidence > 0 && confidence < 1)) throw std::invalid_argument("Confidence must be in (0,1)");
        int ns = int(std::lround(samples));
        if (ns < 2 || ns > 1000000) throw std::invalid_argument("Samples must be between 2 and 1,000,000");
        std::mt19937_64 rng(std::random_device{}());
        auto r = mc::stats::bootstrap(v, ns, confidence, optNum(p, 0), rng);
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.10g; %.10g; %.10g", r.estimate, r.lower, r.upper);
        return ou(buf);
    });
}

namespace {
std::vector<std::pair<std::string, double>> drivers(const std::string& output) {
    auto& sim = mc::Simulation::instance();
    std::vector<double> out, in;
    if (!sim.samples(output, out)) throw std::invalid_argument("unknown variable: " + output);
    std::vector<std::pair<std::string, double>> d;
    for (const auto& n : sim.resultNames(mc::VarKind::Input)) {
        sim.samples(n, in);
        double r = mc::stats::spearman(in, out);
        d.push_back({n, std::isfinite(r) ? r : 0.0});
    }
    std::stable_sort(d.begin(), d.end(), [](auto& a, auto& b) { return std::fabs(a.second) > std::fabs(b.second); });
    return d;
}
} // namespace

OUString MonteCalcAddIn::topDriverName(const OUString& output, double rank) {
    return guard([&] {
        if (mc::Simulation::instance().busy() || !mc::Simulation::instance().hasResults()) return OUString("-1");
        auto d = drivers(u8(output));
        long i = std::lround(rank);
        if (i < 1 || size_t(i) > d.size()) throw std::invalid_argument("rank out of range");
        return ou(d[i - 1].first);
    });
}

double MonteCalcAddIn::topDriverCorr(const OUString& output, double rank) {
    return guard([&] {
        if (mc::Simulation::instance().busy() || !mc::Simulation::instance().hasResults()) return -1.0;
        auto d = drivers(u8(output));
        long i = std::lround(rank);
        if (i < 1 || size_t(i) > d.size()) throw std::invalid_argument("rank out of range");
        return d[i - 1].second;
    });
}

OUString MonteCalcAddIn::variableName(mc::VarKind kind, double pos) {
    return guard([&] {
        auto n = mc::Simulation::instance().resultNames(kind);
        if (n.empty()) return OUString("-1");
        long i = std::lround(pos);
        if (i < 1 || size_t(i) > n.size()) throw std::invalid_argument("position out of range");
        return ou(n[i - 1]);
    });
}

DoubleMatrix MonteCalcAddIn::samplesMatrix(const OUString& name) {
    return guard([&] {
        std::vector<double> v;
        auto& sim = mc::Simulation::instance();
        if (sim.busy() || !sim.hasResults()) return seq<Sequence<double>>({seq<double>({-1.0})});
        if (!sim.samples(u8(name), v)) throw std::invalid_argument("unknown variable: " + u8(name));
        DoubleMatrix m(sal_Int32(v.size()));
        auto* rows = m.getArray();
        for (size_t i = 0; i < v.size(); ++i) rows[i] = seq<double>({v[i]});
        return m;
    });
}

DoubleMatrix MonteCalcAddIn::histogramMatrix(const OUString& name, const Any& bins) {
    return guard([&] {
        std::vector<double> v;
        auto& sim = mc::Simulation::instance();
        if (sim.busy() || !sim.hasResults()) return seq<Sequence<double>>({seq<double>({-1.0})});
        if (!sim.samples(u8(name), v)) throw std::invalid_argument("unknown variable: " + u8(name));
        int nb = int(optNum(bins, binsDefault()));
        if (nb < 1 || nb > 10000) throw std::invalid_argument("Bins must be between 1 and 10000");
        auto h = mc::stats::histogram(v, nb);
        double total = 0, cum = 0;
        for (double c : h.count) total += c;
        DoubleMatrix m(nb);
        auto* rows = m.getArray();
        for (int i = 0; i < nb; ++i) {
            cum += h.count[i];
            rows[i] = seq<double>({h.lo[i], h.hi[i], h.count[i], h.count[i] / total, cum / total});
        }
        return m;
    });
}

double MonteCalcAddIn::sortValue(const AnyMatrix& data, double pos, const Any& desc) {
    return guard([&] {
        auto s = mc::stats::sorted(numbers(data));
        if (optNum(desc, 0) != 0) std::reverse(s.begin(), s.end());
        long i = std::lround(pos);
        if (i < 1 || size_t(i) > s.size()) throw std::invalid_argument("position out of range");
        return s[i - 1];
    });
}

DoubleMatrix MonteCalcAddIn::benfordMatrix(const AnyMatrix& data) {
    return guard([&] {
        std::vector<double> cnt(10, 0.0);
        double n = 0;
        for (double v : numbers(data)) {
            v = std::fabs(v);
            if (!(v > 0) || !std::isfinite(v)) continue;
            while (v >= 10) v /= 10;
            while (v < 1) v *= 10;
            int d = std::min(9, int(v));
            cnt[d] += 1;
            n += 1;
        }
        if (n < 1) throw std::invalid_argument("no positive numbers in range");
        DoubleMatrix m(10);
        auto* rows = m.getArray();
        double chi = 0;
        for (int d = 1; d <= 9; ++d) {
            double e = std::log10(1 + 1.0 / d);
            chi += (cnt[d] - n * e) * (cnt[d] - n * e) / (n * e);
            rows[d - 1] = seq<double>({double(d), cnt[d] / n, e});
        }
        double pval = 1 - mc::chi2Cdf(chi, 8);
        rows[9] = seq<double>({chi, pval, pval >= 0.05 ? 1.0 : 0.0});
        return m;
    });
}

// ============================== generated function bodies ================
#include "functions_impl.inc"

// ============================== XAddIn ===================================

OUString MonteCalcAddIn::getProgrammaticFuntionName(const OUString& aDisplayName) {
    std::string d = u8(aDisplayName);
    for (const auto& f : functionTable())
        if (d == f.display) return ou(f.prog);
    return OUString();
}

OUString MonteCalcAddIn::getDisplayFunctionName(const OUString& prog) {
    const FuncInfo* f = findProg(prog);
    return f ? ou(f->display) : OUString();
}

OUString MonteCalcAddIn::getFunctionDescription(const OUString& prog) {
    const FuncInfo* f = findProg(prog);
    return f ? ou(f->desc) : OUString();
}

OUString MonteCalcAddIn::getDisplayArgumentName(const OUString& prog, sal_Int32 n) {
    const FuncInfo* f = findProg(prog);
    if (!f || n < 0 || size_t(n) >= f->args.size()) return OUString();
    return ou(f->args[n].name);
}

OUString MonteCalcAddIn::getArgumentDescription(const OUString& prog, sal_Int32 n) {
    const FuncInfo* f = findProg(prog);
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

} // namespace montecalc
