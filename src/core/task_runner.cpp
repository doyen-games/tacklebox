#include "core/task_runner.hpp"

#include "core/log.hpp"

namespace tb {

unsigned TaskRunner::autoWorkers() {
    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    unsigned n = cores > 1 ? cores - 1 : 1;  // leave a core for the UI thread
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    return n;
}

unsigned TaskRunner::conservativeWorkers() { return 2; }

TaskRunner::TaskRunner(unsigned workers) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    target_ = workers ? workers : conservativeWorkers();
    spawnLocked(target_);
}

TaskRunner::~TaskRunner() {
    stop_.store(true);
    workCv_.notify_all();
    std::lock_guard<std::mutex> lock(poolMutex_);
    for (auto& worker : workers_)
        if (worker.thread.joinable()) worker.thread.join();
    for (auto& worker : retired_)
        if (worker.thread.joinable()) worker.thread.join();
}

void TaskRunner::spawnLocked(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        Worker worker;
        worker.quit = std::make_shared<std::atomic<bool>>(false);
        worker.done = std::make_shared<std::atomic<bool>>(false);
        worker.thread =
            std::thread([this, quit = worker.quit, done = worker.done] {
                workerLoop(quit, done);
            });
        workers_.push_back(std::move(worker));
    }
}

void TaskRunner::reapLocked() {
    std::erase_if(retired_, [](Worker& worker) {
        if (!worker.done->load()) return false;
        if (worker.thread.joinable()) worker.thread.join();
        return true;
    });
}

void TaskRunner::setWorkers(unsigned target) {
    if (target < 1) target = 1;
    if (target > 32) target = 32;
    std::lock_guard<std::mutex> lock(poolMutex_);
    reapLocked();
    if (target == target_) return;
    Log::info("worker pool: %u -> %u threads", target_, target);
    if (target > target_) {
        spawnLocked(target - target_);
    } else {
        for (unsigned i = target; i < target_; ++i) {
            Worker& worker = workers_.back();
            worker.quit->store(true);
            retired_.push_back(std::move(worker));
            workers_.pop_back();
        }
        workCv_.notify_all();  // wake idle workers so retirees can exit
    }
    target_ = target;
}

unsigned TaskRunner::workers() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return target_;
}

void TaskRunner::run(std::function<void()> work) {
    pending_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        work_.push_back(std::move(work));
    }
    workCv_.notify_one();
}

void TaskRunner::run(std::function<void()> work, std::function<void()> onMain) {
    run([this, work = std::move(work), onMain = std::move(onMain)]() mutable {
        work();
        postMain(std::move(onMain));
    });
}

void TaskRunner::postMain(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mainMutex_);
    main_.push_back(std::move(fn));
}

size_t TaskRunner::drainMain() {
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(mainMutex_);
        batch.swap(main_);
    }
    for (auto& fn : batch) fn();
    return batch.size();
}

void TaskRunner::workerLoop(std::shared_ptr<std::atomic<bool>> quit,
                            std::shared_ptr<std::atomic<bool>> done) {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this, &quit] {
                return stop_.load() || quit->load() || !work_.empty();
            });
            // Retire between jobs; drain the queue first on full shutdown.
            if (quit->load() && !stop_.load()) break;
            if (stop_.load() && work_.empty()) break;
            job = std::move(work_.front());
            work_.pop_front();
        }
        job();
        pending_.fetch_sub(1);
    }
    done->store(true);
}

bool PromptBroker::wait(Pending request, std::function<void()> onPosted) {
    std::unique_lock<std::mutex> lock(mutex_);
    // One prompt at a time keeps the UI honest: a second asker queues here.
    cv_.wait(lock, [this] { return !current_.has_value() || cancelAll_; });
    if (cancelAll_) return false;
    uint64_t id = request.id;
    current_ = std::move(request);
    if (onPosted) onPosted();
    cv_.wait(lock, [this, id] { return resolvedId_ == id || cancelAll_; });
    if (cancelAll_) {
        current_.reset();
        return false;
    }
    bool answered = resolvedAnswered_;
    current_.reset();
    resolvedId_ = 0;
    cv_.notify_all();  // admit the next queued asker
    return answered;
}

std::optional<PromptBroker::Pending> PromptBroker::current() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

void PromptBroker::resolve(uint64_t id, bool answered) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_ || current_->id != id) return;
    resolvedId_ = id;
    resolvedAnswered_ = answered;
    cv_.notify_all();
}

void PromptBroker::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelAll_ = true;
    cv_.notify_all();
}

}  // namespace tb
