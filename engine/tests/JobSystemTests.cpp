#include "engine/core/jobs/JobSystem.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

void Test1kJobs() {
    std::atomic<int> counter{0};
    engine::core::jobs::JobGroup group;

    for (int i = 0; i < 1000; ++i) {
        engine::core::jobs::Jobs::Enqueue(group, [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.Wait();
    Require(counter.load(std::memory_order_relaxed) == 1000, "1k jobs counter mismatch");
}

void Test100kJobs() {
    std::atomic<int> counter{0};
    engine::core::jobs::JobGroup group;

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) {
        engine::core::jobs::Jobs::Enqueue(group, [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    group.Wait();
    const auto end = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    Require(counter.load(std::memory_order_relaxed) == 100000, "100k jobs counter mismatch");
    std::cout << "[INFO] 100k jobs completed in " << elapsedMs << " ms\n";
}

void TestNestedJobs() {
    std::atomic<int> counter{0};
    engine::core::jobs::JobGroup outer;

    for (int i = 0; i < 250; ++i) {
        engine::core::jobs::Jobs::Enqueue(outer, [&counter]() {
            engine::core::jobs::JobGroup inner;
            for (int j = 0; j < 20; ++j) {
                engine::core::jobs::Jobs::Enqueue(inner, [&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
            inner.Wait();
        });
    }

    outer.Wait();
    Require(counter.load(std::memory_order_relaxed) == 5000, "nested jobs counter mismatch");
}

void TestMultiProducer() {
    std::atomic<int> counter{0};
    engine::core::jobs::JobGroup group;

    std::vector<std::thread> producers;
    producers.reserve(4);

    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([&counter, &group]() {
            for (int i = 0; i < 5000; ++i) {
                engine::core::jobs::Jobs::Enqueue(group, [&counter]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    group.Wait();
    Require(counter.load(std::memory_order_relaxed) == 20000, "multi-producer jobs counter mismatch");
}

void TestWaitFromWorker() {
    std::atomic<int> counter{0};

    auto parent = engine::core::jobs::Jobs::Enqueue([&counter]() {
        engine::core::jobs::JobGroup nested;
        for (int i = 0; i < 1000; ++i) {
            engine::core::jobs::Jobs::Enqueue(nested, [&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        nested.Wait();
    });

    parent.Wait();
    Require(counter.load(std::memory_order_relaxed) == 1000, "wait-from-worker failed");
}

} // namespace

int main() {
    engine::core::jobs::Jobs::Init();

    Test1kJobs();
    Test100kJobs();
    TestNestedJobs();
    TestMultiProducer();
    TestWaitFromWorker();

    engine::core::jobs::Jobs::Shutdown();
    std::cout << "[PASS] JobSystem tests passed\n";
    return 0;
}
