#include "src/shared/metric/MetricRegistry.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::metric;

	bool TestCounter()
	{
		MetricRegistry r;
		auto& c = r.GetCounter("ticks");
		c.Inc();
		c.Inc(5);
		if (c.value != 6) return false;
		// meme nom retourne meme compteur
		auto& c2 = r.GetCounter("ticks");
		if (c2.value != 6) return false;
		LOG_INFO(Core, "[MetricTests] counter OK");
		return true;
	}

	bool TestGauge()
	{
		MetricRegistry r;
		auto& g = r.GetGauge("sessions");
		g.Set(10);
		g.Add(-3);
		if (g.value != 7) return false;
		LOG_INFO(Core, "[MetricTests] gauge OK");
		return true;
	}

	bool TestHistogramBuckets()
	{
		MetricRegistry r;
		auto& h = r.CreateHistogram("lat_ms", {10, 50, 100, 500});
		h.Observe(5);   // bucket 0
		h.Observe(10);  // bucket 0 (inclusif)
		h.Observe(30);  // bucket 1
		h.Observe(200); // bucket 3
		h.Observe(1000);// overflow bucket
		if (h.totalCount != 5) return false;
		if (h.bucketCount.size() != 5) return false;
		if (h.bucketCount[0] != 2) return false;
		if (h.bucketCount[1] != 1) return false;
		if (h.bucketCount[2] != 0) return false;
		if (h.bucketCount[3] != 1) return false;
		if (h.bucketCount[4] != 1) return false; // overflow
		// mean = (5+10+30+200+1000)/5 = 249
		if (h.Mean() < 248.5 || h.Mean() > 249.5) return false;
		LOG_INFO(Core, "[MetricTests] histogram buckets OK");
		return true;
	}

	bool TestRegistrySize()
	{
		MetricRegistry r;
		r.GetCounter("a"); r.GetCounter("b");
		r.GetGauge("g");
		r.CreateHistogram("h", {1, 2});
		if (r.CounterCount() != 2 || r.GaugeCount() != 1 || r.HistogramCount() != 1)
			return false;
		LOG_INFO(Core, "[MetricTests] registry size OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestCounter() && TestGauge() && TestHistogramBuckets() && TestRegistrySize();
	if (ok) LOG_INFO(Core, "[MetricTests] ALL OK");
	else LOG_ERROR(Core, "[MetricTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
