#pragma once

#include "src/client/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>
#include <string_view>

namespace engine::world { class StreamCache; }

namespace engine::world::terrain
{
	/// Charge un `TerrainChunk` depuis le `StreamCache` (lookup pur). Si la clé
	/// `cacheKey` n'est pas en cache, retourne `nullptr` (le caller doit alors
	/// charger depuis disque + Insert dans le cache, puis rappeler).
	/// \return shared_ptr<TerrainChunk> partagé sur succès, nullptr si miss ou
	/// désérialisation échouée (`outError` renseigné dans ce dernier cas).
	std::shared_ptr<TerrainChunk> LoadFromCache(
		engine::world::StreamCache& cache,
		std::string_view cacheKey,
		std::string& outError);

	/// Forme la clé canonique d'un `terrain.bin` dans le `StreamCache` :
	/// `chunks/chunk_<i>_<j>/terrain.bin`. Utilisée par tous les chemins de
	/// lecture (éditeur + client).
	std::string MakeTerrainCacheKey(int chunkX, int chunkZ);
}
