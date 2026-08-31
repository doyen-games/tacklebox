#include "core/task_runner.hpp"

#include "core/log.hpp"

namespace tb {

TaskRunner::TaskRunner(unsigned workers) {
    unsigned n = workers ? workers : std::thread::hardware_concurrency();
    if (n < 2) n = 2;
    if (n > 4) n = 4;  // chain RPC + KDF work; more threads buy nothing
    threads_.reserve(n);
    for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { workerLoop(); });
}

TaskRunner::~TaskRunner() {
    stop_.store(true);
    workCv_.notify_all();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
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

void TaskRunner::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this] { return stop_.load() || !work_.empty(); });
            if (stop_.load() && work_.empty()) return;
            job = std::move(work_.front());
            work_.pop_front();
        }
        job();
        pending_.fetch_sub(1);
    }
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
