// Threading backbone. Dwarfkit calls are blocking by design, so every chain
// interaction runs on a worker; the UI thread only ever drains completions.
//
//   runner.run([]{ heavy work }, [](){ apply result on main });
//
// The UI loop calls drainMain() once per frame. PromptBroker lets a worker
// block on a decision the user makes in the UI (dwarfkit's UserInterface
// prompt contract), with cancellation.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tb {

class TaskRunner {
public:
    explicit TaskRunner(unsigned workers = 0);  // 0 = conservativeWorkers()
    ~TaskRunner();

    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Pool sizing for the multicore toggle: every core (minus one for the UI
    // thread) versus the historical small pool.
    static unsigned autoWorkers();          // clamp(hardware_concurrency - 1, 2, 16)
    static unsigned conservativeWorkers();  // 2

    // Resize the pool at runtime (clamped to [1, 32]). Growing spawns
    // immediately; shrinking retires workers as they finish their current
    // job - never blocks the calling thread on in-flight work.
    void setWorkers(unsigned target);
    unsigned workers() const;  // current target

    // Queue work on the pool.
    void run(std::function<void()> work);

    // Queue work on the pool, then a completion on the UI thread.
    void run(std::function<void()> work, std::function<void()> onMain);

    // Queue a callback for the UI thread directly (safe from any thread).
    void postMain(std::function<void()> fn);

    // UI thread only: run queued main-thread callbacks. Returns how many ran.
    size_t drainMain();

    // Number of jobs queued or executing on the pool (for busy indicators).
    int pending() const { return pending_.load(); }

private:
    // One pool thread. Retiring is cooperative: the loop exits after its
    // current job once `quit` is set; the thread is joined lazily.
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> quit;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void workerLoop(std::shared_ptr<std::atomic<bool>> quit,
                    std::shared_ptr<std::atomic<bool>> done);
    void spawnLocked(unsigned count);
    void reapLocked();  // join retired workers that have finished

    mutable std::mutex poolMutex_;
    std::vector<Worker> workers_;
    std::vector<Worker> retired_;
    unsigned target_ = 0;

    std::deque<std::function<void()>> work_;
    std::deque<std::function<void()>> main_;
    std::mutex workMutex_;
    std::mutex mainMutex_;
    std::condition_variable workCv_;
    std::atomic<bool> stop_{false};
    std::atomic<int> pending_{0};
};

// One pending question from a worker to the human. The worker blocks in
// wait(); the UI resolves or cancels it. Payload shapes are defined by the
// asker (see app/bridge.cpp and the signing flow).
class PromptBroker {
public:
    struct Pending {
        uint64_t id = 0;
        std::string kind;          // "sign", "plugin", ...
        std::shared_ptr<void> payload;
    };

    // Worker side: publish a request and block until the UI answers.
    // Returns false when cancelled (app shutdown, user reject is an answer).
    bool wait(Pending request, std::function<void()> onPosted = nullptr);

    // UI side: current open request, if any.
    std::optional<Pending> current();

    // UI side: wake the worker. answered=false means cancelled.
    void resolve(uint64_t id, bool answered);

    // Shutdown: cancel anything outstanding so workers can exit.
    void cancelAll();

    uint64_t nextId() { return ++idCounter_; }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Pending> current_;
    uint64_t resolvedId_ = 0;
    bool resolvedAnswered_ = false;
    std::atomic<uint64_t> idCounter_{0};
    bool cancelAll_ = false;
};

}  // namespace tb
