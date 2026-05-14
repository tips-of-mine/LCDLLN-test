#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world::volumes::dungeons
{
	/// Entrée du catalogue de templates de donjon (M100.43). Décrit un
	/// donjon disponible : id (consommé par master), display name,
	/// niveau min, range de difficulty, mesh décoratif optionnel.
	///
	/// Le catalogue est purement descriptif côté éditeur — c'est le
	/// master (M100.44) qui valide qu'un `dungeonTemplateId` est connu
	/// à la réception de `kOpcodeEnterDungeonRequest`. Si inconnu :
	/// `kEnterDungeonErrorTemplateNotFound`.
	struct DungeonCatalogEntry
	{
		std::string id;                ///< matche kMaxDungeonTemplateIdBytes (64 octets)
		std::string displayName;
		std::string description;
		std::string decorativeMeshPath; ///< glTF optionnel pour l'arche d'entrée
		std::string thumbnailPath;
		uint16_t    requiredLevel = 1u;
		uint8_t     minDifficulty = 1u;
		uint8_t     maxDifficulty = 1u;
	};

	/// Loader JSON minimal pour `game/data/meshes/dungeons/catalog.json`.
	/// Pattern hand-rolled, parallèle aux CaveCatalog/OverhangCatalog/ArchCatalog.
	class DungeonCatalog
	{
	public:
		bool LoadFromContent(const std::string& contentRoot, std::string& outError);
		bool ParseJson(const std::string& jsonText, std::string& outError);

		const std::vector<DungeonCatalogEntry>& Entries() const { return m_entries; }
		size_t Size() const { return m_entries.size(); }

		const DungeonCatalogEntry* FindById(const std::string& id) const;

	private:
		std::vector<DungeonCatalogEntry> m_entries;
	};
}
