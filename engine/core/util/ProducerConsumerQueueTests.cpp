/**
 * Unit tests for engine::core::util::ProducerConsumerQueue.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/core/util/ProducerConsumerQueue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using engine::core::util::ProducerConsumerQueue;

static void TestSinglePushPop()
{
	ProducerConsumerQueue<int> q;
	q.Push(42);
	int v = 0;
	Assert(q.TryPop(v), "TryPop succeeds when non-empty");
	Assert(v == 42, "value preserved");
	Assert(!q.TryPop(v), "TryPop fails when empty");
}

static void TestFifoOrder()
{
	ProducerConsumerQueue<int> q;
	for (int i = 0; i < 100; ++i)
	{
		q.Push(i);
	}
	for (int i = 0; i < 100; ++i)
	{
		int v = -1;
		Assert(q.TryPop(v), "FIFO pop");
		Assert(v == i, "FIFO order");
	}
}

static void TestWaitAndPopTimeout()
{
	ProducerConsumerQueue<int> q;
	int v = 0;
	const auto t0 = std::chrono::steady_clock::now();
	const bool ok = q.WaitAndPop(v, std::chrono::milliseconds(50));
	const auto elapsed = std::chrono::steady_clock::now() - t0;
	Assert(!ok, "timeout returns false");
	Assert(elapsed >= std::chrono::milliseconds(40), "actually waited (>=40ms)");
}

static void TestWaitAndPopWakesOnPush()
{
	ProducerConsumerQueue<int> q;
	std::atomic<bool> got{false};
	std::thread t([&] {
		int v = 0;
		if (q.WaitAndPop(v, std::chrono::seconds(2)) && v == 7)
		{
			got = true;
		}
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	q.Push(7);
	t.join();
	Assert(got.load(), "WaitAndPop wakes when item pushed");
}

static void TestCancelUnblocks()
{
	ProducerConsumerQueue<int> q;
	std::atomic<bool> returned{false};
	std::thread t([&] {
		int v = 0;
		q.WaitAndPop(v, std::chrono::seconds(5));
		returned = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	q.Cancel();
	t.join();
	Assert(returned.load(), "Cancel unblocks WaitAndPop");
	Assert(q.IsCancelled(), "IsCancelled true");
}

static void TestCancelDrainsRemainingItems()
{
	// Comportement attendu : Cancel n'efface pas la file. Les items déjà
	// présents peuvent être consommés normalement, ce qui est utile pour un
	// shutdown propre où le worker draine sa file avant de quitter.
	ProducerConsumerQueue<int> q;
	q.Push(1);
	q.Push(2);
	q.Cancel();
	int v = 0;
	Assert(q.WaitAndPop(v, std::chrono::milliseconds(10)) && v == 1, "drain item 1");
	Assert(q.WaitAndPop(v, std::chrono::milliseconds(10)) && v == 2, "drain item 2");
	Assert(!q.WaitAndPop(v, std::chrono::milliseconds(10)), "no more items after drain");
}

static void TestMultiProducerMultiConsumerNoLoss()
{
	constexpr int kProducers = 4;
	constexpr int kPerProducer = 1000;
	constexpr int kConsumers = 2;
	constexpr int kTotal = kProducers * kPerProducer;

	ProducerConsumerQueue<int> q;
	std::vector<std::thread> producers;
	for (int p = 0; p < kProducers; ++p)
	{
		producers.emplace_back([&q, p] {
			for (int i = 0; i < kPerProducer; ++i)
			{
				q.Push(p * kPerProducer + i);
			}
		});
	}

	std::atomic<int> consumed{0};
	std::vector<int> received;
	std::mutex receivedMtx;
	std::vector<std::thread> consumers;
	for (int c = 0; c < kConsumers; ++c)
	{
		consumers.emplace_back([&] {
			while (consumed.load() < kTotal)
			{
				int v = 0;
				if (q.WaitAndPop(v, std::chrono::milliseconds(50)))
				{
					{
						std::lock_guard<std::mutex> lk(receivedMtx);
						received.push_back(v);
					}
					consumed.fetch_add(1);
				}
			}
		});
	}

	for (auto& t : producers) t.join();
	// Au cas où des consumers timeout pile au moment où la prod finit.
	while (consumed.load() < kTotal)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	q.Cancel();
	for (auto& t : consumers) t.join();

	Assert(static_cast<int>(received.size()) == kTotal, "all items consumed (no loss, no duplicate)");

	std::set<int> dedup(received.begin(), received.end());
	Assert(static_cast<int>(dedup.size()) == kTotal, "no duplicates");
}

int main()
{
	TestSinglePushPop();
	TestFifoOrder();
	TestWaitAndPopTimeout();
	TestWaitAndPopWakesOnPush();
	TestCancelUnblocks();
	TestCancelDrainsRemainingItems();
	TestMultiProducerMultiConsumerNoLoss();

	if (s_failCount > 0)
	{
		std::cerr << "[ProducerConsumerQueueTests] " << s_failCount << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "[ProducerConsumerQueueTests] OK" << std::endl;
	return 0;
}
