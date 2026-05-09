#pragma once
// CMANGOS.34 (Phase 4.34a) — MetricRegistry : compteurs et histogrammes
// internes (ticks/sec, queries/sec, AI updates/sec) pour observabilite.
// Header-only. Differe de PrometheusMetrics (deja present) en exposant un
// registry generic interrogeable a tout instant pour debug/admin.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace engine::server::metric
{
	/// Compteur monotone (incremente uniquement). Sert a quantifier les
	/// evenements cumules.
	struct Counter
	{
		uint64_t value = 0;
		void Inc(uint64_t n = 1) { value += n; }
	};

	/// Gauge : valeur instantanee (peut monter ou descendre). Sessions actives,
	/// memoire residente, etc.
	struct Gauge
	{
		int64_t value = 0;
		void Set(int64_t v) { value = v; }
		void Add(int64_t d) { value += d; }
	};

	/// Histogramme simple (buckets fixes, fournis a la creation). Pour
	/// distribution de latence, taille de paquet, etc.
	struct Histogram
	{
		std::vector<uint64_t> bounds;     ///< borne sup inclusive de chaque bucket
		std::vector<uint64_t> bucketCount;///< meme taille que bounds + 1 (overflow)
		uint64_t totalCount = 0;
		double   sum        = 0.0;

		void Observe(double v)
		{
			totalCount++;
			sum += v;
			for (size_t i = 0; i < bounds.size(); ++i)
			{
				if (v <= static_cast<double>(bounds[i]))
				{
					bucketCount[i]++;
					return;
				}
			}
			bucketCount.back()++;
		}

		double Mean() const
		{
			return totalCount ? sum / static_cast<double>(totalCount) : 0.0;
		}
	};

	class MetricRegistry
	{
	public:
		Counter& GetCounter(const std::string& name) { return m_counters[name]; }
		Gauge&   GetGauge(const std::string& name)   { return m_gauges[name]; }

		Histogram& CreateHistogram(const std::string& name, std::vector<uint64_t> bounds)
		{
			auto& h = m_histograms[name];
			h.bounds = std::move(bounds);
			h.bucketCount.assign(h.bounds.size() + 1, 0);
			h.totalCount = 0;
			h.sum = 0.0;
			return h;
		}

		Histogram* GetHistogram(const std::string& name)
		{
			auto it = m_histograms.find(name);
			return (it == m_histograms.end()) ? nullptr : &it->second;
		}

		size_t CounterCount() const   { return m_counters.size(); }
		size_t GaugeCount() const     { return m_gauges.size(); }
		size_t HistogramCount() const { return m_histograms.size(); }

	private:
		std::unordered_map<std::string, Counter>   m_counters;
		std::unordered_map<std::string, Gauge>     m_gauges;
		std::unordered_map<std::string, Histogram> m_histograms;
	};
}
