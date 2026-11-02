// MonteCalc - simulation state
#include "simulation.h"
#include "copula.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mc {

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    // trim
    size_t a = r.find_first_not_of(" \t"), b = r.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : r.substr(a, b - a + 1);
}

double compoundSample(const Distribution& d, const Bounds& b, double u, int count,
                      std::mt19937_64& rng) {
    double v = sampleBounded(d, u, b);
    if (count > 1) {
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        for (int i = 1; i < count; ++i) v += sampleBounded(d, uni(rng), b);
    }
    return v;
}

Simulation& Simulation::instance() {
    static Simulation s;
    return s;
}

int Simulation::indexOf(const std::string& name) const {
    auto it = index_.find(lower(name));
    return it == index_.end() ? -1 : it->second;
}

int Simulation::ensureVariable(const std::string& name, VarKind kind, const std::string& desc) {
    int i = indexOf(name);
    if (i >= 0) return i;
    Variable v;
    v.name = name;
    v.kind = kind;
    v.description = desc;
    vars_.push_back(v);
    i = int(vars_.size()) - 1;
    index_[lower(name)] = i;
    if (mode_ == Mode::Simulate) {
        // variable appeared mid-run (e.g. inside an IF branch): independent uniforms
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        std::vector<double> u(iterations_);
        for (auto& x : u) x = uni(rng_);
        uniforms_.push_back(u);
        results_.push_back(std::vector<double>(iterations_, kNaN));
    }
    return i;
}

double Simulation::input(const std::string& name, const std::string& desc, double expected,
                         const Sampler& sampler) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (lower(name).empty()) throw std::invalid_argument("variable name must not be empty");
    switch (mode_) {
    case Mode::Expected:
        return expected;
    case Mode::Register: {
        int i = ensureVariable(name, VarKind::Input, desc);
        if (++vars_[i].callsThisPass > 1) duplicates_.insert(vars_[i].name);
        vars_[i].description = desc;
        return expected;
    }
    case Mode::Random: {
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        return sampler(uni(rng_), rng_);
    }
    case Mode::Replay: {
        auto it = resultIndex_.find(lower(name));
        if (it != resultIndex_.end() && replay_ < iterations_) {
            double v = results_[it->second][replay_];
            if (std::isfinite(v)) return v;
        }
        return expected;
    }
    case Mode::Simulate: {
        int i = ensureVariable(name, VarKind::Input, desc);
        double v;
        if (locked_.count(lower(name))) {
            v = expected;
        } else {
            v = sampler(uniforms_[i][iteration_], rng_);
        }
        results_[i][iteration_] = v;
        return v;
    }
    }
    return expected;
}

double Simulation::output(const std::string& name, double value) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (lower(name).empty()) throw std::invalid_argument("variable name must not be empty");
    if (mode_ == Mode::Register) {
        int i = ensureVariable(name, VarKind::Output, "Output");
        if (++vars_[i].callsThisPass > 1) duplicates_.insert(vars_[i].name);
    } else if (mode_ == Mode::Simulate) {
        int i = ensureVariable(name, VarKind::Output, "Output");
        results_[i][iteration_] = value;
    }
    return value;
}

double Simulation::correlate(const std::string& a, const std::string& b, double rho, int type) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (!(rho > -1 && rho < 1)) throw std::invalid_argument("correlation must be between -1 and 1");
    if (type < 0 || type > 5) throw std::invalid_argument("correlation type must be 0..5");
    if (mode_ == Mode::Register) corr_.push_back({a, b, rho, type});
    return rho;
}

void Simulation::beginRegistration() {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    vars_.clear();
    index_.clear();
    corr_.clear();
    duplicates_.clear();
    mode_ = Mode::Register;
}

