// MonteCalc - the iteration loop of a simulation: pause / stop, and Memory Calculation on
// several threads.
//
// The spreadsheet programs must recalculate on their main thread, so a run that recalculates
// the spreadsheet evaluates the iterations in order on the calling thread. A Memory
// Calculation model is evaluated in memory: every worker thread gets its own copy of the
// model and takes the next free iteration. In both cases the calling (user interface) thread
// calls `idle` regularly, so windows stay responsive (preview window, pause and stop buttons).
#pragma once

#include <atomic>
#include <functional>

namespace mc {

// shared between the engine and the preview window
struct RunControl {
    std::atomic<bool> pause{false};
    std::atomic<bool> stop{false};
    std::atomic<int> threads{1};   // threads in use (shown in the preview window)
};

// evaluates and records iteration i on the calling thread
using IterationFn = std::function<void(int i)>;

// Iterations [first, n) in order on the calling thread. Returns false when stopped.
bool runSequential(int first, int n, const IterationFn& eval, RunControl& ctl, const std::function<void()>& idle);

// Iterations [first, n) on `threads` worker threads; makeWorker() is called once per worker on
// the calling thread and returns that worker's evaluator. Returns false when stopped. An
// exception of a worker stops the others and is rethrown here.
bool runParallel(int first, int n, int threads, const std::function<IterationFn()>& makeWorker, RunControl& ctl,
                 const std::function<void()>& idle);

// worker threads for Memory Calculation: the processor cores, at most 16
int defaultThreads();

} // namespace mc
