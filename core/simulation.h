// MonteCalc - simulation state shared by the spreadsheet functions and the engine.
//
// How a run works:
//   1. Register: the engine recalculates the document once. Every input,
//      output and correlation function call registers itself here by name.
//   2. Prepare:  correlated uniforms are generated for all inputs.
//   3. Simulate: the engine recalculates the document once per iteration.
//      Input functions turn "their" uniform for the current iteration into a
//      value of their distribution (inverse CDF) and record it; output
//      functions record the value they receive.
//   4. Finish:   the document goes back to showing expected values and the
//      recorded samples are available to the result functions and reports.
#pragma once

#include "distributions.h"

#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace mc {

enum class Mode {
    Expected,   // cells show the expected value of each input (default)
    Register,   // registration recalculation
    Simulate,   // inside a simulation run
    Replay,     // cells show the values of one stored iteration
    Random      // live preview: every recalculation draws new random values
};

enum class VarKind { Input, Output };

struct Variable {
    std::string name;
    VarKind kind = VarKind::Input;
    std::string description;   // e.g. "Triangular(1; 2; 3)"
    int callsThisPass = 0;     // to detect duplicate names
};

struct Settings {
    int iterations = 1000;
    long long seed = 0;        // 0 = random seed
    double worst = 0.05;       // scenario quantiles
    double real = 0.50;
    double best = 0.95;
    int bins = 20;             // default histogram bins
};

struct CorrelationRef {
    std::string a, b;
    double rho = 0;
    int type = 0;
};

// A sampler turns a uniform number into a value (inverse CDF), possibly using
// the generator for additional independent draws (compound sums, time series).
using Sampler = std::function<double(double u, std::mt19937_64& rng)>;

class Simulation {
public:
    static Simulation& instance();

    // ---- called by the spreadsheet functions ----
    double input(const std::string& name, const std::string& description, double expected,
                 const Sampler& sampler);
    double output(const std::string& name, double value);
    double correlate(const std::string& a, const std::string& b, double rho, int type);

    // ---- called by the engine ----
    void beginRegistration();
    // Returns warnings. Throws std::exception on fatal problems.
    std::vector<std::string> prepare(const Settings& s);
    void setIteration(int i);
    void finish();
    void abort();
    void reset();                         // forget results
    void setMode(Mode m);
    Mode mode() const;
    void setReplayIteration(int i);       // switch to Replay mode
    void toggleLock(const std::string& name);
    bool isLocked(const std::string& name) const;

    // ---- registration info ----
    std::vector<Variable> variables() const;     // registration order
    std::vector<CorrelationRef> correlations() const;
    std::vector<std::string> duplicateNames() const;

    // ---- results ----
    bool hasResults() const;
    int iterations() const;
    Settings settings() const;
    void setSettings(const Settings& s);
    std::vector<std::string> resultNames(VarKind kind) const;
    bool resultKind(const std::string& name, VarKind& kind) const;
    // Copy of the samples of a variable (case-insensitive name lookup).
    // Returns false if unknown.
    bool samples(const std::string& name, std::vector<double>& out) const;
    std::vector<std::string> lastWarnings() const;

private:
    Simulation() = default;
    int indexOf(const std::string& name) const;
    int ensureVariable(const std::string& name, VarKind kind, const std::string& desc);

    mutable std::recursive_mutex mtx_;
    Mode mode_ = Mode::Expected;
    Settings settings_;
    std::vector<Variable> vars_;
    std::map<std::string, int> index_;   // lower-cased name -> index
    std::vector<CorrelationRef> corr_;
    std::set<std::string> locked_;       // lower-cased
    std::set<std::string> duplicates_;
    std::vector<std::vector<double>> uniforms_;  // [var][iteration]
    std::vector<std::vector<double>> results_;   // [var][iteration]
    std::mt19937_64 rng_{std::random_device{}()};
    int iteration_ = 0;
    int replay_ = 0;
    int iterations_ = 0;
    bool hasResults_ = false;
    std::vector<std::string> warnings_;
    // snapshot of the variables that belong to the stored results
    std::vector<Variable> resultVars_;
    std::map<std::string, int> resultIndex_;
};

std::string lower(const std::string& s);
// Draw `count` values (first one from u, the rest independent) and sum them.
double compoundSample(const Distribution& d, const Bounds& b, double u, int count,
                      std::mt19937_64& rng);

} // namespace mc