std::vector<std::string> Simulation::prepare(const Settings& s) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (s.iterations < 1) throw std::invalid_argument("number of iterations must be at least 1");
    settings_ = s;
    iterations_ = s.iterations;
    rng_.seed(s.seed != 0 ? (unsigned long long)s.seed : std::random_device{}());
    std::vector<std::string> warn;
    for (const auto& d : duplicates_)
        warn.push_back("variable name '" + d + "' is used in more than one cell; only the last value is kept");

    std::vector<CorrelationSpec> specs;
    for (const auto& c : corr_) {
        int a = indexOf(c.a), b = indexOf(c.b);
        if (a < 0 || b < 0) {
            warn.push_back("correlation " + c.a + " / " + c.b + ": unknown variable name, ignored");
            continue;
        }
        if (vars_[a].kind != VarKind::Input || vars_[b].kind != VarKind::Input) {
            warn.push_back("correlation " + c.a + " / " + c.b + ": only inputs can be correlated, ignored");
            continue;
        }
        specs.push_back({a, b, c.rho, c.type});
    }
    CopulaResult cr = generateUniforms(int(vars_.size()), iterations_, specs, rng_);
    for (auto& w : cr.warnings) warn.push_back(w);
    uniforms_ = std::move(cr.u);
    results_.assign(vars_.size(), std::vector<double>(iterations_, kNaN));
    for (auto& v : vars_) v.callsThisPass = 0;
    hasResults_ = false;
    iteration_ = 0;
    mode_ = Mode::Simulate;
    warnings_ = warn;
    return warn;
}

void Simulation::setIteration(int i) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    iteration_ = i;
}

void Simulation::finish() {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    mode_ = Mode::Expected;
    hasResults_ = true;
    resultVars_ = vars_;
    resultIndex_ = index_;
}

void Simulation::abort() {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    mode_ = Mode::Expected;
    results_.clear();
    uniforms_.clear();
    hasResults_ = false;
}

void Simulation::reset() {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    results_.clear();
    uniforms_.clear();
    resultVars_.clear();
    resultIndex_.clear();
    hasResults_ = false;
    iterations_ = 0;
    if (mode_ == Mode::Replay) mode_ = Mode::Expected;
}

void Simulation::setMode(Mode m) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    mode_ = m;
}

Mode Simulation::mode() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return mode_;
}

bool Simulation::busy() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return mode_ == Mode::Register || mode_ == Mode::Simulate;
}

void Simulation::setReplayIteration(int i) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (!hasResults_) throw std::runtime_error("no simulation results available");
    if (i < 0 || i >= iterations_) throw std::invalid_argument("iteration out of range");
    replay_ = i;
    mode_ = Mode::Replay;
}

void Simulation::toggleLock(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    std::string k = lower(name);
    if (locked_.count(k)) locked_.erase(k); else locked_.insert(k);
}

bool Simulation::isLocked(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return locked_.count(lower(name)) > 0;
}

std::vector<Variable> Simulation::variables() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return vars_;
}

std::vector<CorrelationRef> Simulation::correlations() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return corr_;
}

std::vector<std::string> Simulation::duplicateNames() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return {duplicates_.begin(), duplicates_.end()};
}

bool Simulation::hasResults() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return hasResults_;
}

int Simulation::iterations() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return hasResults_ ? iterations_ : 0;
}

Settings Simulation::settings() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return settings_;
}

void Simulation::setSettings(const Settings& s) {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    settings_ = s;
}

std::vector<std::string> Simulation::resultNames(VarKind kind) const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    std::vector<std::string> r;
    if (!hasResults_) return r;
    for (const auto& v : resultVars_)
        if (v.kind == kind) r.push_back(v.name);
    return r;
}

bool Simulation::resultKind(const std::string& name, VarKind& kind) const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    auto it = resultIndex_.find(lower(name));
    if (!hasResults_ || it == resultIndex_.end()) return false;
    kind = resultVars_[it->second].kind;
    return true;
}

bool Simulation::samples(const std::string& name, std::vector<double>& out) const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    if (!hasResults_) return false;
    auto it = resultIndex_.find(lower(name));
    if (it == resultIndex_.end()) return false;
    out = results_[it->second];
    return true;
}

std::vector<std::string> Simulation::lastWarnings() const {
    std::lock_guard<std::recursive_mutex> g(mtx_);
    return warnings_;
}

} // namespace mc
