#pragma once

// M101.3 — Lecteur client du segment `routines.bin`.
//
// Mince wrapper autour du codec pur `routine_graph` : lit les octets d'un
// fichier et les décode en graphes. Header-only (pas d'ajout à la liste de
// sources d'engine_core). Le runtime ne tente la lecture que si le flag
// `kChunkMetaHasRoutines` est présent (rétro-compatibilité des paquets anciens).

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "src/shared/routine/RoutineGraph.h"
#include "src/shared/routine/RoutineSegmentCodec.h"

namespace engine::world::routine
{
	struct LoadedRoutines
	{
		std::vector<engine::routine::RoutineGraph> graphs;
	};

	/// Décode des octets `routines.bin` déjà en mémoire.
	inline bool ReadRoutines(const std::vector<uint8_t>& bytes, LoadedRoutines& out, std::string& err)
	{
		auto graphs = engine::routine::codec::DecodeRoutinesBin(bytes, err);
		if (!graphs) return false;
		out.graphs = std::move(*graphs);
		return true;
	}

	/// Lit et décode un fichier `routines.bin`.
	inline bool ReadRoutinesFile(const std::string& path, LoadedRoutines& out, std::string& err)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in.good())
		{
			err = "ReadRoutinesFile: open failed: " + path;
			return false;
		}
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
		                           std::istreambuf_iterator<char>());
		return ReadRoutines(bytes, out, err);
	}
}
