#pragma once

// M100.29 — Document de splines de la zone. Header-only.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "src/client/world/spline/SplineInstances.h"

namespace engine::editor::world
{
	class SplineDocument
	{
	public:
		uint32_t Add(const engine::world::spline::Spline& s)
		{
			const uint32_t id = static_cast<uint32_t>(m_splines.size());
			m_splines.push_back(s);
			return id;
		}
		void RemoveLast() { if (!m_splines.empty()) m_splines.pop_back(); }
		const std::vector<engine::world::spline::Spline>& All() const { return m_splines; }
		std::vector<engine::world::spline::Spline>& Mutable() { return m_splines; }

		bool SaveToDisk(const std::string& path, std::string& err) const
		{
			const std::vector<uint8_t> bytes = engine::world::spline::SaveSplinesBin(m_splines);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) { err = "SplineDocument::SaveToDisk: open failed: " + path; return false; }
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!out.good()) { err = "SplineDocument::SaveToDisk: write failed: " + path; return false; }
			return true;
		}

		/// Roadmap-8 (dette audit 7.2) — Relit `splines.bin` et REMPLACE le
		/// contenu courant. Fichier absent = succès avec liste vide.
		/// \return false si le fichier existe mais est corrompu.
		bool LoadFromDisk(const std::string& path, std::string& err)
		{
			m_splines.clear();
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) return true; // pas de splines : OK
			std::ifstream in(path, std::ios::binary);
			if (!in.good()) { err = "SplineDocument::LoadFromDisk: open failed: " + path; return false; }
			std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			return engine::world::spline::LoadSplinesBin(bytes, m_splines, err);
		}

		/// Roadmap-8 — Vide toutes les splines (changement de carte).
		void Clear() { m_splines.clear(); }

	private:
		std::vector<engine::world::spline::Spline> m_splines;
	};
}
