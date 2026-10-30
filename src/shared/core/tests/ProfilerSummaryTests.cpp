// FIX.13 — Tests du résumé texte du profileur (ProfilerSummary.h).

#include "src/shared/core/ProfilerSummary.h"

#include <iostream>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	using namespace engine::core;
	ProfilerFrameSnapshot s;
	s.cpuTotalMs = 83.14;
	s.gpuTotalMs = 12.4;
	s.cpuScopes = { { "Update", 0, 20.0 }, { "Render", 0, 61.2 }, { "Scatter", 0, 3.0 }, { "Scatter", 0, 2.5 } };
	s.gpuPasses = { { "geometry", 6.1, true }, { "lighting", 3.0, true }, { "broken", 99.0, false } };

	const auto cpu = TopCpuScopes(s.cpuScopes, 2);
	Check(cpu.size() == 2u && cpu[0].first == "Render" && cpu[1].first == "Update", "cpu : tri decroissant, borne topN");
	const auto all = TopCpuScopes(s.cpuScopes, 10);
	bool scatterSummed = false;
	for (const auto& [n, ms] : all)
		if (n == "Scatter") scatterSummed = ms > 5.49 && ms < 5.51;
	Check(scatterSummed, "cpu : scopes de meme nom additionnes");

	const auto gpu = TopGpuPasses(s.gpuPasses, 5);
	Check(gpu.size() == 2u && gpu[0].first == "geometry", "gpu : passes invalides ignorees");

	const std::string line = FormatProfilerSummary(s, 3);
	Check(line == "cpu=83.1ms gpu=12.4ms | cpu: Render=61.2 Update=20.0 Scatter=5.5 | gpu: geometry=6.1 lighting=3.0",
		"ligne formatee");
	if (s_failCount != 0)
		std::cerr << "  obtenu : " << line << std::endl;

	const std::string empty = FormatProfilerSummary(ProfilerFrameSnapshot{}, 3);
	Check(empty == "cpu=0.0ms gpu=0.0ms | cpu: - | gpu: -", "snapshot vide");

	if (s_failCount == 0)
		std::cout << "[OK] profiler_summary_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
