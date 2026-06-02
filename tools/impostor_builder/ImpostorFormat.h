/// M45.4 — Format binaire de l'atlas d'impostors octaédriques (outil offline).
///
/// Cet en-tête est volontairement AUTONOME : il ne dépend ni du moteur
/// (engine_core), ni de Vulkan. Le format est versionné « champ par champ »
/// en little-endian pour la portabilité (Windows/Linux), à la manière de
/// OutputVersion.cpp côté moteur, mais répliqué inline ici.
///
/// Disposition du fichier FORMAT v2 (voir FORMAT.md pour le détail des offsets) :
///   [ ImpostorHeader      24 octets, champ par champ ]
///   [ ImpostorAtlasInfo   métadonnées atlas         ]
///   [ uint64 albedoSize ][ albedoSize octets RGBA8  ]
///   [ uint64 normalSize ][ normalSize octets RGBA8  ]
///   [ uint64 ormSize    ][ ormSize    octets RGBA8  ]

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
	/// v2 : ajoute un TROISIÈME atlas (orm) après albedo+normal, albedo
	/// échantillonné depuis les textures baseColor, contentHash = FNV-1a 64.
	constexpr uint32_t kImpostorVersion = 2u;

	/// En-tête fixe de 24 octets, écrit/lu champ par champ (little-endian).
	/// IMPORTANT : ne JAMAIS dépendre du layout mémoire du struct sur disque —
	/// on sérialise explicitement chaque champ. Le static_assert ne sert qu'à
	/// documenter la taille logique attendue (4*4 + 8 = 24).
	struct ImpostorHeader
	{
		uint32_t magic         = kImpostorMagic;  ///< kImpostorMagic.
		uint32_t formatVersion = kImpostorVersion; ///< Version du format disque.
		uint32_t builderVersion = 2u;             ///< Version de l'outil ayant produit le fichier.
		uint32_t engineVersion  = 0u;             ///< Version moteur cible (0 = indéfini).
		uint64_t contentHash    = 0u;             ///< Hash FNV-1a 64 du .gltf source (0 si non calculé).
	};
	static_assert(sizeof(ImpostorHeader) == 24, "ImpostorHeader doit faire 24 octets");

	/// Métadonnées décrivant la grille d'atlas et les bounds du mesh source.
	struct ImpostorAtlasInfo
	{
		uint32_t viewsPerAxis = 0u; ///< N : la grille fait N×N tiles (vues).
		uint32_t tileSize     = 0u; ///< Côté d'une tile en pixels (carrée).
		uint32_t channels     = 4u; ///< Canaux par texel (toujours 4 = RGBA8).
		float    boundsMin[3] = {0.0f, 0.0f, 0.0f}; ///< Coin min de l'AABB monde du mesh.
		float    boundsMax[3] = {0.0f, 0.0f, 0.0f}; ///< Coin max de l'AABB monde du mesh.
	};

	/// Écrit un fichier d'atlas d'impostors complet (header + info + 3 atlas).
	/// FORMAT v2 : sérialise les trois atlas dans l'ordre disque albedo, normal, orm.
	/// \param path        Chemin de sortie (créé/écrasé).
	/// \param info        Métadonnées de la grille (viewsPerAxis, tileSize, bounds).
	/// \param contentHash Hash du mesh source (FNV-1a 64) écrit dans header.contentHash.
	/// \param albedo      Atlas albedo RGBA8, taille attendue =
	///                    (viewsPerAxis*tileSize)^2 * 4 octets.
	/// \param normal      Atlas normal RGBA8, même taille que l'albedo.
	/// \param orm         Atlas ORM RGBA8, même taille que l'albedo.
	/// \param err         Message d'erreur lisible en cas d'échec.
	/// \return true si l'écriture a réussi.
	/// Effet de bord : crée/écrase le fichier sur disque.
	bool WriteImpostorFile(const std::string& path,
	                       const ImpostorAtlasInfo& info,
	                       uint64_t contentHash,
	                       const std::vector<uint8_t>& albedo,
	                       const std::vector<uint8_t>& normal,
	                       const std::vector<uint8_t>& orm,
	                       std::string& err);

	/// Relit un fichier d'atlas d'impostors et restitue info + 3 atlas (round-trip).
	/// \param path          Chemin du fichier à lire.
	/// \param outInfo       Métadonnées restituées.
	/// \param outContentHash Hash de contenu lu depuis le header.
	/// \param outAlbedo     Atlas albedo RGBA8 restitué.
	/// \param outNormal     Atlas normal RGBA8 restitué.
	/// \param outOrm        Atlas ORM RGBA8 restitué.
	/// \param err           Message d'erreur lisible en cas d'échec.
	/// \return true si la lecture et la validation (magic/version==2/tailles) ont réussi.
	bool ReadImpostorFile(const std::string& path,
	                      ImpostorAtlasInfo& outInfo,
	                      uint64_t& outContentHash,
	                      std::vector<uint8_t>& outAlbedo,
	                      std::vector<uint8_t>& outNormal,
	                      std::vector<uint8_t>& outOrm,
	                      std::string& err);
}
