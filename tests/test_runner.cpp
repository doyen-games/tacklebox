#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/task_runner.hpp"

using namespace tb;

namespace {

// Wait for a counter to reach a value, far past any sane completion time.
bool waitFor(const std::atomic<int>& counter, int expected) {
    for (int i = 0; i < 5000; ++i) {
        if (counter.load() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return counter.load() == expected;
}

}  // namespace

TEST_CASE("worker pool sizing") {
    CHECK(TaskRunner::conservativeWorkers() == 2);
    CHECK(TaskRunner::autoWorkers() >= 2);
    CHECK(TaskRunner::autoWorkers() <= 16);
}

TEST_CASE("pool resizes at runtime without losing work") {
    TaskRunner runner(2);
    CHECK(runner.workers() == 2);

    std::atomic<int> done{0};
    for (int i = 0; i < 32; ++i)
        runner.run([&done] { done.fetch_add(1); });
    CHECK(waitFor(done, 32));

    // Grow mid-flight and keep executing.
    runner.setWorkers(6);
    CHECK(runner.workers() == 6);
    for (int i = 0; i < 64; ++i)
        runner.run([&done] { done.fetch_add(1); });
    CHECK(waitFor(done, 96));

    // Shrink to a single worker; queued work still completes.
    runner.setWorkers(1);
    CHECK(runner.workers() == 1);
    for (int i = 0; i < 16; ++i)
        runner.run([&done] { done.fetch_add(1); });
    CHECK(waitFor(done, 112));

    // Clamps.
    runner.setWorkers(0);
    CHECK(runner.workers() == 1);
    runner.setWorkers(999);
    CHECK(runner.workers() == 32);
}

TEST_CASE("pool destruction drains queued work") {
    std::atomic<int> done{0};
    {
        TaskRunner runner(3);
        for (int i = 0; i < 24; ++i)
            runner.run([&done] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                done.fetch_add(1);
            });
    }  // ~TaskRunner joins after the queue empties
    CHECK(done.load() == 24);
}
