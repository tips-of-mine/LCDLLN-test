/// M45.4 — Format binaire de l'atlas d'impostors octaédriques (outil offline).
///
/// Cet en-tête est volontairement AUTONOME : il ne dépend ni du moteur
/// (engine_core), ni de Vulkan. Le format est versionné « champ par champ »
/// en little-endian pour la portabilité (Windows/Linux), à la manière de
/// OutputVersion.cpp côté moteur, mais répliqué inline ici.
///
/// Disposition du fichier (voir FORMAT.md pour le détail des offsets) :
///   [ ImpostorHeader      24 octets, champ par champ ]
///   [ ImpostorAtlasInfo   métadonnées atlas         ]
///   [ uint64 albedoSize ][ albedoSize octets RGBA8  ]
///   [ uint64 normalSize ][ normalSize octets RGBA8  ]

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tools::impostor_builder
{
	/// Magic du fichier : octets ASCII 'M''I''P''O' encodés en little-endian.
	/// 0x4F50494D = 'O'(0x4F)<<24 | 'P'(0x50)<<16 | 'I'(0x49)<<8 | 'M'(0x4D).
	/// Choisi unique (non utilisé ailleurs dans le dépôt).
	constexpr uint32_t kImpostorMagic = 0x4F50494Du;

	/// Version du format binaire. Incrémenter à chaque changement non
	/// rétro-compatible de la disposition disque.
	constexpr uint32_t kImpostorVersion = 1u;

	/// En-tête fixe de 24 octets, écrit/lu champ par champ (little-endian).
	/// IMPORTANT : ne JAMAIS dépendre du layout mémoire du struct sur disque —
	/// on sérialise explicitement chaque champ. Le static_assert ne sert qu'à
	/// documenter la taille logique attendue (4*4 + 8 = 24).
	struct ImpostorHeader
	{
		uint32_t magic         = kImpostorMagic;  ///< kImpostorMagic.
		uint32_t formatVersion = kImpostorVersion; ///< Version du format disque.
		uint32_t builderVersion = 1u;             ///< Version de l'outil ayant produit le fichier.
		uint32_t engineVersion  = 0u;             ///< Version moteur cible (0 = indéfini en v1).
		uint64_t contentHash    = 0u;             ///< Hash du contenu (mesh source), 0 si non calculé.
	};
	static_assert(sizeof(ImpostorHeader) == 24, "ImpostorHeader doit faire 24 octets");

	/// Métadonnées décrivant la grille d'atlas et les bounds du mesh source.
	struct ImpostorAtlasInfo
	{
		uint32_t viewsPerAxis = 0u; ///< N : la grille fait N×N tiles (vues).
		uint32_t tileSize     = 0u; ///< Côté d'une tile en pixels (carrée).
		uint32_t channels     = 4u; ///< Canaux par texel (toujours 4 = RGBA8 en v1).
		float    boundsMin[3] = {0.0f, 0.0f, 0.0f}; ///< Coin min de l'AABB monde du mesh.
		float    boundsMax[3] = {0.0f, 0.0f, 0.0f}; ///< Coin max de l'AABB monde du mesh.
	};

	/// Écrit un fichier d'atlas d'impostors complet (header + info + atlas).
	/// \param path        Chemin de sortie (créé/écrasé).
	/// \param info        Métadonnées de la grille (viewsPerAxis, tileSize, bounds).
	/// \param albedoAtlas Atlas albedo RGBA8, taille attendue =
	///                    (viewsPerAxis*tileSize)^2 * 4 octets.
	/// \param normalAtlas Atlas normal RGBA8, même taille que l'albedo.
	/// \param outError    Message d'erreur lisible en cas d'échec.
	/// \return true si l'écriture a réussi.
	/// Effet de bord : crée/écrase le fichier sur disque.
	bool WriteImpostorFile(const std::string& path,
	                       const ImpostorAtlasInfo& info,
	                       const std::vector<uint8_t>& albedoAtlas,
	                       const std::vector<uint8_t>& normalAtlas,
	                       std::string& outError);

	/// Relit un fichier d'atlas d'impostors et restitue info + atlas (round-trip).
	/// \param path      Chemin du fichier à lire.
	/// \param outInfo   Métadonnées restituées.
	/// \param outAlbedo Atlas albedo RGBA8 restitué.
	/// \param outNormal Atlas normal RGBA8 restitué.
	/// \param outError  Message d'erreur lisible en cas d'échec.
	/// \return true si la lecture et la validation (magic/version/tailles) ont réussi.
	bool ReadImpostorFile(const std::string& path,
	                      ImpostorAtlasInfo& outInfo,
	                      std::vector<uint8_t>& outAlbedo,
	                      std::vector<uint8_t>& outNormal,
	                      std::string& outError);
}
