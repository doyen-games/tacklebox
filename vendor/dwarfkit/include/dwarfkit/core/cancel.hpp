// The one concurrency primitive (BLUEPRINT.md 5.2): a shared atomic flag plus
// condition variable. UI prompts and callback waits take one so a wallet flow
// can race "user closed the prompt" against "wallet answered".
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace dwarfkit {

class CancelToken {
public:
    CancelToken() : state_(std::make_shared<State>()) {}

    // Cancel from any thread; wakes every waiter sharing this token.
    void cancel() {
        {
            const std::lock_guard<std::mutex> lock(state_->mutex);
            state_->cancelled = true;
        }
        state_->cv.notify_all();
    }

    bool cancelled() const { return state_->cancelled.load(); }

    // Sleep until the timeout elapses or the token is cancelled. Returns
    // cancelled() so callers can `if (token.waitFor(backoff)) return ...`.
    bool waitFor(std::chrono::milliseconds timeout) const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->cv.wait_for(lock, timeout, [&] { return state_->cancelled.load(); });
        return state_->cancelled.load();
    }

private:
    struct State {
        std::atomic<bool> cancelled{false};
        std::mutex mutex;
        std::condition_variable cv;
    };
    std::shared_ptr<State> state_;
};

}  // namespace dwarfkit
