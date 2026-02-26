// engine/core/jobs/tests/job_system_test.cpp
// M00.3 — Job System smoke tests.
//
// Tests:
//   1. 1k jobs — basic correctness.
//   2. 100k jobs — throughput + no deadlock.
//   3. Nested jobs — job enqueues more jobs.
//   4. Multi-producer — multiple threads enqueue simultaneously.
//   5. JobGroup Wait — batch completion.

#include "engine/core/jobs/JobSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace engine::core::jobs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void Expect(bool condition, const char* msg) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::abort();
    }
    std::fprintf(stdout, "[PASS] %s\n", msg);
}

// ---------------------------------------------------------------------------
// Test 1: 1 000 jobs, check counter
// ---------------------------------------------------------------------------
static void Test1k() {
    constexpr int N = 1'000;
    std::atomic<int> counter{0};

    JobGroup g;
    for (int i = 0; i < N; ++i) {
        g.Add([&counter]{ counter.fetch_add(1, std::memory_order_relaxed); });
    }
    g.Wait();

    Expect(counter.load() == N, "1k jobs: counter == 1000");
}

// ---------------------------------------------------------------------------
// Test 2: 100 000 jobs
// ---------------------------------------------------------------------------
static void Test100k() {
    constexpr int N = 100'000;
    std::atomic<int> counter{0};

    auto t0 = std::chrono::steady_clock::now();

    JobGroup g;
    for (int i = 0; i < N; ++i) {
        g.Add([&counter]{ counter.fetch_add(1, std::memory_order_relaxed); });
    }
    g.Wait();

    auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::fprintf(stdout, "[INFO] 100k jobs completed in %.1f ms\n", ms);

    Expect(counter.load() == N, "100k jobs: counter == 100000");
}

// ---------------------------------------------------------------------------
// Test 3: Nested jobs
// ---------------------------------------------------------------------------
static void TestNested() {
    std::atomic<int> counter{0};

    JobGroup outer;
    constexpr int OUTER = 10;
    constexpr int INNER = 50;

    for (int i = 0; i < OUTER; ++i) {
        outer.Add([&counter]{
            // From within a worker, enqueue more jobs via a nested group.
            JobGroup inner;
            for (int j = 0; j < INNER; ++j) {
                inner.Add([&counter]{ counter.fetch_add(1, std::memory_order_relaxed); });
            }
            inner.Wait(); // Wait from worker thread — must not deadlock.
        });
    }
    outer.Wait();

    Expect(counter.load() == OUTER * INNER, "nested jobs: counter == 500");
}

// ---------------------------------------------------------------------------
// Test 4: Multi-producer
// ---------------------------------------------------------------------------
static void TestMultiProducer() {
    constexpr int PRODUCERS = 4;
    constexpr int JOBS_PER  = 1'000;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);

    std::atomic<int> groupsDone{0};
    std::vector<std::unique_ptr<JobGroup>> groups(PRODUCERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        groups[p] = std::make_unique<JobGroup>();
        JobGroup* gptr = groups[p].get();
        producers.emplace_back([gptr, &counter]{
            for (int i = 0; i < JOBS_PER; ++i) {
                gptr->Add([&counter]{ counter.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto& t : producers) t.join();
    for (auto& g : groups) g->Wait();

    Expect(counter.load() == PRODUCERS * JOBS_PER, "multi-producer: counter == 4000");
}

// ---------------------------------------------------------------------------
// Test 5: JobHandle single-job wait
// ---------------------------------------------------------------------------
static void TestJobHandle() {
    std::atomic<bool> ran{false};
    JobHandle h = Jobs::Enqueue([&ran]{ ran.store(true, std::memory_order_release); });
    Jobs::Wait(h);
    Expect(h.IsDone(),        "JobHandle: IsDone() after Wait");
    Expect(ran.load(),        "JobHandle: job ran");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::fprintf(stdout, "=== Job System Tests ===\n");

    Jobs::Init(0); // use hardware_concurrency - 1

    Test1k();
    Test100k();
    TestNested();
    TestMultiProducer();
    TestJobHandle();

    Jobs::Shutdown();

    std::fprintf(stdout, "=== All tests passed ===\n");
    return 0;
}
