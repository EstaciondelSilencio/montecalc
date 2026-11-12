// MonteCalc - the iteration loop of a simulation (see runner.h)
#include "runner.h"

#include "simulation.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace mc {

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kIdleEvery = std::chrono::milliseconds(50);

// while paused: keep the windows alive
void waitWhilePaused(RunControl& ctl, const std::function<void()>& idle) {
    while (ctl.pause && !ctl.stop) {
        if (idle) idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
} // namespace

int defaultThreads() {
    unsigned n = std::thread::hardware_concurrency();
    return int(std::max(1u, std::min(16u, n)));
}

bool runSequential(int first, int n, const IterationFn& eval, RunControl& ctl, const std::function<void()>& idle) {
    auto& sim = Simulation::instance();
    ctl.threads = 1;
    auto last = Clock::now();
    for (int i = first; i < n; ++i) {
        if (ctl.pause) waitWhilePaused(ctl, idle);
        if (ctl.stop) return false;
        sim.setIteration(i);
        eval(i);
        sim.completeIteration(i);
        if (idle && Clock::now() - last >= kIdleEvery) {
            idle();
            last = Clock::now();
        }
    }
    if (idle) idle();
    return !ctl.stop;
}

bool runParallel(int first, int n, int threads, const std::function<IterationFn()>& makeWorker, RunControl& ctl,
                 const std::function<void()>& idle) {
    auto& sim = Simulation::instance();
    // one thread too runs as a worker: the results do not depend on the thread count
    threads = std::max(1, std::min(threads, std::max(1, n - first)));
    ctl.threads = threads;
    std::vector<IterationFn> evals;
    for (int t = 0; t < threads; ++t) evals.push_back(makeWorker());
    std::atomic<int> next{first};
    std::atomic<int> running{threads};
    std::exception_ptr error;
    std::mutex errMtx;
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            try {
                for (;;) {
                    while (ctl.pause && !ctl.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    if (ctl.stop) break;
                    int i = next++;
                    if (i >= n) break;
                    sim.beginWorkerIteration(i);
                    try {
                        evals[size_t(t)](i);
                    } catch (...) {
                        sim.endWorkerIteration();
                        throw;
                    }
                    sim.endWorkerIteration();
                    sim.completeIteration(i);
                }
            } catch (...) {
                std::lock_guard<std::mutex> g(errMtx);
                if (!error) error = std::current_exception();
                ctl.stop = true;
            }
            --running;
        });
    }
    // the calling thread: user interface (preview window, buttons, progress)
    while (running > 0) {
        if (idle) idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    for (auto& th : pool) th.join();
    if (idle) idle();
    if (error) std::rethrow_exception(error);
    return !ctl.stop;
}

} // namespace mc
