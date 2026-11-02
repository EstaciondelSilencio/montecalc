// MonteCalc - simulation state shared by the spreadsheet functions and the engine.
//
// How a run works (mirrors the MC FLO workflow):
//   1. Register: the engine recalculates the document once. Every named input,
//      output and correlation function call registers itself here by name. The
//      engine also scans the cells for FLOsimula_ formulas (that is how unnamed
//      inputs and the output cells are found).
//   2. Prepare:  correlated uniforms are generated for all named inputs.
//   3. Simulate: the engine recalculates the document once per iteration (or
//      evaluates it in memory). Named inputs turn "their" uniform for the
//      current iteration into a value of their distribution; unnamed inputs draw
//      independently. After each iteration the engine records the values of all
//      input and output cells.
//   4. Finish:   the document shows expected values again; the samples are kept
//      as a result set (one per variant when a multiple simulation is run).
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
    Replay,     // cells show the values of one stored iteration (named inputs)
    Random      // preview: every recalculation draws new random values (seed -1)
};

enum class VarKind { Input, Output, Correlation };

struct Variable {
    std::string name;
    VarKind kind = VarKind::Input;
    std::string description;   // e.g. "Triangle(1; 2; 3)"
    std::string cell;          // e.g. "Model.B4" (set by the engine)
    bool userDefined = false;  // FLOsimula_UserDefined
    int callsThisPass = 0;     // to detect duplicate names
};

struct Settings {
    int iterations = 1000;
    long long seed = 0;        // 0 = random seed, -1 = preview mode
    double worst = 0.05;       // scenario quantiles
    double real = 0.50;
    double best = 0.95;
    int bins = 20;             // default histogram bins (fMC_Bin)
};

struct CorrelationRef {
    std::string a, b;
    double rho = 0;
    int type = 0;
};

// A sampler turns a uniform number into a value (inverse CDF), possibly using
// the generator for additional independent draws (compound sums, time series).
using Sampler = std::function<double(double u, std::mt19937_64& rng)>;

struct ResultSet {
    std::string label;               // variant name ("" for a single run)
    std::vector<Variable> vars;      // inputs first (registration order), then outputs
    std::map<std::string, int> index;
    std::vector<std::vector<double>> data;  // [var][iteration]
    int iterations = 0;
};

class Simulation {
public:
    static Simulation& instance();

    // ---- called by the spreadsheet functions ----
    // name may be empty (unnamed input: independent draws, recorded by the engine)
    double input(const std::string& name, const std::string& description, double expected,
                 const Sampler& sampler, bool userDefined = false);
    double output(const std::string& name, double value, bool hasValue);
    double correlate(const std::string& a, const std::string& b, double rho, int type);
    std::mt19937_64& rng() { return rng_; }

    // ---- called by the engine ----
    void beginRegistration();
    // Returns warnings. Throws std::exception on fatal problems.
    std::vector<std::string> prepare(const Settings& s);
    void setIteration(int i);
    // Record a value for the current iteration (creates the variable if needed).
    void record(const std::string& name, VarKind kind, double value);
    int variableIndex(const std::string& name) const { std::lock_guard<std::recursive_mutex> g(mtx_); return indexOf(name); }
    void recordAt(int index, double value);
    void setVariableInfo(const std::string& name, VarKind kind, const std::string& desc, const std::string& cell);
    void clearResultSets();
    void finish(const std::string& label = "");
    void abort();
    void reset();                         // forget results
    void setMode(Mode m);
    Mode mode() const;
    bool busy() const;                    // registering or simulating
    void setReplayIteration(int i);       // switch to Replay mode
    void toggleLock(const std::string& name);
    bool isLocked(const std::string& name) const;
    std::vector<std::string> lockedNames() const;

    // ---- registration info ----
    std::vector<Variable> variables() const;     // registration order
    std::vector<CorrelationRef> correlations() const;
    std::vector<std::string> duplicateNames() const;

    // ---- results (of the active result set) ----
    bool hasResults() const;
    int iterations() const;
    Settings settings() const;
    void setSettings(const Settings& s);
    std::vector<std::string> resultNames(VarKind kind) const;
    std::vector<Variable> resultVariables() const;
    bool resultKind(const std::string& name, VarKind& kind) const;
    bool samples(const std::string& name, std::vector<double>& out) const;
    std::vector<std::string> lastWarnings() const;
    std::vector<CorrelationRef> resultCorrelations() const;

    // ---- variants (multiple simulation) ----
    std::vector<std::string> resultSetLabels() const;
    int activeResultSet() const;
    void selectResultSet(int i);
    bool samplesIn(int set, const std::string& name, std::vector<double>& out) const;

private:
    Simulation() = default;
    int indexOf(const std::string& name) const;
    int ensureVariable(const std::string& name, VarKind kind, const std::string& desc);
    const ResultSet* active() const;

    mutable std::recursive_mutex mtx_;
    Mode mode_ = Mode::Expected;
    Settings settings_;
    std::vector<Variable> vars_;
    std::map<std::string, int> index_;   // lower-cased name -> index
    std::vector<CorrelationRef> corr_, resultCorr_;
    std::set<std::string> locked_;       // lower-cased
    std::set<std::string> duplicates_;
    std::vector<std::vector<double>> uniforms_;  // [var][iteration]
    std::vector<std::vector<double>> work_;      // [var][iteration] of the running simulation
    std::mt19937_64 rng_{std::random_device{}()};
    int iteration_ = 0;
    int replay_ = 0;
    int iterations_ = 0;
    std::vector<ResultSet> sets_;
    int active_ = -1;
    std::vector<std::string> warnings_;
};

std::string lower(const std::string& s);
// Draw `count` values (first one from u, the rest independent) and sum them.
double compoundSample(const Distribution& d, const Bounds& b, double u, int count,
                      std::mt19937_64& rng);

} // namespace mc
