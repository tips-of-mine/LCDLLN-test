#pragma once

// FIX.13 — Résumé texte d'une frame du profileur, pour le journal client.
//
// Le profileur (Profiler) mesure déjà les passes GPU du frame graph et les
// scopes CPU (PROFILE_SCOPE), mais rien ne les journalisait : impossible de
// savoir, depuis un log joueur, si une chute d'images par seconde vient du CPU
// ou du GPU. Ce module formate une ligne compacte :
//   cpu=83.1ms gpu=12.4ms | cpu: Render=61.2 Update=20.4 … | gpu: geometry=6.1 lighting=3.0 …
// Les scopes CPU de même nom sont additionnés (une boucle peut en produire
// plusieurs), puis triés par durée décroissante (égalité : ordre alphabétique).
//
// Header-only, sans état : testé par profiler_summary_tests.

#include "src/shared/core/Profiler.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::core
{
	/// Durées (ms) par nom, additionnées, triées décroissantes ; au plus \p topN entrées.
	inline std::vector<std::pair<std::string, double>> TopCpuScopes(const std::vector<CpuProfileSample>& scopes, size_t topN)
	{
		std::map<std::string, double> sum;
		for (const CpuProfileSample& s : scopes)
			sum[s.name] += s.durationMs;
		std::vector<std::pair<std::string, double>> v(sum.begin(), sum.end());
		std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		if (v.size() > topN)
			v.resize(topN);
		return v;
	}

	/// Passes GPU valides, additionnées par nom, triées décroissantes ; au plus \p topN.
	inline std::vector<std::pair<std::string, double>> TopGpuPasses(const std::vector<GpuProfileSample>& passes, size_t topN)
	{
		std::map<std::string, double> sum;
		for (const GpuProfileSample& p : passes)
			if (p.valid)
				sum[p.name] += p.durationMs;
		std::vector<std::pair<std::string, double>> v(sum.begin(), sum.end());
		std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		if (v.size() > topN)
			v.resize(topN);
		return v;
	}

	/// Ligne de résumé d'une frame : totaux CPU/GPU puis les \p topN plus gros
	/// scopes CPU et passes GPU (ms, 1 décimale).
	inline std::string FormatProfilerSummary(const ProfilerFrameSnapshot& s, size_t topN)
	{
		char buf[64];
		std::string out;
		std::snprintf(buf, sizeof(buf), "cpu=%.1fms gpu=%.1fms", s.cpuTotalMs, s.gpuTotalMs);
		out += buf;
		auto append = [&](const char* label, const std::vector<std::pair<std::string, double>>& v)
		{
			out += " | ";
			out += label;
			out += ":";
			if (v.empty())
				out += " -";
			for (const auto& [name, ms] : v)
			{
				std::snprintf(buf, sizeof(buf), "=%.1f", ms);
				out += " ";
				out += name;
				out += buf;
			}
		};
		append("cpu", TopCpuScopes(s.cpuScopes, topN));
		append("gpu", TopGpuPasses(s.gpuPasses, topN));
		return out;
	}
}
