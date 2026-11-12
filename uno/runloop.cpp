// MonteCalc - the iterations of a run and its summary, shared by the LibreOffice extension and
// the Excel add-in (no user interface here: the Excel test program on Linux links it too)
#include "engine.hpp"

#include "../core/stats.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace montecalc {

bool runIterations(int n, const mc::formula::Model* mem, const std::vector<mc::formula::CellAddr>& addr, const std::vector<int>& index,
                   const mc::IterationFn& recalc, int threads, LiveRun& live) {
    auto& sim = mc::Simulation::instance();
    if (!mem) return mc::runSequential(0, n, recalc, live.ctl, live.idle);   // the spreadsheet recalculates: its own thread
    auto evalWith = [&addr, &index, &sim](std::shared_ptr<mc::formula::Model> m) -> mc::IterationFn {
        return [m, &addr, &index, &sim](int) {
            m->beginIteration();
            for (size_t k = 0; k < addr.size(); ++k) {
                mc::Value v = m->value(addr[k]);
                sim.recordAt(index[k], v.kind == mc::Value::Number || v.kind == mc::Value::Bool ? v.num : std::nan(""));
            }
        };
    };
    auto first = std::make_shared<mc::formula::Model>(*mem);
    if (!mc::runSequential(0, std::min(1, n), evalWith(first), live.ctl, live.idle)) return false;
    if (n <= 1) return true;
    // time series continue from one iteration to the next: in order, on this thread
    if (sim.usesSeriesState()) return mc::runSequential(1, n, evalWith(first), live.ctl, live.idle);
    int t = threads > 0 ? threads : mc::defaultThreads();
    return mc::runParallel(1, n, t, [&] { return evalWith(std::make_shared<mc::formula::Model>(*first)); }, live.ctl, live.idle);
}

std::string runSummary(const RunOptions& o, const RunResult& rr) {
    auto& sim = mc::Simulation::instance();
    std::ostringstream os;
    auto labels = sim.resultSetLabels();
    std::string secs = fmtNum(std::round(rr.seconds * 100) / 100);
    if (rr.stopped)   // the Stop button: the completed iterations are the result
        os << "Simulation stopped after " << sim.iterations() << " of " << o.s.iterations << " iterations (" << secs << " s)\n";
    else
        os << "Simulation finished: " << o.s.iterations << " iterations" << (labels.size() > 1 ? " x " + std::to_string(labels.size()) + " variants" : "")
           << " in " << secs << " s" << (rr.memoryUsed ? " (memory calculation)" : "") << "\n";
    if (rr.memoryUsed && rr.threads > 1) os << "Memory Calculation on " << rr.threads << " threads\n";
    os << "\n";
    int active = sim.activeResultSet();
    for (size_t vi = 0; vi < labels.size(); ++vi) {
        if (labels.size() > 1) os << "Variant " << labels[vi] << ":\n";
        sim.selectResultSet(int(vi));
        for (const auto& name : sim.resultNames(mc::VarKind::Output)) {
            std::vector<double> v;
            sim.samples(name, v);
            os << "  " << name << ": mean " << fmtNum(mc::stats::mean(v)) << ", std dev " << fmtNum(mc::stats::stdev(v))
               << ", worst " << fmtNum(mc::stats::percentile(v, o.s.worst)) << ", real " << fmtNum(mc::stats::percentile(v, o.s.real))
               << ", best " << fmtNum(mc::stats::percentile(v, o.s.best)) << "\n";
        }
    }
    if (active >= 0) sim.selectResultSet(active);
    if (!rr.warnings.empty()) {
        os << "\nWarnings:\n";
        for (auto& w : rr.warnings) os << " - " << w << "\n";
    }
    return os.str();
}

} // namespace montecalc
